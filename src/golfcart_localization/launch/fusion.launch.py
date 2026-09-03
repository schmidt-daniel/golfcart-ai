#!/usr/bin/env python3
"""Launch the sensor fusion / localization pipeline.

Runs the sensor_fusion_node (bridges custom IMU/GPS to standard messages) and
the robot_localization ekf_node (fuses wheel odometry + IMU + GPS).

Usage:
  ros2 launch golfcart_localization fusion.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
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
        output='screen',
    )

    return LaunchDescription([
        sensor_fusion,
        ekf,
        quality,
    ])