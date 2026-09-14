#!/usr/bin/env bash
# =============================================================================
# optimize_boot.sh — Reduce the Raspberry Pi's boot-to-ready time.
#
# The ESP32 handle unit shows a splash screen while the Pi boots, but a faster
# Pi means the user reaches the course screen sooner. This script applies
# safe, reversible boot optimizations:
#
#   1. systemd tuning (drop-in overrides — the tracked unit files stay clean)
#      - parallel service startup (drop strict After= chains where safe)
#      - remove duplicate ROS sourcing (ExecStartPre + ExecStart both source)
#      - faster crash recovery (RestartSec)
#      - shorter default stop timeout
#   2. Bootloader / kernel (config.txt + cmdline.txt)
#      - quiet boot, no splash, no overscan
#      - reduce kernel log verbosity
#   3. Disable unneeded services (bluetooth, avahi, etc.)
#   4. Boot-time measurement (systemd-analyze)
#
# Run this ON the Raspberry Pi (as the pi user; sudo is used internally).
#
# Usage:
#   ./scripts/optimize_boot.sh --apply      # apply optimizations
#   ./scripts/optimize_boot.sh --revert     # undo optimizations
#   ./scripts/optimize_boot.sh --dry-run    # show what would change (default)
#   ./scripts/optimize_boot.sh --measure    # report boot time (no changes)
#
# Examples:
#   ./scripts/optimize_boot.sh --dry-run
#   ./scripts/optimize_boot.sh --apply
#   ./scripts/optimize_boot.sh --measure
# =============================================================================
set -euo pipefail

# ---- Modes ----
MODE="dry-run"
case "${1:-}" in
  --apply)   MODE="apply" ;;
  --revert)  MODE="revert" ;;
  --dry-run) MODE="dry-run" ;;
  --measure) MODE="measure" ;;
  -h|--help)
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
    exit 0 ;;
  *)
    echo "Usage: $0 [--apply|--revert|--dry-run|--measure]"
    exit 1 ;;
esac

# ---- Paths ----
BOOT_DIR="/boot/firmware"          # Raspberry Pi OS Bookworm+ (config.txt here)
[[ -f "$BOOT_DIR/config.txt" ]] || BOOT_DIR="/boot"
CONFIG_TXT="$BOOT_DIR/config.txt"
CMDLINE_TXT="$BOOT_DIR/cmdline.txt"
BACKUP_DIR="/etc/golfcart-boot-backup"
SYSTEMD_DIR="/etc/systemd/system"

# Marker comment used to identify our edits (for --revert).
MARKER="golfcart-optimize-boot"

# ---- Helpers ----
say() { echo "==> $*"; }
warn() { echo "WARN: $*" >&2; }

# Run a command, honoring dry-run.
run() {
  if [[ "$MODE" == "dry-run" ]]; then
    echo "    [dry-run] $*"
  else
    "$@"
  fi
}

# ---- 0. Measure (no changes) ----
if [[ "$MODE" == "measure" ]]; then
  say "Boot time (systemd-analyze):"
  systemd-analyze 2>/dev/null || warn "systemd-analyze unavailable"
  echo
  say "Slowest units (top 10):"
  systemd-analyze blame 2>/dev/null | head -10 || true
  echo
  say "Critical chain:"
  systemd-analyze critical-chain 2>/dev/null | head -20 || true
  exit 0
fi

# ---- 1. Backup (once) ----
if [[ "$MODE" == "apply" ]]; then
  if [[ ! -d "$BACKUP_DIR" ]]; then
    say "Backing up boot config to $BACKUP_DIR"
    sudo mkdir -p "$BACKUP_DIR"
    sudo cp "$CONFIG_TXT" "$BACKUP_DIR/config.txt" 2>/dev/null || true
    sudo cp "$CMDLINE_TXT" "$BACKUP_DIR/cmdline.txt" 2>/dev/null || true
  else
    say "Backup already exists at $BACKUP_DIR (not overwriting)"
  fi
