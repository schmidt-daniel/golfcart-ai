#!/usr/bin/env python3
"""HMI node.

Renders the operator menu on the TFT display and translates joystick input into
menu navigation and feature commands.

Course flow:
  1. On startup, subscribe to /course/list (CourseList) for available courses.
  2. Course select screen -> operator picks a course -> /course/select.
  3. Tee select screen -> operator picks a tee -> /course/hole (tee_id).
  4. Hole screen -> renders the active hole layout (tee at bottom, green at
     top), hole number, distance for the selected tee, trolley position, and
     remaining distance to the hole (from /course/hole HoleSession).

Rendering is done to a PIL image (480x320) via hmi_draw. On hardware this is
displayed via luma.lcd; headless it is saved to disk / logged.
"""

import os

import rclpy
from rclpy.node import Node
from golfcart_msgs.srv import SetGoal, SummonTrigger, CourseSelect, HoleSelect
from golfcart_msgs.msg import (SummonStatus, GeofenceStatus, CourseList,
                               CourseMap, HoleSession)

from golfcart_teleop import hmi_draw


class HmiNode(Node):
    def __init__(self):
        super().__init__('hmi_node')
        self.declare_parameter('save_screens', False)
        self.save_screens = self.get_parameter('save_screens').value

        # ---- Clients ----
        self.set_goal_client = self.create_client(SetGoal, 'set_goal')
        self.summon_client = self.create_client(SummonTrigger, 'summon')
        self.course_select_client = self.create_client(CourseSelect, 'course/select')
        self.hole_select_client = self.create_client(HoleSelect, 'course/hole')

        # ---- Subscriptions ----
        self.summon_status_sub = self.create_subscription(
            SummonStatus, 'summon/status', self.on_summon_status, 10)
        self.geofence_status_sub = self.create_subscription(
            GeofenceStatus, 'geofence/status', self.on_geofence_status, 10)
        self.course_list_sub = self.create_subscription(
            CourseList, 'course/list', self.on_course_list, 10)
        self.course_map_sub = self.create_subscription(
            CourseMap, 'course/map', self.on_course_map, 10)
        self.hole_session_sub = self.create_subscription(
            HoleSession, 'course/hole', self.on_hole_session, 10)

        # ---- State ----
        self.courses = []          # [(id, name)]
        self.course_cursor = 0
        self.tees = []             # [(id, name)]
        self.tee_cursor = 0
        self.screen = 'course'     # course | tee | hole
        self.hole = None           # latest HoleSession
        self.course_map = None     # latest CourseMap

        self.get_logger().info('HMI node started')

    # ------------------------------------------------------------------
    # Callbacks
    # ------------------------------------------------------------------

    def on_summon_status(self, msg):
        self.get_logger().info(f'Summon: {msg.state} (distance {msg.distance_m:.1f} m)')
        if msg.state == 'ARRIVED':
            self.get_logger().info('Trolley has arrived — summon complete.')

    def on_geofence_status(self, msg):
        if msg.state in ('NEAR', 'CROSSED', 'OUT_OF_FIX'):
            self.get_logger().warn(
                f'Geofence: {msg.state} (distance to boundary '
                f'{msg.distance_to_boundary_m:.1f} m)')

    def on_course_list(self, msg):
        self.courses = [(c.id, c.name) for c in msg.courses]
        self.get_logger().info(f'Available courses: {[n for _, n in self.courses]}')
        if self.screen == 'course':
            self.render()

    def on_course_map(self, msg):
        self.course_map = msg
        # Populate tees from the CourseMap (parallel arrays).
        self.tees = list(zip(msg.tee_ids, msg.tee_names))
        self.get_logger().info(f'Course "{msg.course_name}" tees: {[n for _, n in self.tees]}')
        if self.screen == 'course' and self.tees:
            self.screen = 'tee'
            self.render()

    def on_hole_session(self, msg):
        self.hole = msg
        if self.screen in ('tee', 'course'):
            self.screen = 'hole'
        self.render()

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def render(self):
        if self.screen == 'course':
            screen = hmi_draw.render_course_list(self.courses, self.course_cursor)
        elif self.screen == 'tee':
            screen = hmi_draw.render_tee_select(self.tees, self.tee_cursor)
        elif self.screen == 'hole' and self.hole is not None:
            screen = self._render_hole()
        else:
            return
        self._show(screen)

    def _render_hole(self):
        h = self.hole
        tee_name = ''
        if self.course_map is not None:
            for tid, name in zip(self.course_map.tee_ids, self.course_map.tee_names):
                if tid == h.tee_id:
                    tee_name = name
                    break
        boundary = [(p.x, p.y) for p in h.boundary.points]
        features = [(f.type, f.x, f.y) for f in h.features]
        trolley = (h.trolley_x, h.trolley_y) if h.remaining_m > 0 or True else None
        return hmi_draw.render_hole(
            h.hole_number, h.hole_name, tee_name,
            h.distance_m, h.remaining_m, boundary, features, trolley)

    def _show(self, screen):
        if self.save_screens:
            os.makedirs('/tmp/hmi', exist_ok=True)
            path = f'/tmp/hmi/{self.screen}.png'
            screen.img.save(path)
            self.get_logger().info(f'HMI screen saved: {path}')
        else:
            self.get_logger().info(f'HMI screen: {self.screen}')

    # ------------------------------------------------------------------
    # Selection actions (called by joystick input / services)
    # ------------------------------------------------------------------

    def select_course(self, index=None):
        """Select a course by index (or the cursor) via /course/select."""
        if index is not None:
            self.course_cursor = index
        if not self.courses:
            return False
        cid = self.courses[self.course_cursor][0]
        if not self.course_select_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('course/select service not available')
            return False
        req = CourseSelect.Request()
        req.course_id = cid
        future = self.course_select_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        if future.result() is not None:
            self.get_logger().info(f'course/select: {future.result().message}')
            return future.result().success
        return False

    def select_tee(self, index=None):
        """Select a tee by index (or the cursor) via /course/hole."""
        if index is not None:
            self.tee_cursor = index
        if not self.tees:
            return False
        tid = self.tees[self.tee_cursor][0]
        if not self.hole_select_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('course/hole service not available')
            return False
        req = HoleSelect.Request()
        req.hole_number = 0  # auto-detect
        req.tee_id = tid
        future = self.hole_select_client.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        if future.result() is not None:
            self.get_logger().info(f'course/hole: {future.result().message}')
            return future.result().success
        return False

    def cursor_up(self):
        if self.screen == 'course' and self.courses:
            self.course_cursor = (self.course_cursor - 1) % len(self.courses)
            self.render()
        elif self.screen == 'tee' and self.tees:
            self.tee_cursor = (self.tee_cursor - 1) % len(self.tees)
            self.render()

    def cursor_down(self):
        if self.screen == 'course' and self.courses:
            self.course_cursor = (self.course_cursor + 1) % len(self.courses)
            self.render()
        elif self.screen == 'tee' and self.tees:
            self.tee_cursor = (self.tee_cursor + 1) % len(self.tees)
            self.render()

    def confirm(self):
        if self.screen == 'course':
            self.select_course()
        elif self.screen == 'tee':
            self.select_tee()

    def back(self):
        if self.screen == 'tee':
            self.screen = 'course'
            self.render()
        elif self.screen == 'hole':
            self.screen = 'tee'
            self.render()

    # ------------------------------------------------------------------
    # Legacy helpers (kept for compatibility)
    # ------------------------------------------------------------------

    def navigate_to_target(self, x, y, theta=0.0):
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