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

import math
import serial

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger
from golfcart_msgs.srv import CourseSelect, HoleSelect, DriveDistance, EndRound
from golfcart_msgs.msg import MotionRequest, BatteryState, GpsFix, ImuData
from golfcart_msgs.msg import ObstacleState, GeofenceStatus, SpeedZoneStatus
from golfcart_msgs.msg import SlopeStatus, NavigationStatus, HoleSession, CourseList, CourseMap
from golfcart_msgs.msg import HandleForce, ModeState, AssistConfig, RangeStatus, SlipStatus
from golfcart_msgs.msg import CapabilityStatus, SegmentationStatus
from nav_msgs.msg import Odometry

from golfcart_hmi import protocol as p


def _draw_line(px, x0, y0, x1, y1, color, w, h):
    """Bresenham line on a PIL pixel map, clipped to (w, h)."""
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        if 0 <= x0 < w and 0 <= y0 < h:
            px[x0, y0] = color
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


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
        self.mode_pub = self.create_publisher(ModeState, 'mode/state', 10)
        self.assist_pub = self.create_publisher(AssistConfig, 'assist/config', 10)

        # ---- Assist state (from the HMI Assist screen) ----
        self.steering_assist_enabled = True
        self.push_assist_enabled = True
        self.assist_level = 3

        # ---- Clients ----
        self.enable_client = self.create_client(Trigger, 'safety/enable')
        self.stop_client = self.create_client(Trigger, 'safety/stop')
        self.course_select_client = self.create_client(CourseSelect, 'course/select')
        self.hole_select_client = self.create_client(HoleSelect, 'course/hole')
        self.drive_distance_client = self.create_client(DriveDistance, 'drive_distance')
        self.end_round_client = self.create_client(EndRound, 'end_round')

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
        self.range_sub = self.create_subscription(
            RangeStatus, 'range/status', self.on_range, 10)
        self.slip_sub = self.create_subscription(
            SlipStatus, 'slip/status', self.on_slip, 10)
        self.cap_sub = self.create_subscription(
            CapabilityStatus, 'capability/status', self.on_capability, 10)
        self.seg_sub = self.create_subscription(
            SegmentationStatus, 'segmentation/status', self.on_segmentation, 10)
        self.odom_sub = self.create_subscription(
            Odometry, 'odometry/filtered', self.on_odometry, 10)

        # ---- Screen state (for MENU_SELECT interpretation) ----
        self.screen = p.SCREEN_SPLASH
        self.courses = []          # [(id, name)]
        self.tees = []             # [(id, name)]

        # ---- Map view state ----
        self.map_available = False
        self.map_x = 0.0           # trolley map x (m)
        self.map_y = 0.0           # trolley map y (m)
        self.map_heading = 0.0     # trolley heading (rad)
        self._map_sent = False     # whether the current map bitmap was sent

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
        elif self.screen == p.SCREEN_ASSIST:
            self._menu_assist(item)
        elif self.screen == p.SCREEN_DRIVE_DIST:
            self._menu_drive_dist(item)
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
        # Main menu items: MAP, DRIVE DIST, MODE, ASSIST, ENERGY, CHANGE HOLE,
        # SELECT COURSE, WIFI, END ROUND, DEBUG, SHUTDOWN.
        if item == 0:      # MAP
            self._nav(p.SCREEN_MAP)
        elif item == 1:    # DRIVE DIST
            self._nav(p.SCREEN_DRIVE_DIST)
        elif item == 2:    # MODE
            self._nav(p.SCREEN_MODE)
        elif item == 3:    # ASSIST
            self._nav(p.SCREEN_ASSIST)
        elif item == 4:    # ENERGY
            self._nav(p.SCREEN_ENERGY)
        elif item == 5:    # CHANGE HOLE
            self._nav(p.SCREEN_CHANGE_HOLE)
        elif item == 6:    # SELECT COURSE
            self._nav(p.SCREEN_COURSE)
        elif item == 7:    # WIFI
            self._nav(p.SCREEN_WIFI)
        elif item == 8:    # END ROUND
            self._call_end_round()
        elif item == 9:    # DEBUG
            self._nav(p.SCREEN_DEBUG)
        elif item == 10:   # SHUTDOWN
            self.get_logger().info('SHUTDOWN requested (not wired)')

    def _menu_drive_dist(self, item):
        # item 0..4 = 10/20/30/40/50 m; 5 = Cancel; 6 = Main Menu.
        if item == 6:
            self._nav(p.SCREEN_MENU)
            return
        if item == 5:
            self._call_drive_distance(0.0, cancel=True)
            return
        dists = [10, 20, 30, 40, 50]
        if 0 <= item < len(dists):
            self._call_drive_distance(float(dists[item]), cancel=False)

    def _menu_mode(self, item):
        # item 0..2 = mode; 3 = Main Menu.
        if item == 3:
            self._nav(p.SCREEN_MENU)
        else:
            # Mode select: 0=MANUAL, 1=FOLLOW, 2=AUTONOMOUS (matches ST_MODE).
            self._set_mode(item)

    def _set_mode(self, mode):
        """Publish the operating mode on /mode/state (ModeState)."""
        names = ['MANUAL', 'FOLLOW', 'AUTONOMOUS', 'TELEOP']
        msg = ModeState()
        msg.mode = mode
        msg.mode_name = names[mode] if mode < len(names) else 'UNKNOWN'
        msg.timestamp = self.get_clock().now().to_msg()
        self.mode_pub.publish(msg)
        self.get_logger().info(f'Mode set to {msg.mode_name}')

    def _menu_assist(self, item):
        # Assist screen items: 0=Push Assist, 1=Assist Level, 2=Hill Assist,
        # 3=Steering Assist, 4=Main Menu.
        if item == 0:      # Push Assist toggle
            self.push_assist_enabled = not self.push_assist_enabled
            self._publish_assist_config()
        elif item == 1:    # Assist Level (cycle 0-5)
            self.assist_level = (self.assist_level + 1) % 6
            self._publish_assist_config()
        elif item == 2:    # Hill Assist (not wired to a node yet)
            self.get_logger().info('Hill Assist toggle (not wired)')
        elif item == 3:    # Steering Assist toggle
            self.steering_assist_enabled = not self.steering_assist_enabled
            self._publish_assist_config()
        elif item == 4:    # Main Menu
            self._nav(p.SCREEN_MENU)

    def _publish_assist_config(self):
        """Publish the assist configuration on /assist/config (AssistConfig)."""
        msg = AssistConfig()
        msg.steering_assist_enabled = self.steering_assist_enabled
        msg.push_assist_enabled = self.push_assist_enabled
        msg.assist_level = self.assist_level
        msg.timestamp = self.get_clock().now().to_msg()
        self.assist_pub.publish(msg)
        # Reflect the steering-assist state back on the HMI Assist screen.
        self._send_state(p.ST_STEERING_ASSIST, 1 if self.steering_assist_enabled else 0)
        self.get_logger().info(
            f'Assist config: steering={self.steering_assist_enabled} '
            f'push={self.push_assist_enabled} level={self.assist_level}')

    def _menu_debug(self, item):
        # item 0..6 = debug view; 7 = Main Menu.
        if item == 7:
            self._nav(p.SCREEN_MENU)
        else:
            debug_screens = [p.SCREEN_DEBUG_SYSTEM, p.SCREEN_DEBUG_GPS,
                             p.SCREEN_DEBUG_LIDAR, p.SCREEN_DEBUG_CAMERA,
                             p.SCREEN_DEBUG_IMU, p.SCREEN_DEBUG_NAV,
                             p.SCREEN_SENSORS]
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

    def _call_drive_distance(self, distance_m, cancel=False):
        if not self.drive_distance_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('drive_distance service not available')
            return
        req = DriveDistance.Request()
        req.distance_m = distance_m
        req.cancel = cancel
        future = self.drive_distance_client.call_async(req)
        future.add_done_callback(
            lambda f: self.get_logger().info(
                f'drive_distance: {f.result().message if f.result() else "failed"}'))

    def _call_end_round(self):
        if not self.end_round_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn('end_round service not available')
            return
        req = EndRound.Request()
        future = self.end_round_client.call_async(req)
        future.add_done_callback(
            lambda f: self.get_logger().info(
                f'end_round: {f.result().message if f.result() else "failed"}'))

    def on_course_list(self, msg):
        self.courses = [(c.id, c.name) for c in msg.courses]
        self.get_logger().info(f'Courses: {[n for _, n in self.courses]}')

    def on_course_map(self, msg):
        self.tees = list(zip(msg.tee_ids, msg.tee_names))
        self.get_logger().info(f'Course "{msg.course_name}" tees: '
                               f'{[n for _, n in self.tees]}')
        self._render_map(msg)

    def on_odometry(self, msg):
        # Trolley position/heading in the map frame -> downlink to the map view.
        self.map_x = msg.pose.pose.position.x
        self.map_y = msg.pose.pose.position.y
        # Yaw from the quaternion.
        q = msg.pose.pose.orientation
        self.map_heading = math.atan2(
            2.0 * (q.w * q.z + q.x * q.y),
            1.0 - 2.0 * (q.y * q.y + q.z * q.z))
        self._send_state(p.ST_MAP_X, int(self.map_x * 100))
        self._send_state(p.ST_MAP_Y, int(self.map_y * 100))
        self._send_state(p.ST_MAP_HEADING, int(math.degrees(self.map_heading) * 10))

    def _render_map(self, msg):
        """Downsample the course map to a small RGB565 bitmap for the ESP32.

        The ESP32's 320x480 screen + limited RAM can't hold a full occupancy
        grid, so the Pi renders a compact top-down bitmap (forbidden zones +
        features) and sends it once per map load via DL_MAP_FRAME.
        """
        try:
            from PIL import Image as PILImage
            import struct as _struct
        except ImportError:
            self.get_logger().warn('Pillow not available; map view disabled')
            self.map_available = False
            self._send_state(p.ST_MAP_AVAILABLE, 0)
            return

        # Map area on the ESP32 is 296x380; use a compact render size.
        MAP_W, MAP_H = 148, 190
        img = PILImage.new('RGB', (MAP_W, MAP_H), (18, 27, 42))  # C_SURFACE
        px = img.load()

        # Compute the map bounds from all forbidden-zone points (fallback to
        # feature points if there are no zones).
        all_pts = []
        for zone in msg.forbidden_zones:
            all_pts.extend((pt.x, pt.y) for pt in zone.points)
        if not all_pts:
            all_pts = [(f.x, f.y) for f in msg.features]
        if not all_pts:
            self.map_available = False
            self._send_state(p.ST_MAP_AVAILABLE, 0)
            return
        xs = [pt[0] for pt in all_pts]
        ys = [pt[1] for pt in all_pts]
        minx, maxx = min(xs), max(xs)
        miny, maxy = min(ys), max(ys)
        if maxx - minx < 1e-6 or maxy - miny < 1e-6:
            self.map_available = False
            self._send_state(p.ST_MAP_AVAILABLE, 0)
            return

        def to_px(x, y):
            sx = int((x - minx) / (maxx - minx) * (MAP_W - 1))
            sy = int((1.0 - (y - miny) / (maxy - miny)) * (MAP_H - 1))
            return sx, sy

        # Draw forbidden zones as red polygons (fill + outline).
        for zone in msg.forbidden_zones:
            pts = [(pt.x, pt.y) for pt in zone.points]
            if len(pts) < 3:
                continue
            for yy in range(MAP_H):
                for xx in range(MAP_W):
                    wx = minx + (xx / (MAP_W - 1)) * (maxx - minx)
                    wy = maxy - (yy / (MAP_H - 1)) * (maxy - miny)
                    inside = False
                    j = len(pts) - 1
                    for i in range(len(pts)):
                        xi, yi = pts[i]
                        xj, yj = pts[j]
                        if ((yi > wy) != (yj > wy)) and \
                           (wx < (xj - xi) * (wy - yi) / (yj - yi) + xi):
                            inside = not inside
                        j = i
                    if inside:
                        px[xx, yy] = (248, 113, 113)  # red hazard
            for i in range(len(pts)):
                x0, y0 = to_px(*pts[i])
                x1, y1 = to_px(*pts[(i + 1) % len(pts)])
                _draw_line(px, x0, y0, x1, y1, (248, 113, 113), MAP_W, MAP_H)

        # Draw course features (holes/tees) as small cyan markers.
        for f in msg.features:
            sx, sy = to_px(f.x, f.y)
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    if 0 <= sx + dx < MAP_W and 0 <= sy + dy < MAP_H:
                        px[sx + dx, sy + dy] = (56, 189, 248)  # cyan

        # Convert to RGB565 (little-endian) for the ESP32.
        rgb565 = bytearray()
        for yy in range(MAP_H):
            for xx in range(MAP_W):
                r, g, b = px[xx, yy]
                val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                rgb565.append(val & 0xFF)
                rgb565.append((val >> 8) & 0xFF)

        self.map_available = True
        self._send_state(p.ST_MAP_AVAILABLE, 1)
        self._send(p.DL_MAP_FRAME, p.build_map_frame(MAP_W, MAP_H, bytes(rgb565)))
        self._map_sent = True
        self.get_logger().info(f'Map rendered {MAP_W}x{MAP_H} -> ESP32')

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

    def on_range(self, msg):
        self._send_state(p.ST_RANGE_M, int(msg.range_m))
        self._send_state(p.ST_RETURN_M, int(msg.return_m))
        state = {'OK': 0, 'CAUTION': 1, 'CRITICAL': 2}.get(msg.state, 0)
        self._send_state(p.ST_RANGE_STATE, state)
        self._send_state(p.ST_HOLES_REMAINING, int(msg.holes_remaining))

    def on_slip(self, msg):
        if msg.valid:
            self._send_state(p.ST_SLIP, 1 if msg.slipping else 0)

    def on_capability(self, msg):
        # Pack the capability bitmask (must match the ESP32 CAP_* bit order:
        # 0=LiDAR H, 1=LiDAR T, 2=GPS, 3=IMU, 4=Battery, 5=Camera, 6=Coral,
        # 7=ODrive).
        mask = 0
        mask |= (1 << 0) if msg.lidar_horizontal else 0
        mask |= (1 << 1) if msg.lidar_tilted else 0
        mask |= (1 << 2) if msg.gps else 0
        mask |= (1 << 3) if msg.imu else 0
        mask |= (1 << 4) if msg.battery else 0
        mask |= (1 << 5) if msg.camera else 0
        mask |= (1 << 6) if msg.coral else 0
        mask |= (1 << 7) if msg.odrive else 0
        self._send_state(p.ST_CAPABILITY, mask)

    def on_segmentation(self, msg):
        if msg.valid:
            self._send_state(p.ST_SEGMENTATION, 1 if msg.active else 0)

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