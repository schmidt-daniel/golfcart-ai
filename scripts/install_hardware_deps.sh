#!/usr/bin/env bash
# Install and configure hardware-side dependencies on the Raspberry Pi.
#
# Usage:
#   ./scripts/install_hardware_deps.sh \
#     --gps-device /dev/serial/by-id/USB-GPS \
#     --lidar-horizontal /dev/serial/by-id/USB-LiDAR-HORIZONTAL \
#     --lidar-tilted /dev/serial/by-id/USB-LiDAR-TILTED
set -euo pipefail

GPS_DEVICE=""
LIDAR_HORIZONTAL="/dev/ttyUSB0"
LIDAR_TILTED="/dev/ttyUSB1"
LIDAR_WS="${HOME}/ldlidar_ros2_ws"
LIDAR_REPO="https://github.com/ldrobotSensorTeam/ldlidar_ros2.git"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gps-device) GPS_DEVICE="$2"; shift 2 ;;
    --lidar-horizontal) LIDAR_HORIZONTAL="$2"; shift 2 ;;
    --lidar-tilted) LIDAR_TILTED="$2"; shift 2 ;;
    --lidar-workspace) LIDAR_WS="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

if [[ -z "$GPS_DEVICE" ]]; then
  echo "ERROR: --gps-device is required" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(dirname "$SCRIPT_DIR")"
ROS_DISTRO="${ROS_DISTRO:-lyrical}"

if [[ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]]; then
  echo "ERROR: ROS 2 setup not found at /opt/ros/${ROS_DISTRO}/setup.bash" >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y gpsd gpsd-clients i2c-tools git python3-rosdep python3-colcon-common-extensions

# gpsd owns the GPS serial device; gps_node connects to gpsd on localhost:2947.
sudo install -d -m 0755 /etc/golfcart
sudo tee /etc/default/gpsd >/dev/null <<EOF
START_DAEMON="true"
USBAUTO="false"
DEVICES="${GPS_DEVICE}"
GPSD_OPTIONS="-n"
EOF
sudo systemctl enable gpsd.service
sudo systemctl restart gpsd.service

mkdir -p "${LIDAR_WS}/src"
if [[ ! -d "${LIDAR_WS}/src/ldlidar_ros2/.git" ]]; then
  git clone --depth 1 "$LIDAR_REPO" "${LIDAR_WS}/src/ldlidar_ros2"
fi

source "/opt/ros/${ROS_DISTRO}/setup.bash"
cd "$LIDAR_WS"
rosdep update
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install

sudo tee /etc/golfcart/golfcart.env >/dev/null <<EOF
GPS_IMPLEMENTATION=real
LIDAR_DRIVER=ldlidar
LIDAR_DEVICE_HORIZONTAL=${LIDAR_HORIZONTAL}
LIDAR_DEVICE_TILTED=${LIDAR_TILTED}
LDLIDAR_SETUP=${LIDAR_WS}/install/setup.bash
EOF

# Keep the generated install overlay available for interactive diagnostics.
if ! grep -q "${LIDAR_WS}/install/setup.bash" "${HOME}/.bashrc" 2>/dev/null; then
  printf '\nsource %s/install/setup.bash\n' "$LIDAR_WS" >> "${HOME}/.bashrc"
fi

echo "==> Hardware dependencies configured"
echo "    GPS: $GPS_DEVICE"
echo "    Horizontal LiDAR: $LIDAR_HORIZONTAL"
echo "    Tilted LiDAR: $LIDAR_TILTED"
echo "    LiDAR overlay: $LIDAR_WS/install"
