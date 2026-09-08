#!/usr/bin/env python3
"""Launch the geofence node.

Starts:
  - geofence_node (golfcart_geofence): /gps/fix -> /geofence/status

Usage:
  ros2 launch golfcart_geofence geofence.launch.py config_file:=hole5.yaml
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    config_file_arg = DeclareLaunchArgument(
        'config_file', default_value='hole5.yaml',
        description='Geofence boundary config file (in golfcart_geofence/config)')
    config_file = LaunchConfiguration('config_file')

    pkg_share = get_package_share_directory('golfcart_geofence')
    config_path = os.path.join(pkg_share, 'config', config_file.perform(None))

    geofence_node = Node(
        package='golfcart_geofence',
        executable='geofence_node',
        name='geofence_node',
        output='screen',
        parameters=[{'config_file': config_path}],
    )

    return LaunchDescription([
        config_file_arg,
        geofence_node,
    ])