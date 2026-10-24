# ML-DSA-44 (FIPS 204) from PQClean, "clean" portable C implementation.
# Upstream commit recorded in PQCLEAN_COMMIT. Licence: public domain (see LICENSE).
idf_component_register(
    SRCS "clean/ntt.c" "clean/packing.c" "clean/poly.c" "clean/polyvec.c"
         "clean/reduce.c" "clean/rounding.c" "clean/sign.c" "clean/symmetric-shake.c"
         "common/fips202.c" "port/randombytes_esp.c"
    INCLUDE_DIRS "clean" "common"
    PRIV_REQUIRES esp_hw_support
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-error)
