"""Handle-unit serial protocol (Raspberry Pi <-> ESP32).

Implements the framing, message types, and state-value tables from
`docs/handle-protocol.md`. Pure Python (no ROS deps) so it can be unit-tested
and shared between the Pi gateway and tooling.

Frame format (see spec §2.1):
    +--------+--------+--------+--------+--------+------------------+--------+
    | 0xAA   | 0x55   | TYPE   | LEN    | SEQ    | PAYLOAD[LEN]     | CRC16  |
    +--------+--------+--------+--------+--------+------------------+--------+

Byte stuffing: 0xAA / 0x55 in the payload are escaped with 0xDB + (byte ^ 0x20).
"""

from __future__ import annotations

import struct
from typing import List, Optional, Tuple

# --- Protocol version ---
PROTOCOL_VERSION = 1

# --- Start-of-frame markers ---
SOF1 = 0xAA
SOF2 = 0x55
ESC = 0xDB

# --- Message types (downlink: Pi -> ESP32) ---
DL_HELLO = 0x01
DL_SCREEN_NAV = 0x02
DL_STATE_UPDATE = 0x03
DL_DEBUG_SUMMARY = 0x04
DL_CONFIG = 0x05
DL_ACK = 0x06
DL_BOOT_STATUS = 0x07
DL_MAP_FRAME = 0x08

# --- Message types (uplink: ESP32 -> Pi) ---
UL_HELLO = 0x81
UL_JOYSTICK = 0x82
UL_TOUCH = 0x83
UL_MENU_SELECT = 0x84
UL_FORCE = 0x85
UL_ACK = 0x86

# --- State value IDs (downlink STATE_UPDATE payload: id + value) ---
ST_BATTERY_PCT = 0x01
ST_SPEED_MPS = 0x02
ST_SAFETY_STATE = 0x03
ST_MODE = 0x04
ST_GPS_LAT = 0x05
ST_GPS_LON = 0x06
ST_GPS_SPEED = 0x07
ST_GPS_SATS = 0x08
ST_IMU_ROLL = 0x09
ST_IMU_PITCH = 0x0A
ST_OBSTACLE = 0x0B
ST_OBSTACLE_NEAREST_M = 0x0C
ST_GEOFENCE = 0x0D
ST_SPEED_ZONE_LIMIT = 0x0E
ST_SLOPE_DEG = 0x0F
ST_NAV_STATUS = 0x10
ST_HOLE_NUMBER = 0x11
ST_HOLE_DISTANCE_M = 0x12
ST_HOLE_REMAINING_M = 0x13
ST_PUSH_FORCE_N = 0x14
ST_ASSIST_LEVEL = 0x15
ST_ASSIST_ENABLED = 0x16
ST_HILL_ASSIST_ENABLED = 0x17
ST_TIME_HHMM = 0x18
ST_BACKLIGHT = 0x19
ST_STEERING_ASSIST = 0x1A

# --- Energy dashboard state values (0x40-0x7F reserved) ---
ST_RANGE_M = 0x40
ST_RETURN_M = 0x41
ST_RANGE_STATE = 0x42

# --- Wheel-slip state value ---
ST_SLIP = 0x43

# --- Hardware capability bitmask (downlink) ---
ST_CAPABILITY = 0x44

# --- Course segmentation status (downlink) ---
ST_SEGMENTATION = 0x45

# --- Predictive range: holes remaining (downlink) ---
ST_HOLES_REMAINING = 0x46

# --- Map view state (downlink) ---
ST_MAP_X = 0x47          # trolley map x (int16, cm)
ST_MAP_Y = 0x48          # trolley map y (int16, cm)
ST_MAP_HEADING = 0x49    # trolley heading (int16, deg x10)
ST_MAP_AVAILABLE = 0x4A  # 1 when a course map is loaded

# --- Capabilities bitmask (uplink HELLO) ---
CAP_JOYSTICK = 0x01
CAP_TOUCH = 0x02
CAP_FORCE = 0x04
CAP_DISPLAY = 0x08

# --- Screen IDs (downlink SCREEN_NAV payload: screen id + optional arg) ---
SCREEN_SPLASH = 0x00
SCREEN_COURSE = 0x01
SCREEN_TEE = 0x02
SCREEN_HOLE = 0x03
SCREEN_MENU = 0x04
SCREEN_MODE = 0x05
SCREEN_ASSIST = 0x06
SCREEN_CHANGE_HOLE = 0x07
SCREEN_WIFI = 0x08
SCREEN_DEBUG = 0x09
SCREEN_DEBUG_SYSTEM = 0x0A
SCREEN_DEBUG_GPS = 0x0B
SCREEN_DEBUG_LIDAR = 0x0C
SCREEN_DEBUG_CAMERA = 0x0D
SCREEN_DEBUG_IMU = 0x0E
SCREEN_DEBUG_NAV = 0x0F
SCREEN_ENERGY = 0x10
SCREEN_SENSORS = 0x11
SCREEN_DRIVE_DIST = 0x12
SCREEN_MAP = 0x13


