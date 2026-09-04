#!/usr/bin/env python3
"""Extract a spatial subset of a recorded bag using a polygon.

Reads a recorded ROS 2 bag (from record_bag.sh) and writes a new bag containing
only the messages captured while the cart's GPS position was inside a user-drawn
polygon. This lets you split a full-course recording into per-hole bags, then
build a per-hole map with build_map_offline.sh.

The polygon is defined in lat/lon (WGS84). The cart's position comes from the
/gps/fix topic (golfcart_msgs/GpsFix). Messages on other topics are kept only
for the time windows when the cart was inside the polygon (with an optional
padding before/after entry).

Usage:
  python3 scripts/extract_bag_polygon.py <bag_dir> --polygon "lat1,lon1 lat2,lon2 ..." -o <out>
  python3 scripts/extract_bag_polygon.py <bag_dir> --geojson hole5.geojson -o <out>
  python3 scripts/extract_bag_polygon.py <bag_dir> --polygon "..." --pad 3.0 -o <out>

Options:
  --polygon "lat,lon lat,lon ..."   polygon corners (space-separated lat,lon pairs)
  --geojson <file>                  polygon from a GeoJSON Polygon feature
  -o <out>                          output bag directory (default: <bag>_extracted)
  --pad <seconds>                   keep this many seconds before/after entering the polygon
  --gps-topic <name>                GPS topic (default: /gps/fix)
  --keep-topics "a b c"             only keep these topics (default: all)
"""

import argparse
import json
import os
import sys

import rosbag2_py
from rclpy.serialization import deserialize_message, serialize_message
from rosidl_runtime_py.utilities import get_message

# golfcart_msgs GpsFix fields
GPS_TOPIC_DEFAULT = '/gps/fix'
GPS_MSG_TYPE = 'golfcart_msgs/msg/GpsFix'


def point_in_polygon(lat, lon, poly):
    """Ray-casting point-in-polygon test. poly = [(lat,lon), ...]."""
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        lat_i, lon_i = poly[i]
        lat_j, lon_j = poly[j]
        if ((lon_i > lon) != (lon_j > lon)) and \
           (lat < (lat_j - lat_i) * (lon - lon_i) / (lon_j - lon_i) + lat_i):
            inside = not inside
        j = i
    return inside


def parse_polygon(text):
    """Parse 'lat,lon lat,lon ...' into [(lat,lon), ...]."""
    pts = []
    for pair in text.split():
        lat, lon = pair.split(',')
        pts.append((float(lat), float(lon)))
    if len(pts) < 3:
        raise ValueError('polygon needs at least 3 corners')
    return pts


def load_geojson(path):
    """Load a GeoJSON Polygon feature -> [(lat,lon), ...]."""
    with open(path) as f:
        data = json.load(f)
    # Accept a FeatureCollection, a Feature, or a bare geometry.
    geom = data
    if data.get('type') == 'FeatureCollection':
        geom = data['features'][0]['geometry']
    elif data.get('type') == 'Feature':
        geom = data['geometry']
    if geom.get('type') != 'Polygon':
        raise ValueError('GeoJSON must contain a Polygon geometry')
    # GeoJSON is [lon, lat]; convert to [(lat, lon)].
    return [(c[1], c[0]) for c in geom['coordinates'][0]]


def main():
    parser = argparse.ArgumentParser(description='Extract bag data inside a polygon')
    parser.add_argument('bag_dir', help='input bag directory (with metadata.yaml)')
    parser.add_argument('--polygon', help='polygon corners "lat,lon lat,lon ..."')
    parser.add_argument('--geojson', help='GeoJSON file with a Polygon')
    parser.add_argument('-o', '--out', help='output bag directory')
    parser.add_argument('--pad', type=float, default=0.0,
                        help='seconds to keep before/after entering the polygon')
    parser.add_argument('--gps-topic', default=GPS_TOPIC_DEFAULT)
    parser.add_argument('--keep-topics', help='space-separated topics to keep (default: all)')
    args = parser.parse_args()

    if not args.polygon and not args.geojson:
        parser.error('provide --polygon or --geojson')
    poly = parse_polygon(args.polygon) if args.polygon else load_geojson(args.geojson)

    out_dir = args.out or (args.bag_dir.rstrip('/') + '_extracted')
    keep = set(args.keep_topics.split()) if args.keep_topics else None

    # ---- Open reader ----
    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=args.bag_dir, storage_id='sqlite3'),
        rosbag2_py.ConverterOptions(
            input_serialization_format='cdr', output_serialization_format='cdr'),
    )
    topics = reader.get_all_topics_and_types()
    topic_types = {t.name: t.type for t in topics}
    print(f'Input topics: {sorted(topic_types)}')

    # ---- Open writer ----
    writer = rosbag2_py.SequentialWriter()
    writer.open(
        rosbag2_py.StorageOptions(uri=out_dir, storage_id='sqlite3'),
        rosbag2_py.ConverterOptions(
            input_serialization_format='cdr', output_serialization_format='cdr'),
    )
    for i, t in enumerate(topics):
        if keep is None or t.name in keep:
            writer.create_topic(
                rosbag2_py.TopicMetadata(
                    id=i, name=t.name, type=t.type,
                    serialization_format='cdr'))

    # ---- Pass 1: find time windows when the cart is inside the polygon ----
    # We need the GPS fix timestamps to know when the cart is inside.
    inside_windows = []  # list of (start_ns, end_ns)
    cur_start = None
    last_inside = False
    last_inside_ts = 0
    gps_msg_cls = get_message(GPS_MSG_TYPE)

    # Reset reader to start.
    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=args.bag_dir, storage_id='sqlite3'),
        rosbag2_py.ConverterOptions(
            input_serialization_format='cdr', output_serialization_format='cdr'),
    )

    while reader.has_next():
        topic, data, t = reader.read_next()
        if topic == args.gps_topic:
            msg = deserialize_message(data, gps_msg_cls)
            if not msg.valid:
                continue
            inside = point_in_polygon(msg.latitude_deg, msg.longitude_deg, poly)
            if inside:
                # Track the last timestamp the cart was inside.
                last_inside_ts = t
                if not last_inside:
                    cur_start = t
            elif last_inside and cur_start is not None:
                # Exited the polygon: window ends at the last inside sample.
                inside_windows.append((cur_start, last_inside_ts))
                cur_start = None
            last_inside = inside
    if last_inside and cur_start is not None:
        inside_windows.append((cur_start, last_inside_ts))  # open-ended to end of bag

    if not inside_windows:
        print('WARNING: no GPS fixes found inside the polygon; nothing extracted.')
        return 1

    # Apply padding.
    padded = []
    for s, e in inside_windows:
        s = max(0, s - int(args.pad * 1e9))
        e = e + int(args.pad * 1e9) if e is not None else None
        padded.append((s, e))
    inside_windows = padded

    print(f'Inside windows: {inside_windows}')

    # ---- Pass 2: write messages whose timestamp falls in a window ----
    def in_window(t):
        for s, e in inside_windows:
            if e is None:
                if t >= s:
                    return True
            elif s <= t <= e:
                return True
        return False

    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=args.bag_dir, storage_id='sqlite3'),
        rosbag2_py.ConverterOptions(
            input_serialization_format='cdr', output_serialization_format='cdr'),
    )

    written = 0
    while reader.has_next():
        topic, data, t = reader.read_next()
        if keep is not None and topic not in keep:
            continue
        if in_window(t):
            writer.write(topic, data, t)
            written += 1

    writer.close()
    reader.close()
    print(f'Extracted {written} messages to {out_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())