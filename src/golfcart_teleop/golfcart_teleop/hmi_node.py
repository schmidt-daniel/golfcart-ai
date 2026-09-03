#!/usr/bin/env python3
"""HMI node (scaffold).

Renders the operator menu on the TFT display and translates joystick input into
menu navigation and feature commands. This is a minimal scaffold that provides
the "Navigate to target" menu item, which sets a navigation goal via the
/set_goal service.

The full HMI (TFT rendering, joystick navigation, all menu screens) is specified
in docs/hmi-spec.md and implemented incrementally.
"""

import rclpy
from rclpy.node import Node
from golfcart_msgs.srv import SetGoal


class HmiNode(Node):
    def __init__(self):
        super().__init__('hmi_node')
        self.set_goal_client = self.create_client(SetGoal, 'set_goal')
        self.get_logger().info('HMI node started (scaffold)')

    def navigate_to_target(self, x, y, theta=0.0):
        """Set a navigation goal (map-frame coordinates) via /set_goal."""
        if not self.set_goal_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('set_goal service not available')
            return False
        req = SetGoal.Request()
        req.goal.x = x
        req.goal.y = y
        req.goal.theta = theta
        req.goal.frame_id = 'map'
        req.goal.source = 'hmi'
        future = self.set_goal_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        if future.result() is not None:
            self.get_logger().info(f'set_goal: {future.result().message}')
            return future.result().success
        self.get_logger().warn('set_goal timed out')
        return False


def main(args=None):
    rclpy.init(args=args)
    node = HmiNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()