#!/usr/bin/env bash
# Build lab firmware v1.0.0 (factory image) and v2.0.0 (the update payload).
# Needs ESP-IDF v5.5 with idf.py on PATH. CI runs this in the espressif/idf:v5.5.6 container.
set -euo pipefail
FW="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$FW")"
cd "$FW"

idf.py -B build_v1 -DFW_VERSION=1.0.0 build
idf.py -B build_v2 -DFW_VERSION=2.0.0 build

# Factory flash image: bootloader, partition table and v1.0.0 in ota_0, keys partition blank
(cd build_v1 && python -m esptool --chip esp32 merge_bin \
    --output "$ROOT/device/factory.bin" --fill-flash-size 4MB @flash_args)
cp "$ROOT/device/factory.bin" "$ROOT/device/flash_image.bin"

# Update payload: the v2.0.0 app binary, unsigned
mkdir -p "$ROOT/updates"
cp build_v2/pqc_ota_lab.bin "$ROOT/updates/firmware_v2.bin"

ls -l "$ROOT/device/factory.bin" "$ROOT/updates/firmware_v2.bin"
