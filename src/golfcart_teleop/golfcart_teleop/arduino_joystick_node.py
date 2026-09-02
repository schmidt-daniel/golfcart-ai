#!/usr/bin/env python3
"""Arduino joystick teleop node.

Reads the Arduino Uno serial output (x,y,btn) and publishes MotionRequest on
/motion/request. The button controls safety: a press calls /safety/enable,
a release calls /safety/stop.

Serial protocol from the Arduino:
  x:<0-1023>,y:<0-1023>,btn:<0|1>
"""

import serial

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger
from golfcart_msgs.msg import MotionRequest


class ArduinoJoystickNode(Node):
    def __init__(self):
        super().__init__('arduino_joystick_node')
        self.declare_parameter('port', '/dev/ttyACM0')
        self.declare_parameter('baud', 115200)
        self.declare_parameter('max_linear', 1.0)
        self.declare_parameter('max_angular', 1.0)
        self.declare_parameter('deadzone', 0.05)

        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value
        self.max_linear = self.get_parameter('max_linear').value
        self.max_angular = self.get_parameter('max_angular').value
        self.deadzone = self.get_parameter('deadzone').value

        self.pub = self.create_publisher(MotionRequest, 'motion/request', 10)
        self.enable_client = self.create_client(Trigger, 'safety/enable')
        self.stop_client = self.create_client(Trigger, 'safety/stop')

        self._prev_btn = 0
        self._last_linear = 0.0
        self._last_angular = 0.0

        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.get_logger().info(f'Opened serial port {port} @ {baud}')
        except serial.SerialException as e:
            self.get_logger().fatal(f'Failed to open {port}: {e}')
            raise

        self.timer = self.create_timer(0.02, self.read_serial)  # 50 Hz

    def read_serial(self):
        try:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
        except serial.SerialException as e:
            self.get_logger().error(f'Serial read error: {e}')
            return
        if not line:
            return

        x = y = 512
        btn = 0
        for part in line.split(','):
            if part.startswith('x:'):
                x = int(part[2:])
            elif part.startswith('y:'):
                y = int(part[2:])
            elif part.startswith('btn:'):
                btn = int(part[4:])

        # Map 0-1023 to -1..1 (center 512).
        linear = (y - 512) / 512.0
        angular = (x - 512) / 512.0

        # Apply deadzone.
        if abs(linear) < self.deadzone:
            linear = 0.0
        if abs(angular) < self.deadzone:
            angular = 0.0

        linear *= self.max_linear
        angular *= self.max_angular

        # Button edge detection for safety.
        if btn == 1 and self._prev_btn == 0:
            self.call_service(self.enable_client, 'enable')
        elif btn == 0 and self._prev_btn == 1:
            self.call_service(self.stop_client, 'stop')
            linear = 0.0
            angular = 0.0
        self._prev_btn = btn

        self._last_linear = linear
        self._last_angular = angular
        self.publish(linear, angular, 'arduino_joystick')

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
    node = ArduinoJoystickNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()