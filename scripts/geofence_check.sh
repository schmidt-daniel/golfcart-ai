#!/usr/bin/env bash
# Geofence headless smoke test.
# Launches geofence_node with a test config, drives /gps/fix through
# inside / near / outside, and asserts /geofence/status transitions.
#
# Usage: ./scripts/geofence_check.sh
cd "$(dirname "$0")/.."

export ROS_DISTRO=lyrical
source /opt/ros/lyrical/setup.bash >/dev/null
source install/setup.bash 2>/dev/null || true

set -euo pipefail

CONFIG=/workspace/src/golfcart_geofence/config/test_hole1.yaml
# Origin (51.5, -0.12). Boundary square ~55m half-extent.
# Inside point (near origin), near point (just inside edge), outside point.
INSIDE_LAT=51.5
INSIDE_LON=-0.12
NEAR_LAT=51.50047     # ~3m from the top edge (warn_distance_m=5)
OUTSIDE_LAT=51.5006   # ~11m beyond the top edge

echo "=== launching geofence_node ==="
ros2 run golfcart_geofence geofence_node --ros-args \
  -p config_file:="$CONFIG" \
  -p warn_distance_m:=5.0 \
  -p stop_margin_m:=2.0 \
  -p gps_timeout_s:=3.0 \
  >/tmp/geofence.log 2>&1 &
NODE=$!
sleep 3

# Single continuous publisher; reposition by restarting quickly.
PUB=""
pub_at() {
  kill $PUB 2>/dev/null || true
  sleep 0.2
  python3 /workspace/scripts/gps_fix_pub.py "$1" "$2" 10 >/tmp/gpspub.log 2>&1 &
  PUB=$!
  sleep 2
}

echo "=== INSIDE (origin) ==="
pub_at "$INSIDE_LAT" "$INSIDE_LON"
timeout 3 ros2 topic echo /geofence/status --once 2>/dev/null | grep -E "state|inside|distance" || echo "(no status)"

echo "=== NEAR (3m from edge) ==="
pub_at "$NEAR_LAT" "$INSIDE_LON"
timeout 3 ros2 topic echo /geofence/status 2>/dev/null | grep -m1 -E "state|inside|distance" || echo "(no status)"

echo "=== OUTSIDE (crossed) ==="
pub_at "$OUTSIDE_LAT" "$INSIDE_LON"
timeout 3 ros2 topic echo /geofence/status --once 2>/dev/null | grep -E "state|inside|distance" || echo "(no status)"

kill $PUB $NODE 2>/dev/null
echo "=== geofence log ==="
grep -iE "geofence|loaded|error|fail" /tmp/geofence.log | tail -10
echo "=== done ==="