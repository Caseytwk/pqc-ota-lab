/*
 * PQC OTA lab firmware.
 *
 * Boots, reads the trust anchor from the "keys" partition, then polls the
 * workshop update server. A new image is streamed into the inactive OTA slot,
 * its signature is verified over the written flash (memory mapped), and only
 * then is the slot activated. Output lines are part of the workshop notes:
 * change them here and in Notion together.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/soc.h"

#include "verify.h"

#define SERVER_URL        "http://10.0.2.2:8070"
#define FIRMWARE_URL      SERVER_URL "/firmware.bin"
#define SIGNATURE_URL     SERVER_URL "/firmware.sig"
#define POLL_INTERVAL_MS  5000
#define CHUNK_SIZE        4096
#define KEYS_SUBTYPE      ((esp_partition_subtype_t)0x40)
#define APP_DESC_OFFSET   (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))

static EventGroupHandle_t s_net_events;
#define NET_GOT_IP BIT0

static esp_eth_handle_t s_eth;
static esp_eth_mac_t *s_mac;
static esp_eth_phy_t *s_phy;
static esp_eth_netif_glue_handle_t s_glue;

/* OpenCores ethmac registers (QEMU), see esp_eth/src/openeth/openeth.h */
#define OPENETH_MODER_REG       (DR_REG_EMAC_BASE + 0x00)
#define OPENETH_INT_SOURCE_REG  (DR_REG_EMAC_BASE + 0x04)
#define OPENETH_INT_MASK_REG    (DR_REG_EMAC_BASE + 0x08)
#define OPENETH_MODER_RST       BIT(11)

static trust_anchor_t s_ta;
static uint8_t s_chunk[CHUNK_SIZE];
static uint8_t s_sig[TA_MAX_SIG_LEN + 1];

/* ---------- networking (QEMU OpenCores Ethernet) ---------- */

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    xEventGroupSetBits(s_net_events, NET_GOT_IP);
}

static void network_start(void)
{
    s_net_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.autonego_timeout_ms = 100;
    s_mac = esp_eth_mac_new_openeth(&mac_cfg);
    s_phy = esp_eth_phy_new_dp83848(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth));
    s_glue = esp_eth_new_netif_glue(s_eth);
    ESP_ERROR_CHECK(esp_netif_attach(netif, s_glue));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_eth_start(s_eth));

    xEventGroupWaitBits(s_net_events, NET_GOT_IP, pdFALSE, pdTRUE, portMAX_DELAY);
}

/*
 * A CPU software reset does not reset the Ethernet MAC or the interrupt matrix.
 * Without this, the new firmware takes a stale MAC interrupt during early
 * startup and crashes. Tear the driver down (which unroutes its interrupt),
 * then return the OpenCores MAC registers to their power-on state.
 */
static void network_shutdown(void)
{
    esp_eth_stop(s_eth);
    esp_eth_del_netif_glue(s_glue);
    if (esp_eth_driver_uninstall(s_eth) == ESP_OK) {
        s_phy->del(s_phy);
        s_mac->del(s_mac);
    }
    REG_WRITE(OPENETH_INT_MASK_REG, 0);
    REG_WRITE(OPENETH_INT_SOURCE_REG, 0xFFFFFFFF);
    REG_SET_BIT(OPENETH_MODER_REG, OPENETH_MODER_RST);
    REG_CLR_BIT(OPENETH_MODER_REG, OPENETH_MODER_RST);
}

/* ---------- HTTP helpers ---------- */

static esp_http_client_handle_t http_open(const char *url, int *status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 5000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (h == NULL) {
        return NULL;
    }
    if (esp_http_client_open(h, 0) != ESP_OK) {
        esp_http_client_cleanup(h);
        return NULL;
    }
    esp_http_client_fetch_headers(h);
    *status = esp_http_client_get_status_code(h);
    return h;
}

static void http_close(esp_http_client_handle_t h)
{
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
}