fi

# ---- 2. Bootloader / kernel ----
apply_bootloader() {
  say "Tuning bootloader ($CONFIG_TXT)"
  # config.txt: quiet boot, no splash, no overscan, faster.
  local cfg_add=(
    "disable_splash=1"
    "boot_splash=0"
    "disable_overscan=1"
    "avoid_warnings=1"
  )
  for line in "${cfg_add[@]}"; do
    if ! grep -q "^${line%%=*}" "$CONFIG_TXT" 2>/dev/null; then
      run sudo bash -c "echo '$line  # $MARKER' >> '$CONFIG_TXT'"
    fi
  done

  say "Tuning kernel cmdline ($CMDLINE_TXT)"
  # cmdline.txt: quiet, low log level, no splash.
  if ! grep -q "quiet" "$CMDLINE_TXT" 2>/dev/null; then
    run sudo sed -i 's/$/ quiet loglevel=3/' "$CMDLINE_TXT"
  fi
}

revert_bootloader() {
  say "Reverting bootloader ($CONFIG_TXT)"
  # Remove lines we added (marked).
  run sudo sed -i "/$MARKER/d" "$CONFIG_TXT"
  # Restore cmdline.txt from backup if present.
  if [[ -f "$BACKUP_DIR/cmdline.txt" ]]; then
    run sudo cp "$BACKUP_DIR/cmdline.txt" "$CMDLINE_TXT"
  fi
}

# ---- 3. systemd service drop-ins ----
# Each golfcart service gets a drop-in that:
#   - removes the duplicate ROS sourcing (ExecStartPre already sources; the
#     ExecStart re-sources). We keep ExecStartPre and drop the redundant
#     source from ExecStart by overriding it with a leaner command.
#   - reduces RestartSec for faster crash recovery
#   - removes strict After= ordering where the service is independent, so
#     systemd can start them in parallel.
#
# We write drop-ins rather than editing the tracked unit files so a re-deploy
# (deploy.sh) doesn't clobber the tuning.

# Map service -> (After= override, RestartSec).
# Services that only need the core pipeline up can keep After=core; the rest
# can start in parallel with core.
declare -A AFTER_OVERRIDE=(
  [golfcart-teleop.service]="After=network.target"
  [golfcart-localization.service]="After=network.target"
  [golfcart-mapping.service]="After=network.target golfcart-localization.service"
  [golfcart-navigation.service]="After=network.target golfcart-localization.service"
  [golfcart-follow.service]="After=network.target golfcart-localization.service"
  [golfcart-geofence.service]="After=network.target golfcart-localization.service"
  [golfcart-course-loader.service]="After=network.target"
)

apply_systemd() {
  say "Writing systemd drop-in overrides"
  for svc in "$SYSTEMD_DIR"/golfcart-*.service; do
    [[ -f "$svc" ]] || continue
    name="$(basename "$svc")"
    drop_dir="$SYSTEMD_DIR/$name.d"
    drop_file="$drop_dir/10-optimize-boot.conf"

    # Build the drop-in content.
    local content=""
    content+="# $MARKER\n"
    content+="[Service]\n"
    content+="RestartSec=2\n"                       # faster crash recovery
    content+="TimeoutStopSec=10\n"                  # don't hang on stop
    content+="OOMScoreAdjust=-500\n"                # keep services alive
    content+="Nice=-5\n"                            # slight priority boost

    # Override After= to allow parallel startup where safe.
    if [[ -n "${AFTER_OVERRIDE[$name]:-}" ]]; then
      content+="\n[Unit]\n"
      content+="${AFTER_OVERRIDE[$name]}\n"
    fi

    if [[ "$MODE" == "dry-run" ]]; then
      echo "    [dry-run] would write $drop_file:"
      echo -e "$content" | sed 's/^/      /'
    else
      sudo mkdir -p "$drop_dir"
      printf '%b' "$content" | sudo tee "$drop_file" >/dev/null
      echo "    wrote $drop_file"
    fi
  done

  # Remove the redundant ROS sourcing: ExecStartPre AND ExecStart both source
  # the ROS + install setup. ExecStart already sources, so we clear
  # ExecStartPre (a no-op) to avoid the duplicate shell startup cost. We do
  # this for the core service only (the highest-value one) to keep the change
  # minimal.
  local core_drop="$SYSTEMD_DIR/golfcart-core.service.d/10-optimize-boot.conf"
  local core_pre='ExecStartPre='
  if [[ "$MODE" == "dry-run" ]]; then
    echo "    [dry-run] would add to $core_drop:"
    echo "      $core_pre"
  else
    printf '\n%s\n' "$core_pre" | sudo tee -a "$core_drop" >/dev/null
    echo "    cleared ExecStartPre in $core_drop"
  fi

  run sudo systemctl daemon-reload
}

