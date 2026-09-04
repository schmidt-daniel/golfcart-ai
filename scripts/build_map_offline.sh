#!/usr/bin/env bash
# =============================================================================
# build_map_offline.sh — Rebuild a course map off-board from a recorded bag.
#
# Replays a ROS 2 bag (captured with record_bag.sh) through slam_toolbox to
# produce an occupancy grid, then saves it. This runs on a workstation (not
# the cart), then the resulting map is pushed back to the cart via rsync.
#
# Usage:
#   ./scripts/build_map_offline.sh <bag_dir> [--out <map_prefix>]
#
#   <bag_dir>     path to the recorded bag (e.g. rosbag2/my_course)
#   --out <p>     output map prefix (default: <bag_dir>/map)
#
# Requires a ROS 2 Lyrical environment sourced (slam_toolbox, nav2_map_server,
# rosbag2, this workspace install).
#
# Example:
#   # record on the cart
#   ./scripts/record_bag.sh -o hole5  -- (on cart)
#   # copy to workstation
#   rsync -avz pi@cart:~/golfcart-ai/rosbag2/my_course ./
#   # build map off-board
#   ./scripts/build_map_offline.sh rosbag2/my_course
#   # push map back
#   rsync -avz rosbag2/my_course/map* pi@cart:~/golfcart-ai/maps/my_course/
# =============================================================================
set -euo pipefail

BAG_DIR=""
OUT_PREFIX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT_PREFIX="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 <bag_dir> [--out <map_prefix>]"
      echo "  <bag_dir>     recorded bag directory (metadata.yaml + data files)"
      echo "  --out <prefix> map output prefix (default: <bag_dir>/map)"
      exit 0 ;;
    *) BAG_DIR="$1"; shift ;;
  esac
done

if [[ -z "$BAG_DIR" || ! -f "$BAG_DIR/metadata.yaml" ]]; then
  echo "ERROR: provide a valid bag directory containing metadata.yaml (got '$BAG_DIR')"
  exit 1
fi

if [[ -z "$OUT_PREFIX" ]]; then
  OUT_PREFIX="$(basename "$BAG_DIR")_map"
fi

echo "==> Off-board map build from: $BAG_DIR"
echo "==> Output prefix:           $OUT_PREFIX"

# Play the bag (sim clock + needed topics) in the background.
echo "==> Playing bag (background)..."
ros2 bag play "$BAG_DIR" --clock \
  --topics /scan /tf /tf_static \
  > /tmp/map_play.log 2>&1 &
PLAY_PID=$!

# Start slam_toolbox in the background.
echo "==> Starting slam_toolbox..."
ros2 launch golfcart_mapping offline_mapping.launch.py \
  > /tmp/map_slam.log 2>&1 &
SLAM_PID=$!

# Wait for the bag to finish.
echo "==> Waiting for bag playback to finish..."
wait "$PLAY_PID" || echo "WARN: bag play ended with non-zero code"
sleep 2   # let slam_toolbox finish processing the last scans

# Save the map.
echo "==> Saving map to $OUT_PREFIX..."
map_saver_cli -t /map -f "$OUT_PREFIX"

# Stop slam_toolbox.
echo "==> Shutting down slam_toolbox..."
kill "$SLAM_PID" 2>/dev/null || true

echo "==> Done. Outputs: ${OUT_PREFIX}.pgm, ${OUT_PREFIX}.yaml"
echo "    Push to the cart with: rsync -avz ${OUT_PREFIX}.* <user>@<host>:<workspace>/maps/..."