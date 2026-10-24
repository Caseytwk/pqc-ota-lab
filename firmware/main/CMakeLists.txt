idf_component_register(
    SRCS "main.c" "verify.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_eth esp_netif esp_event esp_http_client app_update
                  esp_partition esp_timer mbedtls bootloader_support pqclean_mldsa44
)
