#!/usr/bin/env bash
# Build the offline fallback image and save it as pqc-ota-lab-<arch>.tar for USB distribution.
# Run from the repo root after the firmware binaries exist. Usage: fallback/build_offline_image.sh [amd64|arm64]
set -euo pipefail
cd "$(dirname "$0")/.."
ARCH="${1:-amd64}"
docker buildx build --platform "linux/${ARCH}" --load -t pqc-ota-lab-env .devcontainer
docker buildx build --platform "linux/${ARCH}" --load -t pqc-ota-lab:latest -f fallback/Dockerfile .
docker save -o "pqc-ota-lab-${ARCH}.tar" pqc-ota-lab:latest
ls -lh "pqc-ota-lab-${ARCH}.tar"
echo "Attendees: docker load -i pqc-ota-lab-${ARCH}.tar && docker run -it --name pqc-lab pqc-ota-lab:latest"
