#!/usr/bin/env bash
# =============================================================================
# docker_run.sh — Run a golfcart docker command with OOM protection.
#
# The host has limited RAM (7.8 GiB, no swap) and runs several other Docker
# containers (open-webui, traccar, workout-tracker, actualbudget). A heavy
# build (colcon build, pio run, colcon test) can OOM the host and kill the
# code-server (VS Code) itself. This wrapper bounds the container's memory so
# a runaway build can't take down the host.
#
# Usage:
#   ./docker/docker_run.sh <image> <workdir> <command...>
#
# Examples:
#   ./docker/docker_run.sh golfcart:lyrical /workspace \
#       bash -c 'source /opt/ros/${ROS_DISTRO}/setup.bash && colcon build'
#   ./docker/docker_run.sh golfcart:lyrical /workspace/src/golfcart_hmi/firmware \
#       pio run -e esp32s3
#
# Env overrides:
#   CONTAINER_MEM_LIMIT   memory cap for the container (default 4g)
#   BUILD_JOBS            make/colcon parallelism (default 2)
# =============================================================================
set -euo pipefail

IMAGE="${1:?usage: docker_run.sh <image> <workdir> <command...>}"
WORKDIR="${2:?usage: docker_run.sh <image> <workdir> <command...>}"
shift 2

CONTAINER_MEM_LIMIT="${CONTAINER_MEM_LIMIT:-4g}"
BUILD_JOBS="${BUILD_JOBS:-2}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> docker run (mem_limit=${CONTAINER_MEM_LIMIT}, jobs=${BUILD_JOBS})"
echo "    image:   ${IMAGE}"
echo "    workdir: ${WORKDIR}"
echo "    cmd:     $*"

# --memory caps the container so a runaway build can't OOM the host.
# MAKEFLAGS limits make/colcon parallelism to reduce peak memory.
docker run --rm \
  --memory "${CONTAINER_MEM_LIMIT}" \
  -e MAKEFLAGS="-j${BUILD_JOBS}" \
  -v "${ROOT_DIR}:/workspace" \
  -w "${WORKDIR}" \
  "${IMAGE}" bash -c "$*"