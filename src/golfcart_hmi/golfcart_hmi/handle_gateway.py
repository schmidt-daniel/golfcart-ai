#!/usr/bin/env python3
"""Handle-unit serial gateway node (Raspberry Pi side).

Bridges the ESP32 handle unit (display + joystick + touch + load cell) to the
ROS bus over a single USB serial link, using the protocol in `protocol.py`
(see `docs/handle-protocol.md`).

Downlink (Pi -> ESP32):
  - Subscribes to ROS state topics and pushes STATE_UPDATE / SCREEN_NAV /
    DEBUG_SUMMARY frames so the ESP32 can render them.

Uplink (ESP32 -> Pi):
  - Reads JOYSTICK / TOUCH / MENU_SELECT / FORCE frames and publishes them as
    ROS messages / calls services.

The ESP32 is the source of truth for input; the Pi is the source of truth for
state. All data handling/control logic lives here (the ESP32 only renders and
samples).
"""

import serial

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger
from golfcart_msgs.srv import CourseSelect, HoleSelect
from golfcart_msgs.msg import MotionRequest, BatteryState, GpsFix, ImuData
from golfcart_msgs.msg import ObstacleState, GeofenceStatus, SpeedZoneStatus
from golfcart_msgs.msg import SlopeStatus, NavigationStatus, HoleSession, CourseList, CourseMap
from golfcart_msgs.msg import HandleForce

from golfcart_hmi import protocol as p


