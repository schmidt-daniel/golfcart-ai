#!/usr/bin/env python3
"""Headless test of the speed_zone_node end-to-end.

Generates a course zip with a SPEED_ZONE, starts course_registry_node +
speed_zone_node, selects the course, publishes odometry inside/outside the
zone, and verifies /speed_zone/status.limit_mps. Exits cleanly.
"""
import math
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from golfcart_msgs.msg import SpeedZoneStatus
from golfcart_msgs.srv import CourseSelect
from nav_msgs.msg import Odometry

# Build a course zip with a SPEED_ZONE using the map editor model (pure python).
def build_course_zip(out_dir: Path) -> Path:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'map_editor'))
    from map_editor.model import Course, CourseOrigin, Hole, Shape
    from map_editor.exporter import export_course

    origin = CourseOrigin(latitude_deg=48.12345, longitude_deg=11.67890,
                          rotation_rad=0.0, course_name="Speed Test", course_id="st1")
    course = Course(origin=origin, osm_id=1)
    # A square speed zone around the origin, roughly +/-0.0005 deg (~55 m).
    # max_speed_mps = 0.5.
    hole = Hole(
        number=1,
        boundary=[(48.12345, 11.67890), (48.12400, 11.67890),
                  (48.12400, 11.67940), (48.12345, 11.67940)],
        shapes=[
            Shape(type="SPEED_ZONE", label="Slow zone", max_speed_mps=0.5,
                  vertices=[(48.12345, 11.67890), (48.12400, 11.67890),
                            (48.12400, 11.67940), (48.12345, 11.67940)]),
        ],
    )
    course.holes = [hole]
    return export_course(course, out_dir)


class SpeedZoneTest(Node):
    def __init__(self):
        super().__init__('speed_zone_test')
        self.received = []
        qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)
        self.sub = self.create_subscription(
            SpeedZoneStatus, 'speed_zone/status', self.on_status, qos)
        self.odom_pub = self.create_publisher(Odometry, 'odometry/filtered', qos)
        self.select_client = self.create_client(CourseSelect, 'course/select')

    def on_status(self, msg):
        self.received.append(msg)

    def select_course(self, course_id):
        if not self.select_client.wait_for_service(timeout_sec=5.0):
            self.get_logger().error('course/select not available')
            return False
        req = CourseSelect.Request()
        req.course_id = course_id
        future = self.select_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=5.0)
        return future.result() is not None and future.result().success

    def pub_odom(self, x, y):
        msg = Odometry()
        msg.pose.pose.position.x = x
        msg.pose.pose.position.y = y
        msg.pose.pose.orientation.w = 1.0
        self.odom_pub.publish(msg)


def main():
    tmp = Path(tempfile.mkdtemp())
    zip_path = build_course_zip(tmp)
    print('course zip:', zip_path)

    rclpy.init()
    test = SpeedZoneTest()
    reg = subprocess.Popen(
        ['ros2', 'run', 'golfcart_navigation', 'course_registry_node',
         '--ros-args', '-p', f'courses_dir:={tmp}'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sz = subprocess.Popen(
        ['ros2', 'run', 'golfcart_navigation', 'speed_zone_node'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(4)

    ok = test.select_course('st1')
    print('select course:', ok)
    time.sleep(1)

    # Inside the zone: origin maps to (0,0) in map frame -> inside the square.
    for _ in range(5):
        test.pub_odom(0.0, 0.0)
        rclpy.spin_once(test, timeout_sec=0.5)
        time.sleep(0.3)
    time.sleep(1)
    rclpy.spin_once(test, timeout_sec=1.0)

    if not test.received:
        print('FAIL: no speed_zone status received')
        sys.exit(1)

    inside = test.received[-1]
    print(f'inside zone: valid={inside.valid} limit={inside.limit_mps} '
          f'zone={inside.zone!r}')
    assert inside.valid
    assert abs(inside.limit_mps - 0.5) < 1e-6, inside.limit_mps
    assert inside.zone == 'Slow zone'

    # Outside the zone: far away (e.g. 500 m east).
    test.received.clear()
    for _ in range(5):
        test.pub_odom(500.0, 500.0)
        rclpy.spin_once(test, timeout_sec=0.5)
        time.sleep(0.3)
    time.sleep(1)
    rclpy.spin_once(test, timeout_sec=1.0)

    outside = test.received[-1]
    print(f'outside zone: valid={outside.valid} limit={outside.limit_mps} '
          f'zone={outside.zone!r}')
    assert outside.valid
    assert outside.limit_mps < 0.0, outside.limit_mps  # -1 = no zone limit

    print('PASS')
    reg.terminate()
    sz.terminate()
    test.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()