#include "verify.h"

#include <string.h>

#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "psa/crypto.h"

#include "api.h"  /* PQClean ML-DSA-44 */

/* SubjectPublicKeyInfo prefix for an uncompressed P-256 key (id-ecPublicKey, prime256v1). */
static const uint8_t P256_SPKI_PREFIX[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    0x04,
};

int ta_parse(const uint8_t *rec, size_t rec_len, trust_anchor_t *out)
{
    memset(out, 0, sizeof(*out));
    if (rec_len < TA_HEADER_LEN || memcmp(rec, TA_MAGIC, 4) != 0) {
        return -1;
    }
    uint8_t type = rec[4];
    uint16_t len = (uint16_t)(rec[5] | (rec[6] << 8));
    if (len > TA_MAX_KEY_LEN || (size_t)TA_HEADER_LEN + len > rec_len) {
        return -1;
    }
    if (type == TA_ECDSA_P256 && len != 64) {
        return -1;
    }
    if (type == TA_MLDSA44 && len != PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES) {
        return -1;
    }
    if (type != TA_ECDSA_P256 && type != TA_MLDSA44) {
        return -1;
    }
    out->type = (ta_type_t)type;
    out->key_len = len;
    memcpy(out->key, rec + TA_HEADER_LEN, len);
    return 0;
}

const char *ta_name(ta_type_t type)
{
    switch (type) {
    case TA_ECDSA_P256: return "ECDSA-P256";
    case TA_MLDSA44:    return "ML-DSA-44";
    default:            return "none";
    }
}

static int verify_ecdsa_p256(const trust_anchor_t *ta,
                             const uint8_t *msg, size_t msg_len,
                             const uint8_t *sig, size_t sig_len)
{
    uint8_t spki[sizeof(P256_SPKI_PREFIX) + 64];
    uint8_t hash[32];
    mbedtls_pk_context pk;
    int rc;

    memcpy(spki, P256_SPKI_PREFIX, sizeof(P256_SPKI_PREFIX));
    memcpy(spki + sizeof(P256_SPKI_PREFIX), ta->key, 64);

    if (mbedtls_sha256(msg, msg_len, hash, 0) != 0) {
        return -1;
    }

    psa_crypto_init();
    mbedtls_pk_init(&pk);
    rc = mbedtls_pk_parse_public_key(&pk, spki, sizeof(spki));
    if (rc == 0) {
        rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_len);
    }
    mbedtls_pk_free(&pk);
    return rc == 0 ? 0 : -1;
}

static int verify_mldsa44(const trust_anchor_t *ta,
                          const uint8_t *msg, size_t msg_len,
                          const uint8_t *sig, size_t sig_len)
{
    if (sig_len != PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES) {
        return -1;
    }
    /* Pure ML-DSA with an empty context string, the OpenSSL default. */
    return PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify_ctx(sig, sig_len, msg, msg_len,
                                                        NULL, 0, ta->key) == 0 ? 0 : -1;
}

int ta_verify(const trust_anchor_t *ta,
              const uint8_t *msg, size_t msg_len,
              const uint8_t *sig, size_t sig_len)
{
    if (sig_len == 0 || sig_len > TA_MAX_SIG_LEN) {
        return -1;
    }
    switch (ta->type) {
    case TA_ECDSA_P256: return verify_ecdsa_p256(ta, msg, msg_len, sig, sig_len);
    case TA_MLDSA44:    return verify_mldsa44(ta, msg, msg_len, sig, sig_len);
    default:            return -1;
    }
}
