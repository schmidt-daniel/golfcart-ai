#!/usr/bin/env python3
"""Launch the vision stack: gesture control + course segmentation.

Runs the camera-enabled nodes (gesture recognition/controller, segmentation).
These gate themselves on the camera + Coral capabilities from /capability/status,
so they degrade gracefully when the camera hardware is absent.

Tuning parameters are read from the central config file (config/golfcart.yaml).
"""

import os

import yaml
from launch import LaunchDescription
from launch_ros.actions import Node


def _load_config():
    path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
        'config', 'golfcart.yaml')
    path = os.path.abspath(path)
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return yaml.safe_load(f) or {}


def _node_params(cfg, name):
    params = dict(cfg.get('common', {}) or {})
    params.update(cfg.get(name, {}) or {})
    return params


def generate_launch_description():
    cfg = _load_config()

    camera = Node(
        package='golfcart_vision',
        executable='camera_node',
        name='camera_node',
        parameters=[_node_params(cfg, 'camera_node')],
        output='screen',
    )

    coral_pose = Node(
        package='golfcart_vision',
        executable='coral_pose_node.py',
        name='coral_pose_node',
        parameters=[_node_params(cfg, 'coral_pose_node')],
        output='screen',
    )

    gesture_recognition = Node(
        package='golfcart_vision',
        executable='gesture_recognition_node',
        name='gesture_recognition_node',
        parameters=[_node_params(cfg, 'gesture_recognition_node')],
        output='screen',
    )

    gesture_controller = Node(
        package='golfcart_vision',
        executable='gesture_controller_node',
        name='gesture_controller_node',
        parameters=[_node_params(cfg, 'gesture_controller_node')],
        output='screen',
    )

    segmentation = Node(
        package='golfcart_vision',
        executable='segmentation_node',
        name='segmentation_node',
        parameters=[_node_params(cfg, 'segmentation_node')],
        output='screen',
    )

    return LaunchDescription([
        camera,
        coral_pose,
        gesture_recognition,
        gesture_controller,
        segmentation,
    ])