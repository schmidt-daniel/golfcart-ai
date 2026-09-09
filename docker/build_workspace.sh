#!/usr/bin/env bash
# =============================================================================
# build_workspace.sh — Build/test the golf cart workspace with OOM protection.
#
# The host has limited RAM (7.8 GiB, no swap) and runs several other Docker
# containers. A full `colcon build` can OOM the host. This script:
#   - limits colcon/make parallelism (BUILD_JOBS, default 2)
#   - caps the container's memory (CONTAINER_MEM_LIMIT, default 4g)
#   - removes the container after the build to free memory
#
# Usage:
#   ./docker/build_workspace.sh [build|test]
#   BUILD_JOBS=2 CONTAINER_MEM_LIMIT=4g ./docker/build_workspace.sh test
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE="golfcart:lyrical"
MODE="${1:-build}"

BUILD_JOBS="${BUILD_JOBS:-2}"
CONTAINER_MEM_LIMIT="${CONTAINER_MEM_LIMIT:-4g}"

docker_cmd() {
  if docker info >/dev/null 2>&1; then
    docker "$@"
  else
    sg docker -c "docker $*"
  fi
}

echo "==> Building image ${IMAGE}"
docker_cmd build -t "${IMAGE}" -f "${SCRIPT_DIR}/Dockerfile" "${ROOT_DIR}"

echo "==> Building workspace (jobs=${BUILD_JOBS}, mem_limit=${CONTAINER_MEM_LIMIT})"
CMD="source /opt/ros/\${ROS_DISTRO}/setup.bash && export MAKEFLAGS=-j${BUILD_JOBS} && colcon build --parallel-workers ${BUILD_JOBS}"
if [[ "$MODE" == "test" ]]; then
  CMD="${CMD} && colcon test --parallel-workers ${BUILD_JOBS} && colcon test-result --verbose"
fi

# --memory limits the container so a runaway build can't OOM the host.
docker_cmd run --rm \
  --memory "${CONTAINER_MEM_LIMIT}" \
  -v "${ROOT_DIR}:/workspace" -w /workspace \
  "${IMAGE}" bash -c "${CMD}"

echo "==> Done (container removed, memory freed)"