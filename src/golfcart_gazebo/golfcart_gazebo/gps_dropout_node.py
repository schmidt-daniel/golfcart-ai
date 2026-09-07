#!/usr/bin/env python3
"""GPS dropout simulator (test-only).

Subscribes to the simulated GPS topic (/gps, gps_msgs/GPSFix) and republishes
it, but suppresses output during configurable dropout windows. This lets us
validate the summon feature's target-loss handling (and dead-reckoning) without
physically losing GPS.

Usage:
  ros2 run golfcart_gazebo gps_dropout_node.py \
    --ros-args -p dropout_every_s:=10.0 -p dropout_duration_s:=3.0

Parameters:
  dropout_every_s:   period between dropout windows (0 = never drop)
  dropout_duration_s: how long each dropout lasts
  in_topic:          input GPS topic (default /gps)
  out_topic:         output GPS topic (default /gps_dropped)
"""

import rclpy
from rclpy.node import Node
from gps_msgs.msg import GPSFix


class GpsDropoutNode(Node):
    def __init__(self):
        super().__init__('gps_dropout_node')
        self.declare_parameter('dropout_every_s', 0.0)
        self.declare_parameter('dropout_duration_s', 3.0)
        self.declare_parameter('in_topic', '/gps')
        self.declare_parameter('out_topic', '/gps_dropped')

        self.dropout_every_s = self.get_parameter('dropout_every_s').value
        self.dropout_duration_s = self.get_parameter('dropout_duration_s').value
        in_topic = self.get_parameter('in_topic').value
        out_topic = self.get_parameter('out_topic').value

        self.sub = self.create_subscription(GPSFix, in_topic, self.on_gps, 10)
        self.pub = self.create_publisher(GPSFix, out_topic, 10)

        self._in_dropout = False
        self._dropout_start = None
        self._next_dropout = None

        if self.dropout_every_s > 0:
            self._next_dropout = self.get_clock().now()
            self.get_logger().info(
                f'GPS dropout: every {self.dropout_every_s}s for '
                f'{self.dropout_duration_s}s ({in_topic} -> {out_topic})')
        else:
            self.get_logger().info('GPS dropout disabled (dropout_every_s=0)')

    def on_gps(self, msg):
        now = self.get_clock().now()

        # Manage dropout windows.
        if self.dropout_every_s > 0:
            if self._in_dropout:
                if (now - self._dropout_start).nanoseconds / 1e9 >= self.dropout_duration_s:
                    self._in_dropout = False
                    self._next_dropout = now + rclpy.duration.Duration(
                        seconds=self.dropout_every_s)
                    self.get_logger().info('GPS dropout ended')
            elif self._next_dropout is not None and now >= self._next_dropout:
                self._in_dropout = True
                self._dropout_start = now
                self.get_logger().info('GPS dropout started')

        if self._in_dropout:
            return  # suppress output

        self.pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = GpsDropoutNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()