class HandleGatewayNode(Node):
    def __init__(self):
        super().__init__('handle_gateway')
        self.declare_parameter('port', '/dev/ttyACM0')
        self.declare_parameter('baud', 460800)
        self.declare_parameter('max_linear', 1.0)
        self.declare_parameter('max_angular', 1.0)
        self.declare_parameter('deadzone', 0.05)
        self.declare_parameter('force_zero_offset', 0.0)
        self.declare_parameter('force_gain', 1.0)

        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value
        self.max_linear = self.get_parameter('max_linear').value
        self.max_angular = self.get_parameter('max_angular').value
        self.deadzone = self.get_parameter('deadzone').value
        self.force_zero = self.get_parameter('force_zero_offset').value
        self.force_gain = self.get_parameter('force_gain').value

        # ---- Serial ----
        self.decoder = p.Decoder()
        self._seq = 0
        try:
            self.ser = serial.Serial(port, baud, timeout=0.05)
            self.get_logger().info(f'Opened handle serial {port} @ {baud}')
        except serial.SerialException as e:
            self.get_logger().fatal(f'Failed to open {port}: {e}')
            raise

        # ---- Publishers (uplink -> ROS) ----
        self.motion_pub = self.create_publisher(MotionRequest, 'motion/request', 10)
        self.force_pub = self.create_publisher(HandleForce, 'handle/force', 10)

        # ---- Clients ----
        self.enable_client = self.create_client(Trigger, 'safety/enable')
        self.stop_client = self.create_client(Trigger, 'safety/stop')
        self.course_select_client = self.create_client(CourseSelect, 'course/select')
        self.hole_select_client = self.create_client(HoleSelect, 'course/hole')

        # ---- Subscriptions (ROS -> downlink) ----
        self.battery_sub = self.create_subscription(
            BatteryState, 'battery/state', self.on_battery, 10)
        self.gps_sub = self.create_subscription(
            GpsFix, 'gps/fix', self.on_gps, 10)
        self.imu_sub = self.create_subscription(
            ImuData, 'imu/data', self.on_imu, 10)
        self.obstacle_sub = self.create_subscription(
            ObstacleState, 'obstacles/state', self.on_obstacle, 10)
        self.geofence_sub = self.create_subscription(
            GeofenceStatus, 'geofence/status', self.on_geofence, 10)
        self.speed_zone_sub = self.create_subscription(
            SpeedZoneStatus, 'speed_zone/status', self.on_speed_zone, 10)
        self.slope_sub = self.create_subscription(
            SlopeStatus, 'slope/status', self.on_slope, 10)
        self.nav_sub = self.create_subscription(
            NavigationStatus, 'navigation/status', self.on_nav, 10)
        self.hole_sub = self.create_subscription(
            HoleSession, 'course/hole', self.on_hole, 10)
        self.course_list_sub = self.create_subscription(
            CourseList, 'course/list', self.on_course_list, 10)
        self.course_map_sub = self.create_subscription(
            CourseMap, 'course/map', self.on_course_map, 10)

        # ---- Screen state (for MENU_SELECT interpretation) ----
        self.screen = p.SCREEN_SPLASH
        self.courses = []          # [(id, name)]
        self.tees = []             # [(id, name)]

        # ---- Timers ----
        self.read_timer = self.create_timer(0.02, self.read_serial)   # 50 Hz
        self.heartbeat_timer = self.create_timer(1.0, self.send_heartbeat)
        # Push boot progress to the splash screen while ROS comes up.
        self._boot_stage = 0
        self.boot_timer = self.create_timer(1.0, self.send_boot_status)

        self.get_logger().info('Handle gateway started')

    # ------------------------------------------------------------------
    # Uplink: read serial, decode, dispatch
    # ------------------------------------------------------------------

    def read_serial(self):
        try:
            data = self.ser.read(256)
        except serial.SerialException as e:
            self.get_logger().error(f'Serial read error: {e}')
            return
        if not data:
            return
        for mtype, payload, seq in self.decoder.feed(data):
            self._dispatch(mtype, payload, seq)

    def _dispatch(self, mtype, payload, seq):
        if mtype == p.UL_HELLO:
            version, caps = p.parse_hello(payload)
            self.get_logger().info(f'Handle HELLO v{version}, caps=0x{caps:02x}')
            self._send(p.DL_HELLO, p.build_hello())
            # The Pi is up: leave the splash screen and show the course screen.
            self._nav(p.SCREEN_COURSE)
        elif mtype == p.UL_JOYSTICK:
            x, y, btn = p.parse_joystick(payload)
            self._on_joystick(x, y, btn)
        elif mtype == p.UL_TOUCH:
            x, y, gesture = p.parse_touch(payload)
            self.get_logger().info(f'Handle touch ({x},{y}) g={gesture}')
        elif mtype == p.UL_MENU_SELECT:
            item = p.parse_menu_select(payload)
            self._on_menu_select(item)
        elif mtype == p.UL_FORCE:
            raw = p.parse_force(payload)
            self._on_force(raw)
        elif mtype == p.UL_ACK:
            acked, status = p.parse_ack(payload)
            self.get_logger().info(f'Handle ACK seq={acked} status={status}')
        else:
            self.get_logger().warn(f'Unknown uplink type 0x{mtype:02x}')

    def _on_joystick(self, x, y, btn):
        # Map raw axes (-32768..32767) to -1..1.
        linear = (y / 32767.0) if y >= 0 else (y / 32768.0)
        angular = (x / 32767.0) if x >= 0 else (x / 32768.0)
        if abs(linear) < self.deadzone:
            linear = 0.0
        if abs(angular) < self.deadzone:
            angular = 0.0
        linear *= self.max_linear
        angular *= self.max_angular
        self._publish_motion(linear, angular, 'handle_joystick')

    def _on_force(self, raw):
        # Apply calibration (zero offset + gain) on the Pi.
        calibrated = (raw - self.force_zero) * self.force_gain
        # Publish the calibrated force for the push-assist controller.
        msg = HandleForce()
        msg.force_n = float(calibrated)
        msg.valid = True
        msg.timestamp = self.get_clock().now().to_msg()
        self.force_pub.publish(msg)

    def _publish_motion(self, linear, angular, source):
        req = MotionRequest()
        req.linear_velocity_mps = float(linear)
        req.angular_velocity_radps = float(angular)
        req.source = source
        req.priority = 1
        req.timestamp = self.get_clock().now().to_msg()
        self.motion_pub.publish(req)

    # ------------------------------------------------------------------
    # Menu select (ESP32 -> Pi): interpret item id by the active screen
    # ------------------------------------------------------------------

    def _on_menu_select(self, item):
        self.get_logger().info(f'Menu select screen=0x{self.screen:02x} item={item}')
        if self.screen == p.SCREEN_COURSE:
            self._menu_course(item)
        elif self.screen == p.SCREEN_TEE:
            self._menu_tee(item)
        elif self.screen == p.SCREEN_MENU:
            self._menu_main(item)
        elif self.screen == p.SCREEN_MODE:
            self._menu_mode(item)
        elif self.screen == p.SCREEN_DEBUG:
            self._menu_debug(item)
        else:
            self.get_logger().info(f'Menu select on screen 0x{self.screen:02x} '
                                   f'not handled yet (item={item})')

    def _menu_course(self, item):
        # item 0..n-1 = course index; last = Main Menu.
        if item < len(self.courses):
            cid = self.courses[item][0]
            self._call_course_select(cid)
        else:
            self._nav(p.SCREEN_MENU)

    def _menu_tee(self, item):
        # item 0..n-1 = tee index; last = Course Selection.
        if item < len(self.tees):
            tid = self.tees[item][0]
            self._call_hole_select(tid)
        else:
            self._nav(p.SCREEN_COURSE)

    def _menu_main(self, item):
        # Main menu items: MAP, MODE, ASSIST, CHANGE HOLE, SELECT COURSE,
        # WIFI, DEBUG, SHUTDOWN.
        if item == 0:      # MAP
            self._nav(p.SCREEN_HOLE)
        elif item == 1:    # MODE
            self._nav(p.SCREEN_MODE)
        elif item == 2:    # ASSIST
            self._nav(p.SCREEN_ASSIST)
        elif item == 3:    # CHANGE HOLE
            self._nav(p.SCREEN_CHANGE_HOLE)
        elif item == 4:    # SELECT COURSE
            self._nav(p.SCREEN_COURSE)
        elif item == 5:    # WIFI
            self._nav(p.SCREEN_WIFI)
        elif item == 6:    # DEBUG
            self._nav(p.SCREEN_DEBUG)
        elif item == 7:    # SHUTDOWN
            self.get_logger().info('SHUTDOWN requested (not wired)')

    def _menu_mode(self, item):
        # item 0..2 = mode; 3 = Main Menu.
        if item == 3:
            self._nav(p.SCREEN_MENU)
        else:
            self.get_logger().info(f'Mode select item={item} (not wired)')

    def _menu_debug(self, item):
        # item 0..5 = debug view; 6 = Main Menu.
        if item == 6:
            self._nav(p.SCREEN_MENU)
        else:
            debug_screens = [p.SCREEN_DEBUG_SYSTEM, p.SCREEN_DEBUG_GPS,
                             p.SCREEN_DEBUG_LIDAR, p.SCREEN_DEBUG_CAMERA,
                             p.SCREEN_DEBUG_IMU, p.SCREEN_DEBUG_NAV]
            if 0 <= item < len(debug_screens):
                self._nav(debug_screens[item])

    def _nav(self, screen_id):
        """Send SCREEN_NAV to the ESP32 and track the active screen."""
        self.screen = screen_id
        self._send(p.DL_SCREEN_NAV, p.build_screen_nav(screen_id))

    def _call_course_select(self, course_id):
        if not self.course_select_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('course/select service not available')
            return
        req = CourseSelect.Request()
        req.course_id = course_id
        future = self.course_select_client.call_async(req)
        future.add_done_callback(
            lambda f: self.get_logger().info(
                f'course/select: {f.result().message if f.result() else "failed"}'))

    def _call_hole_select(self, tee_id):
        if not self.hole_select_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('course/hole service not available')
            return
        req = HoleSelect.Request()
        req.hole_number = 0  # auto-detect
        req.tee_id = tee_id
        future = self.hole_select_client.call_async(req)
        future.add_done_callback(
            lambda f: self.get_logger().info(
                f'course/hole: {f.result().message if f.result() else "failed"}'))

    def on_course_list(self, msg):
        self.courses = [(c.id, c.name) for c in msg.courses]
        self.get_logger().info(f'Courses: {[n for _, n in self.courses]}')

    def on_course_map(self, msg):
        self.tees = list(zip(msg.tee_ids, msg.tee_names))
        self.get_logger().info(f'Course "{msg.course_name}" tees: '
                               f'{[n for _, n in self.tees]}')

    # ------------------------------------------------------------------
    # Downlink: ROS state -> serial
    # ------------------------------------------------------------------

    def on_battery(self, msg):
        if msg.valid:
            self._send_state(p.ST_BATTERY_PCT, int(msg.charge_percent))

    def on_gps(self, msg):
        if msg.valid:
            self._send_state(p.ST_GPS_LAT, int(msg.latitude_deg * 1e7))
            self._send_state(p.ST_GPS_LON, int(msg.longitude_deg * 1e7))
            self._send_state(p.ST_GPS_SPEED, int(msg.speed_mps * 100))

    def on_imu(self, msg):
        if msg.valid:
            self._send_state(p.ST_IMU_ROLL, int(msg.roll_rad * 1000))
            self._send_state(p.ST_IMU_PITCH, int(msg.pitch_rad * 1000))

    def on_obstacle(self, msg):
        if msg.valid:
            self._send_state(p.ST_OBSTACLE, 1 if msg.obstacle_in_zone else 0)

    def on_geofence(self, msg):
        state = {'OK': 0, 'NEAR': 1, 'CROSSED': 2, 'OUT_OF_FIX': 3}.get(msg.state, 0)
        self._send_state(p.ST_GEOFENCE, state)

    def on_speed_zone(self, msg):
        if msg.valid:
            self._send_state(p.ST_SPEED_ZONE_LIMIT, int(msg.limit_mps * 100))

    def on_slope(self, msg):
        if msg.valid:
            self._send_state(p.ST_SLOPE_DEG, int(msg.slope_deg * 100))

    def on_nav(self, msg):
        state = {'IDLE': 0, 'PLANNING': 1, 'DRIVING': 2, 'PAUSED': 3,
                 'ARRIVED': 4, 'ERROR': 5}.get(msg.state, 0)
        self._send_state(p.ST_NAV_STATUS, state)

    def on_hole(self, msg):
        self._send_state(p.ST_HOLE_NUMBER, int(msg.hole_number))
        self._send_state(p.ST_HOLE_DISTANCE_M, int(msg.distance_m))
        self._send_state(p.ST_HOLE_REMAINING_M, int(msg.remaining_m))

    # ------------------------------------------------------------------
    # Serial send helpers
    # ------------------------------------------------------------------

    def _send_state(self, state_id, value):
        self._send(p.DL_STATE_UPDATE, p.build_state_update(state_id, value))

    def _send(self, mtype, payload):
        frame = p.encode(mtype, payload, self._seq)
        self._seq = (self._seq + 1) & 0xFF
        try:
            self.ser.write(frame)
        except serial.SerialException as e:
            self.get_logger().error(f'Serial write error: {e}')

    def send_heartbeat(self):
        self._send(p.DL_ACK, p.build_ack(self._seq))

    def send_boot_status(self):
        """Push boot progress to the splash screen while ROS comes up.

        The ESP32 shows a splash screen until it receives DL_HELLO (sent when
        we get its UL_HELLO). Until then, report our startup progress so the
        user sees something is happening. Once the handle is ready (HELLO
        exchanged), stop sending.
        """
        if self.screen != p.SCREEN_SPLASH:
            return
        stages = [
            (10, 'Starting ROS'),
            (30, 'Loading gateway'),
            (50, 'Opening serial'),
            (70, 'Waiting for services'),
            (90, 'Almost ready'),
        ]
        if self._boot_stage < len(stages):
            progress, text = stages[self._boot_stage]
            self._boot_stage += 1
            self._send(p.DL_BOOT_STATUS, p.build_boot_status(text, progress))


def main(args=None):
    rclpy.init(args=args)
    node = HandleGatewayNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()