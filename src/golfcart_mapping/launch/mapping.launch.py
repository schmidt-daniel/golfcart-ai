#!/usr/bin/env python3
"""Launch the SLAM mapping pipeline.

Runs slam_toolbox (sync_slam_toolbox_node) to build a per-course occupancy grid
from the LiDAR /scan and the odom->base_link TF2.

Usage:
  ros2 launch golfcart_mapping mapping.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('golfcart_mapping')
    slam_params = os.path.join(pkg_share, 'config', 'mapper_params_online_async.yaml')

    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='false',
        description='Use simulation (Gazebo) time')

    use_sim_time = LaunchConfiguration('use_sim_time')

    slam = Node(
        package='slam_toolbox',
        executable='sync_slam_toolbox_node',
        name='slam_toolbox',
        output='screen',
        parameters=[slam_params],
        remappings=[('/scan', '/scan')],
    )

    return LaunchDescription([
        use_sim_time_arg,
        slam,
    ])