#!/usr/bin/env python3
"""Extract learning observations (flags) from a recorded rosbag.

Reads a bag recorded with `record_bag.sh --all` (which captures /gps/fix,
/imu/data, /odometry/filtered, /obstacles/state) and writes a JSONL file of
discrete observations in the course map frame:

    drivable  — the trolley drove here (GPS fix present)
    steep     — the IMU measured high pitch/roll (terrain is steep)
    obstacle  — an obstacle was in the stopping zone

These observations are merged into the existing course map by
`tools/map_editor/map_editor/refine.py` (via `scripts/refine_map.sh`).

The observations are in the course map frame (equirectangular projection from
the course origin, matching `geo.py` / `slope_node`). The course origin is
read from the course zip (`course.yaml`), or from `--origin`.

Usage:
  python3 scripts/extract_flags.py <bag_dir> --course <course.zip> -o obs.jsonl
  python3 scripts/extract_flags.py <bag_dir> --origin "48.12345,11.67890,0.0" -o obs.jsonl
  python3 scripts/extract_flags.py <bag_dir> --course <course.zip> --steep-threshold 0.15 -o obs.jsonl

Options:
  --course <zip>            course zip (reads origin from course.yaml)
  --origin "lat,lon,rot"    course origin (lat, lon, rotation rad); overrides
                            the course zip origin
  -o <file>                 output JSONL (default: <bag_dir>/obs.jsonl)
  --steep-threshold <rad>   IMU pitch/roll threshold for a "steep" flag
                            (default: 0.15 rad ~ 8.6 deg)
  --gps-topic <name>        GPS topic (default: /gps/fix)
  --imu-topic <name>        IMU topic (default: /imu/data)
  --obstacle-topic <name>   obstacle topic (default: /obstacles/state)
"""

import argparse
import json
import math
import sys
import zipfile

import rosbag2_py
from rclpy.serialization import deserialize_message
from rosidl_runtime_py.utilities import get_message

R_EARTH = 6371000.0

GPS_TOPIC = '/gps/fix'
GPS_TYPE = 'golfcart_msgs/msg/GpsFix'
IMU_TOPIC = '/imu/data'
IMU_TYPE = 'golfcart_msgs/msg/ImuData'
OBSTACLE_TOPIC = '/obstacles/state'
OBSTACLE_TYPE = 'golfcart_msgs/msg/ObstacleState'


def latlon_to_map(lat, lon, origin_lat, origin_lon, rotation_rad=0.0):
    """Convert lat/lon to map-frame (x=east, y=north) meters (matches geo.py)."""
    x = math.radians(lon - origin_lon) * R_EARTH * math.cos(math.radians(origin_lat))
    y = math.radians(lat - origin_lat) * R_EARTH
    if rotation_rad:
        c = math.cos(rotation_rad)
        s = math.sin(rotation_rad)
        x, y = x * c - y * s, x * s + y * c
    return x, y


def origin_from_course_zip(zip_path):
    """Read the course origin from a course zip's course.yaml."""
    import yaml
    with zipfile.ZipFile(zip_path) as zf:
        doc = yaml.safe_load(zf.read('course.yaml'))
    course = doc.get('course', doc)
    origin = course.get('origin', {})
    return (float(origin['latitude_deg']), float(origin['longitude_deg']),
            float(origin.get('rotation_rad', 0.0)))


def open_reader(bag_dir):
    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=bag_dir, storage_id='sqlite3'),
        rosbag2_py.ConverterOptions(
            input_serialization_format='cdr', output_serialization_format='cdr'),
    )
    return reader


def main():
    parser = argparse.ArgumentParser(description='Extract learning flags from a bag')
    parser.add_argument('bag_dir', help='input bag directory (with metadata.yaml)')
    parser.add_argument('--course', help='course zip (reads origin from course.yaml)')
    parser.add_argument('--origin', help='course origin "lat,lon,rot" (overrides)')
    parser.add_argument('-o', '--out', help='output JSONL (default: <bag_dir>/obs.jsonl)')
    parser.add_argument('--steep-threshold', type=float, default=0.15,
                        help='IMU pitch/roll threshold for steep flag (rad)')
    parser.add_argument('--gps-topic', default=GPS_TOPIC)
    parser.add_argument('--imu-topic', default=IMU_TOPIC)
    parser.add_argument('--obstacle-topic', default=OBSTACLE_TOPIC)
    args = parser.parse_args()

    # ---- Determine the course origin ----
    if args.origin:
        parts = args.origin.split(',')
        origin_lat, origin_lon = float(parts[0]), float(parts[1])
        rotation = float(parts[2]) if len(parts) > 2 else 0.0
    elif args.course:
        origin_lat, origin_lon, rotation = origin_from_course_zip(args.course)
    else:
        print('ERROR: provide --course <zip> or --origin "lat,lon,rot" '
              'to georeference the observations.', file=sys.stderr)
        return 1

    out_path = args.out or (args.bag_dir.rstrip('/') + '/obs.jsonl')

    # ---- Read the bag and emit observations ----
    reader = open_reader(args.bag_dir)
    gps_cls = get_message(GPS_TYPE)
    imu_cls = get_message(IMU_TYPE)
    obs_cls = get_message(OBSTACLE_TYPE)

    # Track the last GPS position so steep/obstacle flags can be located.
    last_x = last_y = None
    counts = {'drivable': 0, 'steep': 0, 'obstacle': 0}

    with open(out_path, 'w') as f:
        while reader.has_next():
            topic, data, _ = reader.read_next()
            if topic == args.gps_topic:
                msg = deserialize_message(data, gps_cls)
                if not msg.valid:
                    continue
                last_x, last_y = latlon_to_map(
                    msg.latitude_deg, msg.longitude_deg, origin_lat, origin_lon, rotation)
                f.write(json.dumps({"kind": "drivable", "x": last_x, "y": last_y,
                                    "conf": 1.0}) + "\n")
                counts['drivable'] += 1
            elif topic == args.imu_topic and last_x is not None:
                msg = deserialize_message(data, imu_cls)
                if not msg.valid:
                    continue
                if abs(msg.pitch_rad) > args.steep_threshold or \
                   abs(msg.roll_rad) > args.steep_threshold:
                    f.write(json.dumps({
                        "kind": "steep", "x": last_x, "y": last_y, "conf": 1.0,
                        "pitch": msg.pitch_rad, "roll": msg.roll_rad,
                    }) + "\n")
                    counts['steep'] += 1
            elif topic == args.obstacle_topic and last_x is not None:
                msg = deserialize_message(data, obs_cls)
                if msg.valid and msg.obstacle_in_zone:
                    f.write(json.dumps({"kind": "obstacle", "x": last_x, "y": last_y,
                                        "conf": 1.0}) + "\n")
                    counts['obstacle'] += 1

    print(f'Wrote {out_path}: {counts}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
