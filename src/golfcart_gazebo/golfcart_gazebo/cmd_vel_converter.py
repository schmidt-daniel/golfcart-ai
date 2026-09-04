#!/usr/bin/env python3
"""cmd_vel converter.

The diff_drive_controller in ROS 2 Lyrical subscribes to
geometry_msgs/TwistStamped on /cmd_vel, but the navigation stack (Nav2
velocity_smoother) publishes geometry_msgs/Twist. This node subscribes to
/cmd_vel (Twist) and republishes on /cmd_vel_stamped (TwistStamped) with the
current sim time, matched to the simulation clock.
"""

from rclpy.node import Node
import rclpy
from geometry_msgs.msg import Twist, TwistStamped


class CmdVelConverter(Node):
    def __init__(self):
        super().__init__('cmd_vel_converter')
        self.pub = self.create_publisher(TwistStamped, '/cmd_vel_stamped', 10)
        self.sub = self.create_subscription(
            Twist, '/cmd_vel', self.on_cmd_vel, 10)

    def on_cmd_vel(self, msg: Twist):
        out = TwistStamped()
        out.header.stamp = self.get_clock().now().to_msg()
        out.header.frame_id = 'base_link'
        out.twist = msg
        self.pub.publish(out)


def main(args=None):
    rclpy.init(args=args)
    node = CmdVelConverter()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()