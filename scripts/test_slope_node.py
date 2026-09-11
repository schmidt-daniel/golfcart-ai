#!/usr/bin/env python3
"""Headless test of the slope_node end-to-end.

Starts course_registry_node + slope_node, selects a course, publishes an
odometry (yaw), and verifies /slope/status is published with the expected
roll/pitch. Exits cleanly.
"""
import subprocess
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from golfcart_msgs.msg import SlopeStatus
from golfcart_msgs.srv import CourseSelect
from nav_msgs.msg import Odometry


class SlopeTest(Node):
    def __init__(self):
        super().__init__('slope_test')
        self.received = []
        # Match the slope_node's SensorDataQoS (best-effort).
        qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)
        self.sub = self.create_subscription(
            SlopeStatus, 'slope/status', self.on_slope, qos)
        self.odom_pub = self.create_publisher(Odometry, 'odometry/filtered', qos)
        self.select_client = self.create_client(CourseSelect, 'course/select')

    def on_slope(self, msg):
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

    def pub_odom(self, x, y, yaw):
        msg = Odometry()
        msg.pose.pose.position.x = x
        msg.pose.pose.position.y = y
        # yaw -> quaternion (z = sin(yaw/2), w = cos(yaw/2)).
        msg.pose.pose.orientation.z = float(__import__('math').sin(yaw / 2.0))
        msg.pose.pose.orientation.w = float(__import__('math').cos(yaw / 2.0))
        self.odom_pub.publish(msg)


def main():
    rclpy.init()
    test = SlopeTest()
    # Start the nodes.
    reg = subprocess.Popen(
        ['ros2', 'run', 'golfcart_navigation', 'course_registry_node',
         '--ros-args', '-p', 'courses_dir:=/workspace/courses'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    slope = subprocess.Popen(
        ['ros2', 'run', 'golfcart_navigation', 'slope_node'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(4)

    ok = test.select_course('st1')
    print('select course:', ok)
    time.sleep(1)
    # Publish odometry at (5,5) facing east (yaw=0). Gradient dzdx=0.1 -> pitch ~5.7deg.
    for _ in range(5):
        test.pub_odom(5.0, 5.0, 0.0)
        rclpy.spin_once(test, timeout_sec=0.5)
        time.sleep(0.3)
    time.sleep(1)
    rclpy.spin_once(test, timeout_sec=1.0)

    if test.received:
        m = test.received[-1]
        print(f'slope status: valid={m.valid} roll={m.roll_rad:.3f} '
              f'pitch={m.pitch_rad:.3f} slope={m.slope_deg:.1f}deg')
        # Facing east, dzdx=0.1 -> pitch ~5.7deg, roll ~0.
        import math
        expected = math.degrees(math.atan(0.1))
        print(f'expected pitch ~{expected:.1f}deg')
        assert m.valid
        assert abs(m.pitch_rad - math.radians(expected)) < 0.1, m.pitch_rad
        assert abs(m.roll_rad) < 0.1, m.roll_rad
        print('PASS')
    else:
        print('FAIL: no slope status received')
        sys.exit(1)

    reg.terminate()
    slope.terminate()
    test.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()