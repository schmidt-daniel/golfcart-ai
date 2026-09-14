#!/usr/bin/env bash
# Build the ESP32 handle-unit firmware.
#
# Two levels:
#   1. protocol: compile the pure-C protocol layer (handle_protocol.c) with
#      gcc — fast, no external deps, always runs.
#   2. full: build the complete firmware with PlatformIO (needs the ESP32
#      toolchain + LVGL/TFT_eSPI/HX711 libs, downloaded on first run).
#
# Usage:
#   ./build_firmware.sh [protocol|full]
#   (default: protocol)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
MODE="${1:-protocol}"

echo "==> Firmware build (mode=${MODE})"

# --- 1. Protocol layer (pure C, gcc) ---
echo "==> Compiling handle_protocol.c"
gcc -c "${SRC_DIR}/handle_protocol.c" -o /tmp/handle_protocol.o
echo "    OK"

if [[ "$MODE" == "full" ]]; then
  echo "==> Building full firmware with PlatformIO"
  if ! command -v pio >/dev/null 2>&1; then
    echo "ERROR: 'pio' not found. Install PlatformIO (see docker/Dockerfile)." >&2
    exit 1
  fi
  cd "${SCRIPT_DIR}"
  pio run -e esp32s3
  echo "==> Firmware build complete"
fi

echo "==> Done"