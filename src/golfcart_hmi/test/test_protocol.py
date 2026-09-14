"""Tests for the handle-unit serial protocol (protocol.py)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from golfcart_hmi import protocol as p


def test_encode_decode_roundtrip():
    """A message encoded then decoded yields the same type/payload/seq."""
    payload = bytes([0x01, 0x02, 0x03])
    frame = p.encode(p.DL_SCREEN_NAV, payload, seq=7)
    dec = p.Decoder()
    frames = dec.feed(frame)
    assert len(frames) == 1
    mtype, got_payload, seq = frames[0]
    assert mtype == p.DL_SCREEN_NAV
    assert got_payload == payload
    assert seq == 7


def test_byte_stuffing():
    """0xAA / 0x55 / 0xDB in the payload are escaped and recovered."""
    payload = bytes([0xAA, 0x55, 0xDB, 0x01])
    frame = p.encode(p.DL_STATE_UPDATE, payload, seq=1)
    dec = p.Decoder()
    frames = dec.feed(frame)
    assert len(frames) == 1
    assert frames[0][1] == payload


def test_decoder_handles_split_and_noise():
    """Frames split across feeds and leading noise are handled."""
    payload = bytes([0x42])
    frame = p.encode(p.DL_CONFIG, payload, seq=3)
    dec = p.Decoder()
    # Leading noise + split frame.
    frames = dec.feed(b'\x00\x01' + frame[:4])
    assert frames == []
    frames = dec.feed(frame[4:])
    assert len(frames) == 1
    assert frames[0][0] == p.DL_CONFIG
    assert frames[0][1] == payload


def test_crc_rejects_corruption():
    """A corrupted frame is dropped."""
    payload = bytes([0x01])
    frame = bytearray(p.encode(p.DL_HELLO, payload, seq=0))
    frame[-1] ^= 0xFF  # corrupt CRC
    dec = p.Decoder()
    frames = dec.feed(bytes(frame))
    assert frames == []


def test_state_value_encodings():
    """State values encode with the right widths."""
    # uint8
    assert p._encode_value(p.ST_BATTERY_PCT, 82) == bytes([82])
    # int16
    assert p._encode_value(p.ST_SPEED_MPS, 120) == b'\x78\x00'
    # uint16
    assert p._encode_value(p.ST_HOLE_DISTANCE_M, 380) == b'\x7c\x01'
    # int32
    assert p._encode_value(p.ST_GPS_LAT, 481234500) == b'\x44\x0e\xaf\x1c'
    # backlight is uint8
    assert p._encode_value(p.ST_BACKLIGHT, 200) == bytes([200])
    # steering assist is uint8
    assert p._encode_value(p.ST_STEERING_ASSIST, 1) == bytes([1])


def test_uplink_parsers():
    """Uplink payload parsers decode correctly."""
    assert p.parse_hello(bytes([1, 0x0F])) == (1, 0x0F)
    assert p.parse_joystick(b'\x00\x00\x00\x02\x01') == (0, 512, 1)
    assert p.parse_touch(b'\x64\x00\x2c\x01\x00') == (100, 300, 0)
    assert p.parse_menu_select(bytes([5])) == 5
    assert p.parse_force(b'\x23\x00') == 35
    assert p.parse_ack(bytes([3, 0])) == (3, 0)


def test_builders():
    """Downlink payload builders produce the expected bytes."""
    assert p.build_hello() == bytes([p.PROTOCOL_VERSION])
    assert p.build_screen_nav(p.SCREEN_COURSE) == bytes([p.SCREEN_COURSE, 0])
    assert p.build_config(1, 3) == bytes([1, 3])
    assert p.build_ack(9) == bytes([9, 0])


def test_build_boot_status():
    """DL_BOOT_STATUS payload: progress byte + truncated status text."""
    assert p.build_boot_status('Starting ROS', 10) == bytes([10]) + b'Starting ROS'
    # Text longer than 31 bytes is truncated.
    long_text = 'x' * 50
    payload = p.build_boot_status(long_text, 50)
    assert payload[0] == 50
    assert len(payload) == 1 + 31
    assert payload[1:] == b'x' * 31