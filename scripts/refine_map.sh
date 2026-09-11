#!/usr/bin/env bash
# =============================================================================
# refine_map.sh — Refine a course map with recorded observations (learning loop).
#
# Merges observations extracted from a recorded round (rosbag) into an existing
# course zip to produce a refined course map. The refined map is pushed back to
# the cart and loaded like any other course.
#
# Pipeline:
#   1. extract_flags.py  — bag -> obs.jsonl (drivable/steep/obstacle flags)
#   2. refine_map.py     — merge obs.jsonl into course.zip -> refined.zip
#
# Usage:
#   ./scripts/refine_map.sh <course.zip> <bag_dir> [-o <refined.zip>]
#
#   <course.zip>   the existing course map (from the map editor)
#   <bag_dir>      recorded round (rosbag2/<name> from record_bag.sh --all)
#   -o <zip>       output refined course zip (default: <course>_refined.zip)
#
# Requires a ROS 2 Lyrical environment sourced (rosbag2, this workspace) for
# the bag-reading step, plus numpy/yaml for the merge.
#
# Example:
#   # record a round on the cart
#   ./scripts/record_bag.sh --all -o round1
#   # copy bag + course to workstation
#   rsync -avz pi@cart:~/golfcart-ai/rosbag2/round1 ./
#   # refine the course
#   ./scripts/refine_map.sh golfcart-my-course-123-20260911.zip rosbag2/round1
#   # push the refined map back
#   rsync -avz golfcart-my-course-123-20260911_refined.zip pi@cart:~/golfcart-ai/courses/
# =============================================================================
set -euo pipefail

COURSE_ZIP=""
BAG_DIR=""
OUT_ZIP=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o) OUT_ZIP="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 <course.zip> <bag_dir> [-o <refined.zip>]"
      exit 0 ;;
    *) if [[ -z "$COURSE_ZIP" ]]; then COURSE_ZIP="$1"; else BAG_DIR="$1"; fi; shift ;;
  esac
done

if [[ -z "$COURSE_ZIP" || -z "$BAG_DIR" ]]; then
  echo "ERROR: provide <course.zip> and <bag_dir>"
  exit 1
fi
if [[ ! -f "$COURSE_ZIP" ]]; then
  echo "ERROR: course zip not found: $COURSE_ZIP"
  exit 1
fi
if [[ ! -f "$BAG_DIR/metadata.yaml" ]]; then
  echo "ERROR: bag dir must contain metadata.yaml: $BAG_DIR"
  exit 1
fi

if [[ -z "$OUT_ZIP" ]]; then
  OUT_ZIP="${COURSE_ZIP%.zip}_refined.zip"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OBS_JSONL="$(mktemp --suffix=.jsonl)"

echo "==> Extracting observations from bag: $BAG_DIR"
python3 "$SCRIPT_DIR/extract_flags.py" "$BAG_DIR" --course "$COURSE_ZIP" -o "$OBS_JSONL"

echo "==> Refining course: $COURSE_ZIP -> $OUT_ZIP"
python3 -m map_editor.refine "$COURSE_ZIP" "$OBS_JSONL" -o "$OUT_ZIP"

rm -f "$OBS_JSONL"
echo "==> Done: $OUT_ZIP"