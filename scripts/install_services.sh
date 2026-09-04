#!/usr/bin/env bash
# =============================================================================
# install_services.sh — Install and enable the golf cart systemd services.
#
# Run this ON the Raspberry Pi (or via deploy.sh). Copies the unit files from
# the workspace systemd/ directory into /etc/systemd/system, reloads systemd,
# and enables the services to start on boot.
#
# Usage:
#   ./scripts/install_services.sh [--start]
#     --start   also start the services now (default: enable only)
# =============================================================================
set -euo pipefail

START=0
if [[ "${1:-}" == "--start" ]]; then
  START=1
fi

# Resolve the workspace root (parent of scripts/).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(dirname "$SCRIPT_DIR")"
SYSTEMD_DIR="$WORKSPACE/systemd"

if [[ ! -d "$SYSTEMD_DIR" ]]; then
  echo "ERROR: no systemd/ directory found at $SYSTEMD_DIR"
  exit 1
fi

echo "==> Installing systemd units from $SYSTEMD_DIR"
sudo cp "$SYSTEMD_DIR"/*.service /etc/systemd/system/
sudo systemctl daemon-reload

echo "==> Enabling services (start on boot)"
for svc in "$SYSTEMD_DIR"/*.service; do
  name="$(basename "$svc")"
  sudo systemctl enable "$name"
  echo "  enabled: $name"
done

if [[ "$START" == "1" ]]; then
  echo "==> Starting services"
  for svc in "$SYSTEMD_DIR"/*.service; do
    name="$(basename "$svc")"
    sudo systemctl restart "$name"
    echo "  started: $name"
  done
fi

echo "==> Done."
echo "    Status:  systemctl --no-pager status 'golfcart-*'"
echo "    Logs:    journalctl -u golfcart-core -f"