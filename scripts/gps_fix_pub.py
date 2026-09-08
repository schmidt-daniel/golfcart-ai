#!/usr/bin/env python3
"""Test fixture: publish golfcart_msgs/GpsFix at a controllable lat/lon.

Used by geofence_check.sh to drive the geofence node through
inside / near / outside states.

Usage:
  python3 gps_fix_pub.py <lat> <lon> [rate_hz]
"""
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from golfcart_msgs.msg import GpsFix


class GpsFixPub(Node):
    def __init__(self, lat, lon, rate_hz):
        super().__init__('gps_fix_pub')
        q = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)
        self.pub = self.create_publisher(GpsFix, 'gps/fix', q)
        self.lat = lat
        self.lon = lon
        self.rate_hz = rate_hz

    def run(self):
        period = 1.0 / self.rate_hz
        while rclpy.ok():
            msg = GpsFix()
            msg.latitude_deg = self.lat
            msg.longitude_deg = self.lon
            msg.altitude_m = 0.0
            msg.speed_mps = 0.0
            msg.heading_rad = 0.0
            msg.valid = True
            msg.timestamp = self.get_clock().now().to_msg()
            self.pub.publish(msg)
            time.sleep(period)


def main():
    rclpy.init()
    lat = float(sys.argv[1])
    lon = float(sys.argv[2])
    rate = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    node = GpsFixPub(lat, lon, rate)
    try:
        node.run()
    except KeyboardInterrupt:
        pass
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()