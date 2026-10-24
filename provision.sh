#!/usr/bin/env bash
# Write your public key into the device's trust anchor (key partition).
# Usage: ./provision.sh keys/ecdsa_pub.pem
set -euo pipefail
cd "$(dirname "$0")"
exec python3 tools/provision.py "$@"
