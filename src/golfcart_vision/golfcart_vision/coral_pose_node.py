#!/usr/bin/env python3
"""Coral pose keypoint node.

Runs a MoveNet single-pose model on the Coral EdgeTPU and publishes the
shoulder/elbow/wrist keypoints used by gesture_recognition_node.

This node is optional. It only publishes when:
  - the Coral capability is present (/capability/status),
  - a pose model file is configured and exists,
  - the TFLite/EdgeTPU runtime is importable.

When any of those is missing, the node stays silent and gesture_recognition_node
falls back to its CPU skin-based estimator.

Model contract (MoveNet single-pose, 17 keypoints):
  - Input: 1xHxWx3 float32, normalized to [-1, 1].
  - Output: 1x1x17x3 float32, [y, x, score] per keypoint (y down, x right),
    normalized to [0, 1].
  - Keypoint indices: 5=left_shoulder, 6=left_elbow, 7=left_wrist,
    11=right_shoulder, 12=right_elbow, 13=right_wrist.
"""

import os

import numpy as np

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image

from golfcart_msgs.msg import CapabilityStatus, PoseKeypoints

# MoveNet keypoint indices (COCO 17-keypoint layout).
_LEFT_SHOULDER = 5
_LEFT_ELBOW = 6
_LEFT_WRIST = 7
_RIGHT_SHOULDER = 11
_RIGHT_ELBOW = 12
_RIGHT_WRIST = 13


class CoralPoseNode(Node):
    def __init__(self):
        super().__init__('coral_pose_node')
        self.declare_parameter('model_file', '')
        self.declare_parameter('input_topic', 'camera/image')
        self.declare_parameter('output_topic', 'pose/keypoints')
        self.declare_parameter('input_width', 192)
        self.declare_parameter('input_height', 192)
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
            self._input_details = self.interpreter.get_input_details()
            self._output_details = self.interpreter.get_output_details()
            self.get_logger().info('Loaded Coral pose model')
        except Exception as exc:  # noqa: BLE001
            self.get_logger().warning(f'Coral pose runtime unavailable: {exc}')

    def _on_capability(self, msg):
        self.coral_available = msg.coral

    @staticmethod
    def _preprocess(msg, input_width, input_height):
        """Convert a sensor_msgs/Image (rgb8) to the model's input tensor.

        Returns a float32 array shaped (1, H, W, 3) normalized to [-1, 1],
        or None if the image is malformed.
        """
        if msg.encoding != 'rgb8' or msg.step < msg.width * 3:
            return None
        if len(msg.data) < msg.step * msg.height:
            return None
        # Unpack the raw bytes into a HxWx3 array (row stride = step).
        frame = np.frombuffer(bytes(msg.data), dtype=np.uint8)
        frame = frame[:msg.step * msg.height].reshape(msg.height, msg.step, 1)
        frame = frame[:, :msg.width * 3, :].reshape(msg.height, msg.width, 3)
        # Resize to the model input size (bilinear via numpy).
        frame = CoralPoseNode._resize(frame, input_width, input_height)
        # Normalize to [-1, 1] (MoveNet contract).
        frame = (frame.astype(np.float32) / 127.5) - 1.0
        return np.expand_dims(frame, axis=0)

    @staticmethod
    def _resize(img, out_w, out_h):
        """Bilinear resize a HxWx3 uint8 image to (out_h, out_w, 3)."""
        in_h, in_w = img.shape[:2]
        ys = (np.arange(out_h) + 0.5) * (in_h / out_h) - 0.5
        xs = (np.arange(out_w) + 0.5) * (in_w / out_w) - 0.5
        ys = np.clip(ys, 0, in_h - 1)
        xs = np.clip(xs, 0, in_w - 1)
        y0 = ys.astype(np.int32)
        x0 = xs.astype(np.int32)
        y1 = np.minimum(y0 + 1, in_h - 1)
        x1 = np.minimum(x0 + 1, in_w - 1)
        # Weights shaped for broadcasting against (out_h, out_w, 3).
        wy = (ys - y0)[:, None, None]
        wx = (xs - x0)[None, :, None]
        out = (img[y0][:, x0] * (1 - wy) * (1 - wx)
               + img[y0][:, x1] * (1 - wy) * wx
               + img[y1][:, x0] * wy * (1 - wx)
               + img[y1][:, x1] * wy * wx)
        return out.astype(np.uint8)

    def _infer(self, tensor):
        """Run inference and return the (1, 1, 17, 3) keypoint tensor."""
        self.interpreter.set_tensor(self._input_details[0]['index'], tensor)
        self.interpreter.invoke()
        return self.interpreter.get_tensor(self._output_details[0]['index'])

    def _on_image(self, msg):
        if not self.coral_available or self.interpreter is None:
            return
        tensor = self._preprocess(msg, self.input_width, self.input_height)
        if tensor is None:
            return
        try:
            output = self._infer(tensor)
        except Exception as exc:  # noqa: BLE001
            self.get_logger().warning(f'Coral pose inference failed: {exc}')
            return
        # Output is (1, 1, 17, 3) = [y, x, score] per keypoint.
        kps = output[0, 0]  # (17, 3)
        scores = kps[:, 2]
        if scores[_LEFT_SHOULDER] < self.min_confidence or \
           scores[_RIGHT_SHOULDER] < self.min_confidence:
            return  # not confident enough; stay silent (CPU fallback)

        def pt(idx):
            return float(kps[idx, 1]), float(kps[idx, 0])  # (x, y), y down

        msg_out = PoseKeypoints()
        (msg_out.shoulder_l_x, msg_out.shoulder_l_y) = pt(_LEFT_SHOULDER)
        (msg_out.elbow_l_x, msg_out.elbow_l_y) = pt(_LEFT_ELBOW)
        (msg_out.wrist_l_x, msg_out.wrist_l_y) = pt(_LEFT_WRIST)
        (msg_out.shoulder_r_x, msg_out.shoulder_r_y) = pt(_RIGHT_SHOULDER)
        (msg_out.elbow_r_x, msg_out.elbow_r_y) = pt(_RIGHT_ELBOW)
        (msg_out.wrist_r_x, msg_out.wrist_r_y) = pt(_RIGHT_WRIST)
        msg_out.confidence = float(np.mean(scores))
        msg_out.valid = True
        msg_out.timestamp = self.get_clock().now().to_msg()
        self.keypoints_pub.publish(msg_out)


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