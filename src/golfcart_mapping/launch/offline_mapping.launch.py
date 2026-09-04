#!/usr/bin/env python3
"""Launch offline SLAM for rebuilding a course map from a recorded bag.

Replays a recorded ROS 2 bag through slam_toolbox to build the occupancy grid
off-board (on a workstation, not on the cart). Use with the bag played via:

  ros2 bag play <bag> --clock --topics /scan /tf /tf_static

or the wrapper scripts/build_map_offline.sh.

Usage:
  ros2 launch golfcart_mapping offline_mapping.launch.py
  ros2 launch golfcart_mapping offline_mapping.launch.py save_map:=true
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('golfcart_mapping')
    slam_params = os.path.join(pkg_share, 'config', 'mapper_params_offline.yaml')

    save_map_arg = DeclareLaunchArgument(
        'save_map', default_value='false',
        description='Save the map via map_saver_cli after slam processes scans')
    save_map = LaunchConfiguration('save_map')

    slam = Node(
        package='slam_toolbox',
        executable='sync_slam_toolbox_node',
        name='slam_toolbox',
        output='screen',
        parameters=[slam_params],
    )

    map_saver = Node(
        package='nav2_map_server',
        executable='map_saver_cli',
        name='map_saver_cli',
        output='screen',
        condition=IfCondition(save_map),
    )

    return LaunchDescription([
        save_map_arg,
        slam,
        map_saver,
    ])