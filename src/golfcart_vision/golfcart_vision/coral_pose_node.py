#!/usr/bin/env python3
"""Coral pose keypoint node.

Runs a MediaPipe-style pose model on the Coral EdgeTPU and publishes the
shoulder/elbow/wrist keypoints used by gesture_recognition_node.

This node is optional. It only publishes when:
  - the Coral capability is present (/capability/status),
  - a pose model file is configured and exists,
  - the TFLite/EdgeTPU runtime is importable.

When any of those is missing, the node stays silent and gesture_recognition_node
falls back to its CPU skin-based estimator.
"""

import os

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image

from golfcart_msgs.msg import CapabilityStatus, PoseKeypoints


class CoralPoseNode(Node):
    def __init__(self):
        super().__init__('coral_pose_node')
        self.declare_parameter('model_file', '')
        self.declare_parameter('input_topic', 'camera/image')
        self.declare_parameter('output_topic', 'pose/keypoints')
        self.declare_parameter('input_width', 224)
        self.declare_parameter('input_height', 224)
        self.declare_parameter('min_confidence', 0.5)

        self.model_file = self.get_parameter('model_file').value
        self.input_width = self.get_parameter('input_width').value
        self.input_height = self.get_parameter('input_height').value
        self.min_confidence = self.get_parameter('min_confidence').value

        self.coral_available = False
        self.interpreter = None
        self._load_interpreter()

        self.cap_sub = self.create_subscription(
            CapabilityStatus, 'capability/status', self._on_capability, 10)
        self.image_sub = self.create_subscription(
            Image, self.get_parameter('input_topic').value, self._on_image, 10)
        self.keypoints_pub = self.create_publisher(
            PoseKeypoints, self.get_parameter('output_topic').value, 10)

    def _load_interpreter(self):
        if not self.model_file or not os.path.exists(self.model_file):
            self.get_logger().warning(
                f'No pose model at {self.model_file!r}; Coral pose disabled')
            return
        try:
            from tflite_runtime.interpreter import Interpreter
            self.interpreter = Interpreter(model_path=self.model_file)
            self.interpreter.allocate_tensors()
            self.get_logger().info('Loaded Coral pose model')
        except Exception as exc:  # noqa: BLE001
            self.get_logger().warning(f'Coral pose runtime unavailable: {exc}')

    def _on_capability(self, msg):
        self.coral_available = msg.coral

    def _on_image(self, msg):
        if not self.coral_available or self.interpreter is None:
            return
        # Placeholder: real inference requires a concrete model + tensor
        # contract. This node currently only validates the runtime path.
        self.get_logger().debug('Coral pose inference not yet wired to a model')


def main(args=None):
    rclpy.init(args=args)
    node = CoralPoseNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()