revert_systemd() {
  say "Removing systemd drop-in overrides"
  for svc in "$SYSTEMD_DIR"/golfcart-*.service; do
    [[ -f "$svc" ]] || continue
    name="$(basename "$svc")"
    drop_dir="$SYSTEMD_DIR/$name.d"
    if [[ -d "$drop_dir" ]]; then
      run sudo rm -rf "$drop_dir"
      echo "    removed $drop_dir"
    fi
  done
  run sudo systemctl daemon-reload
}

# ---- 4. Disable unneeded services ----
# Services that add boot time but aren't needed for the golf cart.
UNNEEDED_SERVICES=(
  bluetooth
  bluetooth-mesh
  avahi-daemon
  cups
  cups-browsed
  ModemManager
  triggerhappy
  wpa_supplicant
)

apply_disable() {
  say "Disabling unneeded services"
  for svc in "${UNNEEDED_SERVICES[@]}"; do
    if systemctl list-unit-files "$svc.service" >/dev/null 2>&1; then
      run sudo systemctl disable "$svc.service" 2>/dev/null || true
      run sudo systemctl mask "$svc.service" 2>/dev/null || true
    fi
  done
}

revert_disable() {
  say "Re-enabling services (best-effort)"
  for svc in "${UNNEEDED_SERVICES[@]}"; do
    run sudo systemctl unmask "$svc.service" 2>/dev/null || true
    run sudo systemctl enable "$svc.service" 2>/dev/null || true
  done
}

# ---- 5. Network: don't let the hotspot block boot ----
# The hotspot service waits for network-online.target, which can add seconds.
# Make it not block the boot (it can come up whenever the network is ready).
apply_network() {
  local hs="$SYSTEMD_DIR/golfcart-hotspot.service.d/10-optimize-boot.conf"
  local content="# $MARKER\n[Unit]\nAfter=network.target\nWants=network.target\n"
  if [[ "$MODE" == "dry-run" ]]; then
    echo "    [dry-run] would write $hs:"
    echo -e "$content" | sed 's/^/      /'
  else
    sudo mkdir -p "$(dirname "$hs")"
    printf '%b' "$content" | sudo tee "$hs" >/dev/null
    echo "    wrote $hs"
  fi
}

# ---- Main ----
case "$MODE" in
  apply)
    say "Applying boot optimizations"
    apply_bootloader
    apply_systemd
    apply_disable
    apply_network
    say "Done. Reboot to take effect: sudo reboot"
    say "Measure after reboot: $0 --measure"
    ;;
  revert)
    say "Reverting boot optimizations"
    revert_bootloader
    revert_systemd
    revert_disable
    say "Done. Reboot to take effect: sudo reboot"
    ;;
  dry-run)
    say "Dry run — showing what would change (no changes made)"
    apply_bootloader
    apply_systemd
    apply_disable
    apply_network
    say "Dry run complete. Run with --apply to make changes."
    ;;
esac