#!/usr/bin/env bash
# Put the device back to factory firmware v1.0.0, keeping the last provisioned trust anchor.
set -euo pipefail
cd "$(dirname "$0")"
if pgrep -f qemu-system-xtensa > /dev/null; then
  echo "The device is still running. Stop it first with Ctrl+A then X." >&2
  exit 1
fi
cp device/factory.bin device/flash_image.bin
echo "Device reset to factory firmware v1.0.0"
if [ -f keys/.provisioned ]; then
  python3 tools/provision.py --record keys/.provisioned
else
  echo "No trust anchor provisioned"
fi
