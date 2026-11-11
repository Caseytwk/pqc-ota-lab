#!/usr/bin/env bash
# Print your filled in answer sheet from your keys and the saved device log.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 tools/results.py "$@"
