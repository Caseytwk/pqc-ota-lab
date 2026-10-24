#!/usr/bin/env bash
# End to end test of the workshop lab in QEMU, following the Notion lab pages step by step.
# Run inside the lab environment (Codespace or the .devcontainer image) after firmware is built.
set -euo pipefail
cd "$(dirname "$0")/.."
chmod +x ./*.sh tools/*.py
OUT="tests/out"
mkdir -p "$OUT"
LOG="$OUT/current_boot.log"
BOOT=0
SUMMARY=()

die() { echo "FAIL: $1"; echo "----- device log -----"; cat "$LOG"; exit 1; }
ok()  { echo "PASS: $1"; }

boot_until() {   # boot_until <text to wait for> <timeout seconds>
  : > "$LOG"
  ./run_device.sh < /dev/null > "$LOG" 2>&1 &
  local pid=$! waited=0
  until grep -q -- "$1" "$LOG"; do
    sleep 1
    waited=$((waited + 1))
    if ! kill -0 "$pid" 2>/dev/null; then die "device exited before: $1"; fi
    if [ "$waited" -ge "$2" ]; then cp "$LOG" "$OUT/timeout_boot.log"; kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; die "timed out waiting for: $1"; fi
  done
  sleep 1
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  BOOT=$((BOOT + 1))
  cp "$LOG" "$OUT/boot_${BOOT}.log"
}
expect() { grep -q -- "$1" "$LOG" || die "missing line: $1"; }

flip_byte() {
python3 - <<'PY'
d = bytearray(open("ota/firmware.bin", "rb").read())
d[4096] ^= 0xFF
open("ota/firmware.bin", "wb").write(d)
PY
}

cleanup() { pkill -f "http.server 8070" 2>/dev/null || true; pkill -f qemu-system-xtensa 2>/dev/null || true; }
trap cleanup EXIT

rm -f keys/*.pem keys/.provisioned ota/firmware.bin ota/firmware.sig
./reset_device.sh

echo "== Smoke test (prerequisites page)"
boot_until "No trust anchor provisioned" 120
expect "Firmware v1.0.0 running from ota_0"
ok "factory device boots without a trust anchor"

python3 -m http.server 8070 --directory ota > server.log 2>&1 &
sleep 1

run_scheme() {   # run_scheme <label>, keys already provisioned, ota/firmware.* already signed
  local label="$1"
  ./reset_device.sh
  boot_until "Already on latest version" 600
  expect "Trust anchor: $label"
  expect "Found firmware v2.0.0, downloading"
  expect "Verify: PASS"
  expect "Firmware v2.0.0 running from ota_1"
  ok "$label signed update installed"
  SUMMARY+=("$label: $(grep 'signature .* bytes' "$LOG" | head -n 1 | sed 's/.*\] //') | $(grep 'Verify: PASS' "$LOG" | sed 's/.*Verify: //')")

  flip_byte
  ./reset_device.sh
  boot_until "staying on v1.0.0" 600
  expect "Verify: FAIL"
  ok "$label tampered update rejected"
  cp updates/firmware_v2.bin ota/firmware.bin
}

echo "== Lab Part 1: ECDSA P-256"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out keys/ecdsa_priv.pem
openssl pkey -in keys/ecdsa_priv.pem -pubout -out keys/ecdsa_pub.pem
./provision.sh keys/ecdsa_pub.pem
cp updates/firmware_v2.bin ota/firmware.bin
openssl dgst -sha256 -sign keys/ecdsa_priv.pem -out ota/firmware.sig ota/firmware.bin
openssl dgst -sha256 -verify keys/ecdsa_pub.pem -signature ota/firmware.sig ota/firmware.bin
run_scheme "ECDSA-P256"

echo "== Lab Part 2: ML-DSA-44"
./reset_device.sh
openssl genpkey -algorithm ML-DSA-44 -out keys/mldsa_priv.pem
openssl pkey -in keys/mldsa_priv.pem -pubout -out keys/mldsa_pub.pem
./provision.sh keys/mldsa_pub.pem
cp updates/firmware_v2.bin ota/firmware.bin
openssl pkeyutl -sign -rawin -inkey keys/mldsa_priv.pem -in ota/firmware.bin -out ota/firmware.sig
openssl pkeyutl -verify -rawin -pubin -inkey keys/mldsa_pub.pem -in ota/firmware.bin -sigfile ota/firmware.sig
run_scheme "ML-DSA-44"

echo
echo "== All lab steps passed"
printf '%s\n' "${SUMMARY[@]}"
rm -f keys/*.pem keys/.provisioned ota/firmware.bin ota/firmware.sig server.log
cp device/factory.bin device/flash_image.bin
