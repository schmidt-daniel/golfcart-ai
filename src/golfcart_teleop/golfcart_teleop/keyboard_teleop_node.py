#!/usr/bin/env python3
"""Keyboard teleop node.

Reads keyboard input and publishes MotionRequest on /motion/request.
W/S -> forward/back, A/D -> turn, Space -> stop, E -> enable.
"""

import sys
import select
import termios
import tty

import rclpy
from rclpy.node import Node
from golfcart_msgs.msg import MotionRequest


class KeyboardTeleopNode(Node):
    def __init__(self):
        super().__init__('keyboard_teleop_node')
        self.declare_parameter('max_linear', 1.0)
        self.declare_parameter('max_angular', 1.0)
        self.max_linear = self.get_parameter('max_linear').value
        self.max_angular = self.get_parameter('max_angular').value
        self.pub = self.create_publisher(MotionRequest, 'motion/request', 10)
        self.get_logger().info(
            'Keyboard teleop: W/S forward/back, A/D turn, Space stop, E enable')

    def publish(self, linear, angular, source):
        req = MotionRequest()
        req.linear_velocity_mps = float(linear)
        req.angular_velocity_radps = float(angular)
        req.source = source
        req.priority = 1
        req.timestamp = self.get_clock().now().to_msg()
        self.pub.publish(req)

    def run(self):
        old = termios.tcgetattr(sys.stdin)
        try:
            tty.setcbreak(sys.stdin.fileno())
            while rclpy.ok():
                if select.select([sys.stdin], [], [], 0.1)[0]:
                    key = sys.stdin.read(1)
                    if key == 'w':
                        self.publish(self.max_linear, 0.0, 'keyboard')
                    elif key == 's':
                        self.publish(-self.max_linear, 0.0, 'keyboard')
                    elif key == 'a':
                        self.publish(0.0, self.max_angular, 'keyboard')
                    elif key == 'd':
                        self.publish(0.0, -self.max_angular, 'keyboard')
                    elif key == ' ':
                        self.publish(0.0, 0.0, 'keyboard')
                    elif key == 'e':
                        self.get_logger().info('Enable requested (call safety/enable)')
                    elif key == '\x03':  # Ctrl-C
                        break
        finally:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, sys.stdin.fileno())


def main(args=None):
    rclpy.init(args=args)
    node = KeyboardTeleopNode()
    try:
        node.run()
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
