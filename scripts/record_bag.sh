#!/usr/bin/env bash
# =============================================================================
# record_bag.sh — Record a ROS 2 bag on the cart for offline map building.
#
# Record the topics that slam_toolbox + localization need to rebuild a course
# map off-board:
#   - /scan       (LiDAR)        — primary SLAM input
#   - /tf /tf_static             — odom->base_link (and map frames)
#   - /odom OR /wheel/odometry   — wheel odometry
#   - /imu/data, /gps/fix        — optional, for debugging / refinement
#
# Usage (on the cart):
#   ./scripts/record_bag.sh                 # records to rosbag2/<auto-name>
#   ./scripts/record_bag.sh -o my_course    # records to rosbag2/my_course
#   ./scripts/record_bag.sh --all           # also record imu/gps
#
# Stop with Ctrl+C.
# =============================================================================
set -euo pipefail

OUT=""
INCLUDE_IMU_GPS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o) OUT="$2"; shift 2 ;;
    --all) ALLOW_IMU_GPS=1; shift ;;
    -h|--help)
      echo "Usage: $0 [-o <name>] [--all]"
      echo "  -o <name>   output directory name (default: auto timestamp)"
      echo "  --all       also record /imu/data and /gps/fix"
      exit 0 ;;
    *) OUT="$1"; shift ;;
  esac
done

TOPICS="/scan /tf /tf_static /odom /wheel/odometry"
if [[ "${ALLOW_IMU_GPS:-0}" == "1" ]]; then
  TOPICS="$TOPICS /imu/data /gps/fix"
fi

if [[ -n "$OUT" ]]; then
  echo "==> Recording to rosbag2/$OUT"
  set -x
  ros2 bag record -o "$OUT" $TOPICS
else
  echo "==> Recording (Ctrl+C to stop)"
  exec ros2 bag record -z $TOPICS
fi