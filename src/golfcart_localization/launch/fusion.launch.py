#!/usr/bin/env python3
"""Launch the sensor fusion / localization pipeline.

Runs the sensor_fusion_node (bridges custom IMU/GPS to standard messages) and
the robot_localization ekf_node (fuses wheel odometry + IMU + GPS).

Usage:
  ros2 launch golfcart_localization fusion.launch.py
"""

import os

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def _load_config():
    """Load the central tuning config (config/golfcart.yaml)."""
    path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
        'config', 'golfcart.yaml')
    path = os.path.abspath(path)
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return yaml.safe_load(f) or {}


def _node_params(cfg, name):
    """Return a node's params merged with the shared `common` section."""
    params = dict(cfg.get('common', {}) or {})
    params.update(cfg.get(name, {}) or {})
    return params


def generate_launch_description():
    cfg = _load_config()
    pkg_share = get_package_share_directory('golfcart_localization')
    ekf_config = os.path.join(pkg_share, 'config', 'ekf.yaml')

    sensor_fusion = Node(
        package='golfcart_localization',
        executable='sensor_fusion_node',
        name='sensor_fusion_node',
        output='screen',
    )

    ekf = Node(
        package='robot_localization',
        executable='ekf_node',
        name='ekf_filter_node',
        output='screen',
        parameters=[ekf_config],
    )

    quality = Node(
        package='golfcart_localization',
        executable='localization_quality_node',
        name='localization_quality_node',
        parameters=[_node_params(cfg, 'localization_quality_node')],
        output='screen',
    )

    dead_reckoning = Node(
        package='golfcart_localization',
        executable='dead_reckoning_node',
        name='dead_reckoning_node',
        parameters=[_node_params(cfg, 'dead_reckoning_node')],
        output='screen',
    )

    return LaunchDescription([
        sensor_fusion,
        ekf,
        quality,
        dead_reckoning,
    ])