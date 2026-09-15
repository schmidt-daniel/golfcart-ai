#!/usr/bin/env bash
# =============================================================================
# setup_eth_fallback_dhcp.sh — Ethernet DHCP fallback for the Raspberry Pi.
#
# The Pi's Wi-Fi is used only for the phone hotspot, so development/maintenance
# over SSH uses the Ethernet port. This script makes that robust:
#
#   - When an Ethernet cable is plugged in, the Pi first tries to get an IP
#     from an existing DHCP server on the network (a router, or a laptop
#     sharing its connection).
#   - If no DHCP server responds within a timeout, the Pi becomes a DHCP
#     server itself, so a directly-connected laptop gets an IP.
#   - When the cable is unplugged, the fallback DHCP server is stopped.
#
# How it works:
#   - Installs a NetworkManager dispatcher script
#     (/etc/NetworkManager/dispatcher.d/50-golfcart-eth-fallback) that reacts
#     to Ethernet interface events.
#   - On 'up': waits DHCP_TIMEOUT_S (default 15) for a DHCP-assigned address.
#     If none arrives, assigns a static IP and starts dnsmasq as a DHCP server.
#   - On 'dhcp4-change': a real DHCP lease was obtained -> stop the fallback.
#   - On 'down': cable unplugged -> stop the fallback server and remove the
#     static IP.
#
# Requires: dnsmasq (installed automatically if missing) and NetworkManager.
#
# Usage (run ON the Pi, with sudo):
#   ./scripts/setup_eth_fallback_dhcp.sh [--iface eth0] [--ip 192.168.50.1]
#       [--range 192.168.50.100,192.168.50.200,12h] [--timeout 15]
#   ./scripts/setup_eth_fallback_dhcp.sh --uninstall
# =============================================================================
set -euo pipefail

IFACE="eth0"
FALLBACK_IP="192.168.50.1/24"
DHCP_RANGE="192.168.50.100,192.168.50.200,12h"
DHCP_TIMEOUT_S=15
DISPATCHER="/etc/NetworkManager/dispatcher.d/50-golfcart-eth-fallback"
UNINSTALL=0

# ---- Parse args ----
while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface) IFACE="$2"; shift 2 ;;
    --ip) FALLBACK_IP="$2"; shift 2 ;;
    --range) DHCP_RANGE="$2"; shift 2 ;;
    --timeout) DHCP_TIMEOUT_S="$2"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help)
      echo "Usage: $0 [--iface eth0] [--ip 192.168.50.1] [--range RANGE] [--timeout 15] [--uninstall]"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ---- Preconditions ----
if [[ $EUID -ne 0 ]]; then
  echo "ERROR: run with sudo (needs to write $DISPATCHER and install dnsmasq)."
  exit 1
fi
if ! command -v nmcli >/dev/null 2>&1; then
  echo "ERROR: nmcli (NetworkManager) not found. This requires NetworkManager."
  exit 1
fi

# ---- Uninstall ----
if [[ $UNINSTALL -eq 1 ]]; then
  if [[ -f "$DISPATCHER" ]]; then
    rm -f "$DISPATCHER"
    echo "==> Removed $DISPATCHER"
  else
    echo "==> No dispatcher script installed."
  fi
  echo "==> Note: any running fallback dnsmasq is stopped on the next 'down' event."
  exit 0
fi

# ---- Ensure dnsmasq ----
if ! command -v dnsmasq >/dev/null 2>&1; then
  echo "==> Installing dnsmasq..."
  apt-get update -y
  apt-get install -y dnsmasq
fi

# ---- Write the dispatcher script ----
echo "==> Writing $DISPATCHER"
cat > "$DISPATCHER" <<EOF
#!/usr/bin/env bash
# Installed by setup_eth_fallback_dhcp.sh. Do not edit by hand.
IFACE="\$1"
ACTION="\$2"
PID_FILE="/run/golfcart-eth-dnsmasq-\$IFACE.pid"
FALLBACK_IP="$FALLBACK_IP"
DHCP_RANGE="$DHCP_RANGE"
DHCP_TIMEOUT_S=$DHCP_TIMEOUT_S

stop_fallback() {
  if [[ -f "\$PID_FILE" ]]; then
    kill "\$(cat "\$PID_FILE")" 2>/dev/null || true
    rm -f "\$PID_FILE"
  fi
  ip addr del "\$FALLBACK_IP" dev "\$IFACE" 2>/dev/null || true
}

case "\$ACTION" in
  up)
    # Give DHCP a chance; if no address arrives, become a DHCP server.
    (
      sleep "\$DHCP_TIMEOUT_S"
      if ! ip -4 addr show "\$IFACE" | grep -q "inet "; then
        ip addr add "\$FALLBACK_IP" dev "\$IFACE" 2>/dev/null || true
        dnsmasq --interface="\$IFACE" --dhcp-range="\$DHCP_RANGE" \\
                --pid-file="\$PID_FILE" 2>/dev/null || true
      fi
    ) &
    ;;
  dhcp4-change)
    # A real DHCP lease was obtained -> stop any fallback server.
    stop_fallback
    ;;
  down)
    # Cable unplugged -> stop the fallback server and remove the static IP.
    stop_fallback
    ;;
esac
EOF

chmod 755 "$DISPATCHER"
chown root:root "$DISPATCHER"

echo ""
echo "==> Ethernet DHCP fallback installed."
echo "    Interface: $IFACE"
echo "    Fallback IP: $FALLBACK_IP"
echo "    DHCP range: $DHCP_RANGE"
echo "    DHCP timeout: ${DHCP_TIMEOUT_S}s"
echo ""
echo "    Behavior:"
echo "      - Cable in + existing DHCP server -> Pi gets an IP from it."
echo "      - Cable in + no DHCP server -> Pi becomes a DHCP server after"
echo "        ${DHCP_TIMEOUT_S}s (laptop gets an IP in $FALLBACK_IP's subnet)."
echo "      - Cable out -> fallback DHCP server stopped."
echo ""
echo "    It takes effect on the next Ethernet up/down event (no reboot needed)."
echo "    To remove: sudo $0 --uninstall"