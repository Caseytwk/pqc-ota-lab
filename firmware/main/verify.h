/*
 * Trust anchor record and signature verification for the PQC OTA lab.
 *
 * The same file is compiled into the ESP32 firmware and into the host test
 * in tests/host_verify, so the device logic is tested against real OpenSSL
 * signatures before it ever runs on hardware.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Key partition record: "PQTA" | type (1) | key length (2, little endian) | raw key */
#define TA_MAGIC        "PQTA"
#define TA_HEADER_LEN   7
#define TA_MAX_KEY_LEN  1312   /* ML-DSA-44 public key */
#define TA_MAX_SIG_LEN  4096

typedef enum {
    TA_NONE        = 0,
    TA_ECDSA_P256  = 1,  /* raw key: X || Y, 64 bytes. Signature: DER ECDSA over SHA-256 */
    TA_MLDSA44     = 2,  /* raw key: 1312 bytes. Signature: 2420 bytes, pure ML-DSA, empty context */
} ta_type_t;

typedef struct {
    ta_type_t type;
    uint16_t  key_len;
    uint8_t   key[TA_MAX_KEY_LEN];
} trust_anchor_t;

/* Parse a key partition record. Returns 0 on success, -1 if empty or invalid. */
int ta_parse(const uint8_t *rec, size_t rec_len, trust_anchor_t *out);

/* Human readable algorithm name, matching the workshop notes. */
const char *ta_name(ta_type_t type);

/* Verify sig over msg. Returns 0 when the signature is valid. */
int ta_verify(const trust_anchor_t *ta,
              const uint8_t *msg, size_t msg_len,
              const uint8_t *sig, size_t sig_len);

#ifdef __cplusplus
}
#endif
