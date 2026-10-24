#!/usr/bin/env bash
# Boot the emulated ESP32. Stop it with Ctrl+A then X.
set -euo pipefail
cd "$(dirname "$0")"
if pgrep -f qemu-system-xtensa > /dev/null; then
  echo "A device is already running. Stop it first, or run: pkill -f qemu-system-xtensa" >&2
  exit 1
fi
[ -f device/flash_image.bin ] || cp device/factory.bin device/flash_image.bin
echo "Starting ESP32 emulator. Stop it with Ctrl+A then X."
exec qemu-system-xtensa -nographic -M esp32 -m 4M \
  -drive file=device/flash_image.bin,if=mtd,format=raw \
  -drive file=device/qemu_efuse.bin,if=none,format=raw,id=efuse \
  -global driver=nvram.esp32.efuse,property=drive,value=efuse \
  -global driver=timer.esp32.timg,property=wdt_disable,value=true \
  -nic user,model=open_eth \
  -serial mon:stdio