/* Read exactly len bytes unless the stream ends first. Returns bytes read. */
static int http_read_full(esp_http_client_handle_t h, uint8_t *buf, int len)
{
    int total = 0;
    while (total < len) {
        int r = esp_http_client_read(h, (char *)buf + total, len - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }
    return total;
}

/* Returns signature length, 0 if missing, -1 on error. */
static int fetch_signature(void)
{
    int status = 0;
    esp_http_client_handle_t h = http_open(SIGNATURE_URL, &status);
    if (h == NULL) {
        return -1;
    }
    if (status != 200) {
        http_close(h);
        return 0;
    }
    int len = http_read_full(h, s_sig, sizeof(s_sig));
    http_close(h);
    return (len > TA_MAX_SIG_LEN) ? -1 : len;
}

/* ---------- OTA ---------- */

typedef enum {
    POLL_NOTHING,     /* server unreachable or no firmware.bin yet, keep polling */
    POLL_LATEST,      /* server offers the version already running */
    POLL_DONE,        /* update attempted and rejected, stop polling */
} poll_result_t;

static void reject(const esp_partition_t *slot, const char *running_ver)
{
    esp_partition_erase_range(slot, 0, slot->size);
    printf("[OTA ] Update rejected, %s erased, staying on v%s\n", slot->label, running_ver);
}

static poll_result_t poll_once(const esp_app_desc_t *running)
{
    int status = 0;
    esp_http_client_handle_t h = http_open(FIRMWARE_URL, &status);
    if (h == NULL) {
        return POLL_NOTHING;
    }
    if (status != 200) {
        http_close(h);
        return POLL_NOTHING;
    }

    /* Read the image header and app descriptor to learn the offered version. */
    int have = http_read_full(h, s_chunk, sizeof(s_chunk));
    if (have < (int)(APP_DESC_OFFSET + sizeof(esp_app_desc_t))) {
        http_close(h);
        return POLL_NOTHING;
    }
    esp_app_desc_t offered;
    memcpy(&offered, s_chunk + APP_DESC_OFFSET, sizeof(offered));
    if (offered.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        http_close(h);
        printf("[OTA ] firmware.bin is not an ESP32 app image, ignoring\n");
        return POLL_DONE;
    }
    if (strncmp(offered.version, running->version, sizeof(offered.version)) == 0) {
        http_close(h);
        return POLL_LATEST;
    }

    printf("[OTA ] Found firmware v%s, downloading\n", offered.version);

    /* Stream the image into the inactive slot. Nothing is activated yet. */
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        http_close(h);
        printf("[OTA ] Could not open %s for writing\n", slot->label);
        return POLL_DONE;
    }
    size_t total = 0;
    bool write_ok = true;
    while (have > 0) {
        if (esp_ota_write(ota, s_chunk, have) != ESP_OK) {
            write_ok = false;
            break;
        }
        total += have;
        have = http_read_full(h, s_chunk, sizeof(s_chunk));
    }
    http_close(h);
    if (!write_ok || total > slot->size) {
        esp_ota_abort(ota);
        printf("[OTA ] Download or flash write failed\n");
        reject(slot, running->version);
        return POLL_DONE;
    }
    printf("[OTA ] Image %u KB written to %s\n", (unsigned)(total / 1024), slot->label);

    /* Fetch and check the detached signature. */
    int sig_len = fetch_signature();
    if (sig_len <= 0) {
        esp_ota_abort(ota);
        printf("[SIG ] firmware.sig missing or unreadable\n");
        reject(slot, running->version);
        return POLL_DONE;
    }
    printf("[SIG ] %s signature %d bytes\n", ta_name(s_ta.type), sig_len);

    /* Verify over the image exactly as written to flash. */
    const void *image = NULL;
    esp_partition_mmap_handle_t map;
    if (esp_partition_mmap(slot, 0, total, ESP_PARTITION_MMAP_DATA, &image, &map) != ESP_OK) {
        esp_ota_abort(ota);
        printf("[SIG ] Could not map %s for verification\n", slot->label);
        reject(slot, running->version);
        return POLL_DONE;
    }
    int64_t t0 = esp_timer_get_time();
    int rc = ta_verify(&s_ta, image, total, s_sig, sig_len);
    int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    esp_partition_munmap(map);

    if (rc != 0) {
        esp_ota_abort(ota);
        printf("[SIG ] Verify: FAIL\n");
        reject(slot, running->version);
        return POLL_DONE;
    }
    printf("[SIG ] Verify: PASS in %" PRId64 " ms\n", elapsed_ms);

    if (esp_ota_end(ota) != ESP_OK) {
        printf("[OTA ] Signature valid but image format invalid\n");
        reject(slot, running->version);
        return POLL_DONE;
    }
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(slot));
    printf("[OTA ] Activating %s, rebooting\n", slot->label);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    network_shutdown();
    esp_restart();
    return POLL_DONE;
}

static void idle_forever(void)
{
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void ota_task(void *arg)
{
    const esp_app_desc_t *running = esp_app_get_description();

    network_start();
    printf("[OTA ] Checking %s for updates\n", SERVER_URL);

    for (;;) {
        poll_result_t r = poll_once(running);
        if (r == POLL_LATEST) {
            printf("[OTA ] Already on latest version\n");
            break;
        }
        if (r == POLL_DONE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
    idle_forever();
}

/* ---------- boot ---------- */

static bool load_trust_anchor(void)
{
    const esp_partition_t *keys = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, KEYS_SUBTYPE, "keys");
    if (keys == NULL) {
        return false;
    }
    static uint8_t rec[TA_HEADER_LEN + TA_MAX_KEY_LEN];
    if (esp_partition_read(keys, 0, rec, sizeof(rec)) != ESP_OK) {
        return false;
    }
    return ta_parse(rec, sizeof(rec), &s_ta) == 0;
}

void app_main(void)
{
    const esp_app_desc_t *running = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();

    printf("\n[BOOT] Firmware v%s running from %s\n", running->version, part->label);

    if (!load_trust_anchor()) {
        printf("[KEY ] No trust anchor provisioned, OTA disabled\n");
        return;
    }
    printf("[KEY ] Trust anchor: %s\n", ta_name(s_ta.type));

    /* ML-DSA-44 verification needs about 40 KB of stack. */
    xTaskCreate(ota_task, "ota", 64 * 1024, NULL, 5, NULL);
}
