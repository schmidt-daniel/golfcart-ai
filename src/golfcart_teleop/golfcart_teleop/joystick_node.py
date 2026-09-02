#!/usr/bin/env python3
"""Joystick teleop node.

Subscribes to /joy (sensor_msgs/Joy) and publishes MotionRequest on
/motion/request. Left stick Y -> linear velocity, right stick X -> angular.
Button 0 (A) -> enable (calls /safety/enable), Button 1 (B) -> stop
(calls /safety/stop).
"""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Joy
from std_srvs.srv import Trigger
from golfcart_msgs.msg import MotionRequest


class JoystickTeleopNode(Node):
    def __init__(self):
        super().__init__('joystick_node')
        self.declare_parameter('max_linear', 1.0)
        self.declare_parameter('max_angular', 1.0)
        self.declare_parameter('axis_linear', 1)   # left stick Y
        self.declare_parameter('axis_angular', 3)  # right stick X
        self.declare_parameter('button_enable', 0)
        self.declare_parameter('button_stop', 1)

        self.max_linear = self.get_parameter('max_linear').value
        self.max_angular = self.get_parameter('max_angular').value
        self.axis_linear = self.get_parameter('axis_linear').value
        self.axis_angular = self.get_parameter('axis_angular').value
        self.button_enable = self.get_parameter('button_enable').value
        self.button_stop = self.get_parameter('button_stop').value

        self.pub = self.create_publisher(MotionRequest, 'motion/request', 10)
        self.sub = self.create_subscription(Joy, 'joy', self.joy_cb, 10)

        self.enable_client = self.create_client(Trigger, 'safety/enable')
        self.stop_client = self.create_client(Trigger, 'safety/stop')

        # Track previous button states to detect press edges.
        self._prev_enable = False
        self._prev_stop = False

        self.get_logger().info('Joystick teleop ready')

    def joy_cb(self, msg: Joy):
        if len(msg.axes) <= max(self.axis_linear, self.axis_angular):
            return
        if len(msg.buttons) <= max(self.button_enable, self.button_stop):
            return

        enable_pressed = bool(msg.buttons[self.button_enable])
        stop_pressed = bool(msg.buttons[self.button_stop])

        # Edge-triggered enable/stop on button press.
        if enable_pressed and not self._prev_enable:
            self.call_service(self.enable_client, 'enable')
        if stop_pressed and not self._prev_stop:
            self.call_service(self.stop_client, 'stop')
            self.publish(0.0, 0.0, 'joystick')

        self._prev_enable = enable_pressed
        self._prev_stop = stop_pressed

        linear = msg.axes[self.axis_linear] * self.max_linear
        angular = msg.axes[self.axis_angular] * self.max_angular
        self.publish(linear, angular, 'joystick')

    def call_service(self, client, name):
        if not client.service_is_ready():
            self.get_logger().warn(f'{name} service not ready')
            return
        req = Trigger.Request()
        future = client.call_async(req)
        future.add_done_callback(
            lambda f: self.get_logger().info(
                f'{name}: {f.result().message if f.result() else "failed"}'))

    def publish(self, linear, angular, source):
        req = MotionRequest()
        req.linear_velocity_mps = float(linear)
        req.angular_velocity_radps = float(angular)
        req.source = source
        req.priority = 1
        req.timestamp = self.get_clock().now().to_msg()
        self.pub.publish(req)


def main(args=None):
    rclpy.init(args=args)
    node = JoystickTeleopNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
