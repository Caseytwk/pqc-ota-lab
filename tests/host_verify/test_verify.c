/* Host test for firmware/main/verify.c.
 * Usage: test_verify <keys/.provisioned> <firmware.bin> <firmware.sig>
 * Exit code 0 means the signature verified, exactly as the device would decide. */
#include <stdio.h>
#include <stdlib.h>
#include "verify.h"

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(*len ? *len : 1);
    if (fread(buf, 1, *len, f) != *len) { perror(path); exit(2); }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <record> <firmware.bin> <firmware.sig>\n", argv[0]);
        return 2;
    }
    size_t rec_len, fw_len, sig_len;
    unsigned char *rec = slurp(argv[1], &rec_len);
    unsigned char *fw = slurp(argv[2], &fw_len);
    unsigned char *sig = slurp(argv[3], &sig_len);
    trust_anchor_t ta;
    if (ta_parse(rec, rec_len, &ta) != 0) {
        printf("[KEY ] No trust anchor provisioned\n");
        return 2;
    }
    printf("[KEY ] Trust anchor: %s\n", ta_name(ta.type));
    printf("[SIG ] %s signature %zu bytes\n", ta_name(ta.type), sig_len);
    int rc = ta_verify(&ta, fw, fw_len, sig, sig_len);
    printf("[SIG ] Verify: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc == 0 ? 0 : 1;
}
