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
from golfcart_msgs.srv import SetGoal, SummonTrigger
from golfcart_msgs.msg import SummonStatus, GeofenceStatus


class HmiNode(Node):
    def __init__(self):
        super().__init__('hmi_node')
        self.set_goal_client = self.create_client(SetGoal, 'set_goal')
        self.summon_client = self.create_client(SummonTrigger, 'summon')
        self.summon_status_sub = self.create_subscription(
            SummonStatus, 'summon/status', self.on_summon_status, 10)
        self.geofence_status_sub = self.create_subscription(
            GeofenceStatus, 'geofence/status', self.on_geofence_status, 10)
        self.get_logger().info('HMI node started (scaffold)')

    def on_summon_status(self, msg):
        """Show summon status/arrival on the HMI."""
        self.get_logger().info(f'Summon: {msg.state} (distance {msg.distance_m:.1f} m)')
        if msg.state == 'ARRIVED':
            self.get_logger().info('Trolley has arrived — summon complete.')

    def on_geofence_status(self, msg):
        """Notify the operator when the geofence is hit (NEAR/CROSSED/OUT_OF_FIX)."""
        if msg.state in ('NEAR', 'CROSSED', 'OUT_OF_FIX'):
            self.get_logger().warn(
                f'Geofence: {msg.state} (distance to boundary '
                f'{msg.distance_to_boundary_m:.1f} m)')

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

    def summon_to(self, lat, lon, mode='PREDICT'):
        """Trigger summon to the operator's phone position via /summon."""
        if not self.summon_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('summon service not available')
            return False
        req = SummonTrigger.Request()
        req.lat = lat
        req.lon = lon
        req.mode = mode
        req.cancel = False
        future = self.summon_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        if future.result() is not None:
            self.get_logger().info(f'summon: {future.result().message}')
            return future.result().success
        self.get_logger().warn('summon timed out')
        return False

    def cancel_summon(self):
        """Cancel an active summon."""
        if not self.summon_client.wait_for_service(timeout_sec=2.0):
            return False
        req = SummonTrigger.Request()
        req.lat = 0.0
        req.lon = 0.0
        req.mode = 'CURRENT'
        req.cancel = True
        future = self.summon_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        return future.result() is not None and future.result().success


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