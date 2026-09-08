#!/usr/bin/env bash
# =============================================================================
# setup_hotspot.sh — Configure the Raspberry Pi as a local Wi-Fi hotspot.
#
# The Pi operates a local-only Wi-Fi access point (no internet) that the
# operator's smartphone connects to directly for web teleoperation and summon.
# The phone loads the web page on port 8080 and connects to rosbridge_server
# over WebSocket on port 9090.
#
# This script configures the hotspot using NetworkManager (the default on
# Raspberry Pi OS Bookworm+). It creates a dedicated Wi-Fi connection profile
# for the access point.
#
# Usage (run ON the Pi):
#   ./scripts/setup_hotspot.sh [--ssid <name>] [--password <pass>] [--channel <n>]
#
# Examples:
#   ./scripts/setup_hotspot.sh
#   ./scripts/setup_hotspot.sh --ssid GolfCart --password golfcart123
#
# Notes:
#   - Requires NetworkManager (nmcli). Raspberry Pi OS Bookworm ships with it.
#   - The hotspot is local-only (no internet bridging). This keeps trolley
#     control independent of golf-course internet/mobile coverage.
#   - Wi-Fi security and device-pairing are intentionally simple (WPA2-PSK);
#     revisit for production hardening.
# =============================================================================
set -euo pipefail

# ---- Defaults ----
SSID="GolfCart"
PASSWORD="golfcart123"
CHANNEL=6
WIFI_IFACE="wlan0"

# ---- Parse args ----
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ssid) SSID="$2"; shift 2 ;;
    --password) PASSWORD="$2"; shift 2 ;;
    --channel) CHANNEL="$2"; shift 2 ;;
    --iface) WIFI_IFACE="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 [--ssid <name>] [--password <pass>] [--channel <n>] [--iface <wlan>]"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ---- Preconditions ----
if ! command -v nmcli >/dev/null 2>&1; then
  echo "ERROR: nmcli (NetworkManager) not found. This script requires NetworkManager."
  exit 1
fi

if [[ ${#PASSWORD} -lt 8 ]]; then
  echo "ERROR: hotspot password must be at least 8 characters."
  exit 1
fi

echo "==> Configuring Wi-Fi hotspot"
echo "    SSID:     $SSID"
echo "    Channel:  $CHANNEL"
echo "    Interface: $WIFI_IFACE"

# ---- Create the hotspot connection profile ----
# Remove any existing profile with the same name to make this idempotent.
CONN_NAME="golfcart-hotspot"
if nmcli connection show "$CONN_NAME" >/dev/null 2>&1; then
  echo "==> Removing existing profile '$CONN_NAME'"
  nmcli connection delete "$CONN_NAME"
fi

echo "==> Creating hotspot profile '$CONN_NAME'"
nmcli connection add \
  type wifi \
  ifname "$WIFI_IFACE" \
  con-name "$CONN_NAME" \
  ssid "$SSID" \
  wifi-sec.key-mgmt wpa-psk \
  wifi-sec.psk "$PASSWORD" \
  ipv4.method shared \
  ipv6.method disabled

# Set the channel (0 = auto).
nmcli connection modify "$CONN_NAME" wifi.channel "$CHANNEL"

# ---- Activate the hotspot ----
echo "==> Activating hotspot (this may briefly drop Wi-Fi)"
nmcli connection up "$CONN_NAME"

echo ""
echo "==> Hotspot active."
echo "    SSID:     $SSID"
echo "    Password: $PASSWORD"
echo "    Phone connects to this network, then opens:"
echo "      http://<pi-ip>:8080/summon.html   (summon page)"
echo "      http://<pi-ip>:8080/              (web teleop)"
echo ""
echo "    The Pi's IP on the hotspot is typically 10.42.0.1 (NetworkManager 'shared')."
echo "    Verify with: ip -4 addr show $WIFI_IFACE"
echo ""
echo "    To make the hotspot start on boot:"
echo "      sudo nmcli connection modify '$CONN_NAME' connection.autoconnect yes"
