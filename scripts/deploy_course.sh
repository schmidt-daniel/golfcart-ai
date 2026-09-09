#!/usr/bin/env bash
# =============================================================================
# deploy_course.sh — Deploy an exported course Zip (from the map editor) to the
# cart and wire it into the runtime:
#
#   1. Copies the Zip to the cart's maps/ dir.
#   2. Unpacks each holes/holeN.yaml into golfcart_geofence/config/ so the
#      existing geofence_node picks up the hole boundaries.
#   3. (Optional) writes a systemd drop-in / launch arg so course_loader_node
#      publishes the CourseMap on /course/map.
#
# Usage:
#   ./scripts/deploy_course.sh --pi <user@host> --zip <course.zip> [--remote-dir <name>]
#
# Examples:
#   ./scripts/deploy_course.sh --pi pi@192.168.1.50 --zip golfcart-red-123-20260909.zip
#   ./scripts/deploy_course.sh --pi pi@192.168.1.50 --zip ./maps/golfcart-*.zip
#
# Requirements:
#   - SSH access to the Pi (passwordless key recommended).
#   - The Pi has ROS 2 Lyrical installed and the workspace at ~/golfcart-ai.
# =============================================================================
set -euo pipefail

PI_USER_HOST=""
ZIP_FILE=""
REMOTE_DIR="golfcart-ai"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pi) PI_USER_HOST="$2"; shift 2 ;;
    --zip) ZIP_FILE="$2"; shift 2 ;;
    --remote-dir) REMOTE_DIR="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 --pi <user@host> --zip <course.zip> [--remote-dir <name>]"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

if [[ -z "$PI_USER_HOST" || -z "$ZIP_FILE" ]]; then
  echo "ERROR: --pi <user@host> and --zip <course.zip> are required."
  exit 1
fi
if [[ ! -f "$ZIP_FILE" ]]; then
  echo "ERROR: Zip not found: $ZIP_FILE"
  exit 1
fi

REMOTE_HOME=$(ssh "$PI_USER_HOST" 'echo $HOME' 2>/dev/null || true)
if [[ -z "$REMOTE_HOME" ]]; then
  echo "ERROR: cannot reach $PI_USER_HOST via SSH."
  exit 1
fi
REMOTE_WORKSPACE="$REMOTE_HOME/$REMOTE_DIR"
ZIP_NAME="$(basename "$ZIP_FILE")"

echo "==> Deploying course '$ZIP_NAME' to $PI_USER_HOST"

# ---- 1. Copy the Zip to the cart's maps/ dir ----
echo "==> Copying Zip to maps/ ..."
ssh "$PI_USER_HOST" "mkdir -p $REMOTE_WORKSPACE/maps"
rsync -avz "$ZIP_FILE" "$PI_USER_HOST:$REMOTE_WORKSPACE/maps/"

# ---- 2. Unpack hole configs + course.yaml into the geofence config dir ----
echo "==> Unpacking hole configs into golfcart_geofence/config/ ..."
ssh "$PI_USER_HOST" "cd $REMOTE_WORKSPACE && \
  mkdir -p /tmp/course_unpack && \
  rm -rf /tmp/course_unpack/* && \
  unzip -o maps/$ZIP_NAME -d /tmp/course_unpack && \
  cp /tmp/course_unpack/holes/hole*.yaml src/golfcart_geofence/config/ && \
  cp /tmp/course_unpack/course.yaml src/golfcart_geofence/config/course.yaml && \
  echo '  Installed hole configs:' && ls src/golfcart_geofence/config/hole*.yaml && \
  echo '  Installed course.yaml:' && ls src/golfcart_geofence/config/course.yaml"

# ---- 3. Wire course_loader_node to publish the CourseMap ----
# Install the course-loader systemd unit (if not already present) and write a
# drop-in so it starts with the right course_zip.
echo "==> Wiring course_loader_node ..."
ssh "$PI_USER_HOST" "cd $REMOTE_WORKSPACE && \
  sudo cp systemd/golfcart-course-loader.service /etc/systemd/system/ && \
  mkdir -p /etc/systemd/system/golfcart-course-loader.service.d && \
  printf '[Service]\nEnvironment=COURSE_ZIP=%s/maps/%s\n' '$REMOTE_WORKSPACE' '$ZIP_NAME' \
    | sudo tee /etc/systemd/system/golfcart-course-loader.service.d/10-course.conf >/dev/null && \
  sudo systemctl daemon-reload && \
  sudo systemctl enable golfcart-course-loader.service && \
  echo '  course_loader unit + drop-in installed (restart golfcart-course-loader to apply)'"

echo "==> Done."
echo "  - Geofence configs: $REMOTE_WORKSPACE/src/golfcart_geofence/config/hole*.yaml"
echo "  - Course Zip:       $REMOTE_WORKSPACE/maps/$ZIP_NAME"
echo "  - Restart geofence: ssh $PI_USER_HOST 'sudo systemctl restart golfcart-geofence'"
echo "  - Restart loader:   ssh $PI_USER_HOST 'sudo systemctl restart golfcart-course-loader'"