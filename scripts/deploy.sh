#!/usr/bin/env bash
# =============================================================================
# deploy.sh — Deploy the golf cart software to the Raspberry Pi (Option D, Hybrid)
#
# Strategy:
#   - Code is versioned in git and deployed via rsync (simple, rollback = git).
#   - Maps/config are large binary data kept OUT of git; they are rsynced from
#     a local maps/ directory when present.
#   - systemd units are installed/updated and services restarted.
#
# Usage:
#   ./scripts/deploy.sh --pi <user@host> [--map-dir <path>] [--no-build]
#
# Examples:
#   ./scripts/deploy.sh --pi pi@192.168.1.50
#   ./scripts/deploy.sh --pi pi@192.168.1.50 --map-dir ./maps
#   ./scripts/deploy.sh --pi pi@192.168.1.50 --no-build   # skip Docker build
#
# Requirements:
#   - Docker available on this machine (for the build step).
#   - SSH access to the Pi (passwordless key recommended).
#   - The Pi has ROS 2 Lyrical installed and the workspace at ~/golfcart-ai.
# =============================================================================
set -euo pipefail

# ---- Defaults ----
PI_USER_HOST=""
MAP_DIR=""
DO_BUILD=1
REMOTE_DIR="golfcart-ai"
REMOTE_HOME=""
SYSTEMD_DIR="systemd"

# ---- Parse args ----
while [[ $# -gt 0 ]]; do
  case "$1" in
    --pi) PI_USER_HOST="$2"; shift 2 ;;
    --map-dir) MAP_DIR="$2"; shift 2 ;;
    --no-build) DO_BUILD=0; shift ;;
    --remote-dir) REMOTE_DIR="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 --pi <user@host> [--map-dir <path>] [--no-build] [--remote-dir <name>]"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

if [[ -z "$PI_USER_HOST" ]]; then
  echo "ERROR: --pi <user@host> is required (e.g. pi@192.168.1.50)"
  exit 1
fi

# ---- 1. Build in Docker (optional) ----
if [[ "$DO_BUILD" == "1" ]]; then
  echo "==> Building workspace in Docker..."
  if command -v docker >/dev/null 2>&1; then
    docker run --rm -v "$(pwd)":/workspace -w /workspace golfcart:lyrical \
      bash -c 'source /opt/ros/${ROS_DISTRO}/setup.bash && colcon build'
  else
    echo "WARN: docker not found; skipping build (use --no-build if intended)."
  fi
else
  echo "==> Skipping Docker build (--no-build)."
fi

# ---- 2. Determine remote paths ----
# Resolve the remote home directory.
REMOTE_HOME=$(ssh "$PI_USER_HOST" 'echo $HOME' 2>/dev/null || true)
if [[ -z "$REMOTE_HOME" ]]; then
  echo "ERROR: cannot reach $PI_USER_HOST via SSH."
  exit 1
fi
REMOTE_WORKSPACE="$REMOTE_HOME/$REMOTE_DIR"

echo "==> Deploying to $PI_USER_HOST:$REMOTE_WORKSPACE"

# ---- 3. rsync code (git-tracked source) ----
# Exclude build artifacts, install, logs, and the large core dump.
echo "==> Syncing code..."
rsync -avz --delete \
  --exclude 'build/' \
  --exclude 'install/' \
  --exclude 'log/' \
  --exclude 'core' \
  --exclude 'core.*' \
  --exclude '.git/' \
  --exclude 'maps/' \
  ./ "$PI_USER_HOST:$REMOTE_WORKSPACE/"

# ---- 4. rsync maps/config (large binary data, kept out of git) ----
if [[ -n "$MAP_DIR" && -d "$MAP_DIR" ]]; then
  echo "==> Syncing maps from $MAP_DIR..."
  ssh "$PI_USER_HOST" "mkdir -p $REMOTE_WORKSPACE/maps"
  rsync -avz "$MAP_DIR/" "$PI_USER_HOST:$REMOTE_WORKSPACE/maps/"
else
  echo "==> No --map-dir provided; skipping map sync."
fi

# ---- 5. Build on the Pi (if not already built) ----
echo "==> Building on the Pi..."
ssh "$PI_USER_HOST" "cd $REMOTE_WORKSPACE && source /opt/ros/\${ROS_DISTRO}/setup.bash && colcon build"

# ---- 6. Install/update systemd units ----
echo "==> Installing systemd units..."
ssh "$PI_USER_HOST" "cd $REMOTE_WORKSPACE && sudo cp $SYSTEMD_DIR/*.service /etc/systemd/system/ && sudo systemctl daemon-reload"

# ---- 7. Enable + restart services ----
echo "==> Enabling and restarting services..."
ssh "$PI_USER_HOST" "cd $REMOTE_WORKSPACE && for svc in $SYSTEMD_DIR/*.service; do \
  name=\$(basename \$svc); \
  sudo systemctl enable \$name; \
  sudo systemctl restart \$name; \
done"

echo "==> Done. Check status with: ssh $PI_USER_HOST 'systemctl --no-pager status golfcart-*'"
