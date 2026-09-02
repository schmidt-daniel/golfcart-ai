#!/usr/bin/env bash
# Build and test the golf cart workspace inside a ROS 2 Lyrical container.
#
# Usage:
#   ./docker/build.sh          # build the image
#   ./docker/build.sh test     # build image + run colcon build & tests
#   ./docker/build.sh shell    # open an interactive shell in the container
#
# Note: run via `sg docker -c "..."` if your user is in the docker group
# but the group membership is not active in the current session.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE="golfcart:lyrical"

docker_cmd() {
  if docker info >/dev/null 2>&1; then
    docker "$@"
  else
    sg docker -c "docker $*"
  fi
}

build_image() {
  echo "==> Building image ${IMAGE}"
  docker_cmd build -t "${IMAGE}" -f "${SCRIPT_DIR}/Dockerfile" "${ROOT_DIR}"
}

run_build_test() {
  build_image
  echo "==> Building and testing workspace"
  docker_cmd run --rm -v "${ROOT_DIR}:/workspace" -w /workspace \
    "${IMAGE}" bash -c "source /opt/ros/\${ROS_DISTRO}/setup.bash && \
      colcon build && \
      colcon test && \
      colcon test-result --verbose"
}

run_shell() {
  build_image
  echo "==> Opening shell"
  docker_cmd run --rm -it -v "${ROOT_DIR}:/workspace" -w /workspace \
    "${IMAGE}" bash
}

case "${1:-build}" in
  test) run_build_test ;;
  shell) run_shell ;;
  build) build_image ;;
  *) echo "Unknown command: $1"; exit 1 ;;
esac
