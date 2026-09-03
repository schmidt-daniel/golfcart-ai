#!/usr/bin/env python3
"""Launch the golf cart URDF model.

Loads the xacro model and publishes TF2 via robot_state_publisher, so the
frame tree (base_link -> {lidar_link, camera_link, imu_link, gps_link})is
available for localization, SLAM, and navigation.

Usage:
  ros2 launch golfcart_description display.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('golfcart_description')

    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='false',
        description='Use simulation (Gazebo) time')

    use_sim_time = LaunchConfiguration('use_sim_time')

    # Load the xacro model into a robot_description parameter.
    xacro_file = os.path.join(pkg_share, 'urdf', 'golfcart.urdf.xacro')
    robot_desc = os.popen(f'xacro {xacro_file}').read()

    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'robot_description': robot_desc,
            'use_sim_time': use_sim_time,
        }],
    )

    return LaunchDescription([
        use_sim_time_arg,
        robot_state_publisher,
    ])