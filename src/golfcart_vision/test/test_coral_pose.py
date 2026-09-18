"""Tests for the Coral pose node's pure functions (resize + preprocess).

These don't require a Coral or a model — they validate the image
preprocessing pipeline that feeds the MoveNet model.
"""

import sys
from pathlib import Path

import numpy as np
from sensor_msgs.msg import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from golfcart_vision.coral_pose_node import (
    CoralPoseNode,
    _LEFT_SHOULDER,
    _LEFT_ELBOW,
    _LEFT_WRIST,
    _RIGHT_SHOULDER,
    _RIGHT_ELBOW,
    _RIGHT_WRIST,
)


def test_keypoint_indices():
    """MoveNet keypoint indices for the arms are correct."""
    assert _LEFT_SHOULDER == 5
    assert _LEFT_ELBOW == 6
    assert _LEFT_WRIST == 7
    assert _RIGHT_SHOULDER == 11
    assert _RIGHT_ELBOW == 12
    assert _RIGHT_WRIST == 13


def test_resize_shape_and_dtype():
    """Resize produces the target shape and preserves uint8."""
    img = np.zeros((480, 640, 3), dtype=np.uint8)
    img[100:200, 100:200] = 255
    out = CoralPoseNode._resize(img, 192, 192)
    assert out.shape == (192, 192, 3)
    assert out.dtype == np.uint8
    assert out.max() == 255  # the bright block survives


def test_preprocess_normalizes_to_minus_one():
    """A black image normalizes to -1.0 (MoveNet contract)."""
    msg = Image()
    msg.width = 640
    msg.height = 480
    msg.encoding = 'rgb8'
    msg.step = 640 * 3
    msg.data = bytes(np.zeros((480, 640, 3), dtype=np.uint8).tobytes())
    tensor = CoralPoseNode._preprocess(msg, 192, 192)
    assert tensor.shape == (1, 192, 192, 3)
    assert tensor.dtype == np.float32
    assert np.allclose(tensor, -1.0)


def test_preprocess_rejects_bad_encoding():
    """Non-rgb8 images are rejected."""
    msg = Image()
    msg.width = 640
    msg.height = 480
    msg.encoding = 'bgr8'
    msg.step = 640 * 3
    msg.data = bytes(np.zeros((480, 640, 3), dtype=np.uint8).tobytes())
    assert CoralPoseNode._preprocess(msg, 192, 192) is None