def _crc16(data: bytes) -> int:
    """CRC-16/CCITT (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _stuff(payload: bytes) -> bytes:
    """Escape 0xAA / 0x55 / 0xDB in the payload."""
    out = bytearray()
    for b in payload:
        if b in (SOF1, SOF2, ESC):
            out.append(ESC)
            out.append(b ^ 0x20)
        else:
            out.append(b)
    return bytes(out)


def _unstuff(data: bytes) -> Optional[bytes]:
    """Un-escape a stuffed payload. Returns None on a dangling escape."""
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b == ESC:
            if i + 1 >= len(data):
                return None
            out.append(data[i + 1] ^ 0x20)
            i += 2
        else:
            out.append(b)
            i += 1
    return bytes(out)


def encode(msg_type: int, payload: bytes, seq: int) -> bytes:
    """Encode a message into a framed byte string."""
    stuffed = _stuff(payload)
    header = bytes([SOF1, SOF2, msg_type, len(stuffed), seq & 0xFF])
    crc = _crc16(header[2:] + stuffed)  # over TYPE..PAYLOAD
    return header + stuffed + struct.pack('<H', crc)


class Decoder:
    """Incremental frame decoder.

    Feed bytes from the serial stream; yields complete (type, payload, seq)
    tuples as frames are recognized.
    """

    def __init__(self) -> None:
        self._buf = bytearray()
        self._seq = 0

    def feed(self, data: bytes) -> List[Tuple[int, bytes, int]]:
        self._buf.extend(data)
        frames: List[Tuple[int, bytes, int]] = []
        while True:
            frame = self._try_frame()
            if frame is None:
                break
            frames.append(frame)
        return frames

    def _try_frame(self) -> Optional[Tuple[int, bytes, int]]:
        # Find the start-of-frame marker.
        while len(self._buf) >= 2:
            if self._buf[0] == SOF1 and self._buf[1] == SOF2:
                break
            self._buf.pop(0)
        if len(self._buf) < 2:
            return None
        # Need TYPE, LEN, SEQ + payload + CRC.
        if len(self._buf) < 7:
            return None
        msg_type = self._buf[2]
        length = self._buf[3]
        seq = self._buf[4]
        total = 5 + length + 2
        if len(self._buf) < total:
            return None
        stuffed = bytes(self._buf[5:5 + length])
        crc_recv = struct.unpack('<H', bytes(self._buf[5 + length:5 + length + 2]))[0]
        crc_calc = _crc16(bytes(self._buf[2:5 + length]))
        del self._buf[:total]
        if crc_recv != crc_calc:
            return None  # corrupt frame; drop it
        payload = _unstuff(stuffed)
        if payload is None:
            return None
        return (msg_type, payload, seq)


# --- Payload builders (downlink) ---

def build_hello(version: int = PROTOCOL_VERSION) -> bytes:
    return bytes([version])


def build_screen_nav(screen_id: int, arg: int = 0) -> bytes:
    return bytes([screen_id, arg])


def build_state_update(state_id: int, value: int) -> bytes:
    """Encode a state value. value is a signed/unsigned int per the spec."""
    return bytes([state_id]) + _encode_value(state_id, value)


def _encode_value(state_id: int, value: int) -> bytes:
    if state_id in (ST_GPS_LAT, ST_GPS_LON):
        return struct.pack('<i', value)          # int32
    if state_id in (ST_SPEED_MPS, ST_IMU_ROLL, ST_IMU_PITCH,
                    ST_SPEED_ZONE_LIMIT, ST_SLOPE_DEG, ST_PUSH_FORCE_N):
        return struct.pack('<h', value)          # int16
    if state_id in (ST_GPS_SPEED, ST_OBSTACLE_NEAREST_M,
                    ST_HOLE_DISTANCE_M, ST_HOLE_REMAINING_M, ST_TIME_HHMM,
                    ST_RANGE_M, ST_RETURN_M, ST_MAP_X, ST_MAP_Y):
        return struct.pack('<H', value)          # uint16
    if state_id in (ST_MAP_HEADING,):
        return struct.pack('<h', value)          # int16
    return bytes([value & 0xFF])                 # uint8 (default)


def build_config(config_id: int, value: int) -> bytes:
    return bytes([config_id, value & 0xFF])


def build_boot_status(text: str, progress: int = 0) -> bytes:
    """Build a DL_BOOT_STATUS payload: progress (0-100) + short status text.

    The ESP32 shows this on the splash screen while the Pi boots. Text is
    truncated to 31 bytes (fits the splash label).
    """
    text = text.encode('utf-8', 'replace')[:31]
    return bytes([progress & 0xFF]) + text


def build_ack(acked_seq: int, status: int = 0) -> bytes:
    return bytes([acked_seq & 0xFF, status & 0xFF])


def build_map_frame(map_w: int, map_h: int, rgb565: bytes) -> bytes:
    """Build a DL_MAP_FRAME payload: map_w (u16 LE) + map_h (u16 LE) + bitmap.

    rgb565 is a packed little-endian RGB565 bitmap of map_w x map_h pixels.
    """
    return struct.pack('<HH', map_w, map_h) + rgb565


# --- Payload parsers (uplink) ---

def parse_hello(payload: bytes) -> Tuple[int, int]:
    """Return (version, capabilities)."""
    version = payload[0] if len(payload) > 0 else 0
    caps = payload[1] if len(payload) > 1 else 0
    return version, caps


def parse_joystick(payload: bytes) -> Tuple[int, int, int]:
    """Return (x, y, button)."""
    x, y = struct.unpack('<hh', payload[:4])
    btn = payload[4] if len(payload) > 4 else 0
    return x, y, btn


def parse_touch(payload: bytes) -> Tuple[int, int, int]:
    """Return (x, y, gesture)."""
    x, y = struct.unpack('<HH', payload[:4])
    gesture = payload[4] if len(payload) > 4 else 0
    return x, y, gesture


def parse_menu_select(payload: bytes) -> int:
    return payload[0] if payload else 0


def parse_force(payload: bytes) -> int:
    return struct.unpack('<h', payload[:2])[0] if len(payload) >= 2 else 0


def parse_ack(payload: bytes) -> Tuple[int, int]:
    acked = payload[0] if len(payload) > 0 else 0
    status = payload[1] if len(payload) > 1 else 0
    return acked, status