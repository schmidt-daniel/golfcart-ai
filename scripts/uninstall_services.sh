#!/usr/bin/env bash
# =============================================================================
# uninstall_services.sh — Stop, disable, and remove the golf cart systemd units.
#
# Run this ON the Raspberry Pi.
#
# Usage:
#   ./scripts/uninstall_services.sh
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(dirname "$SCRIPT_DIR")"
SYSTEMD_DIR="$WORKSPACE/systemd"

if [[ ! -d "$SYSTEMD_DIR" ]]; then
  echo "ERROR: no systemd/ directory found at $SYSTEMD_DIR"
  exit 1
fi

echo "==> Stopping and disabling services"
for svc in "$SYSTEMD_DIR"/*.service; do
  name="$(basename "$svc")"
  sudo systemctl stop "$name" 2>/dev/null || true
  sudo systemctl disable "$name" 2>/dev/null || true
  echo "  stopped/disabled: $name"
done

echo "==> Removing unit files"
for svc in "$SYSTEMD_DIR"/*.service; do
  name="$(basename "$svc")"
  sudo rm -f "/etc/systemd/system/$name"
  echo "  removed: $name"
done

sudo systemctl daemon-reload
echo "==> Done."