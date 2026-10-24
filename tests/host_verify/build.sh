#!/usr/bin/env bash
# Build the host verifier: firmware/main/verify.c + vendored PQClean + mbedTLS 3.6.
# Usage: tests/host_verify/build.sh <path to mbedtls source checkout>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MBED="$(cd "$1" && pwd)"
OUT="$HERE/build"
mkdir -p "$OUT"
if [ ! -f "$OUT/mbedtls/library/libmbedcrypto.a" ]; then
  cmake -S "$MBED" -B "$OUT/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release > /dev/null
  cmake --build "$OUT/mbedtls" -j"$(nproc)" > /dev/null
fi
PQ="$ROOT/firmware/components/pqclean_mldsa44"
cat > "$OUT/randombytes_host.c" <<'C'
#include "randombytes.h"
#include <stdio.h>
int randombytes(uint8_t *out, size_t n) { FILE *f = fopen("/dev/urandom", "rb"); size_t r = fread(out, 1, n, f); fclose(f); return r == n ? 0 : -1; }
C
cc -O2 -Wall -Wextra -o "$OUT/test_verify" \
  "$HERE/test_verify.c" "$ROOT/firmware/main/verify.c" \
  "$PQ"/clean/*.c "$PQ/common/fips202.c" "$OUT/randombytes_host.c" \
  -I"$ROOT/firmware/main" -I"$PQ/clean" -I"$PQ/common" -I"$MBED/include" \
  "$OUT/mbedtls/library/libmbedcrypto.a"
echo "Built $OUT/test_verify"
