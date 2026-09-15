#!/usr/bin/env python3
"""Launch the geofence node.

Starts:
  - geofence_node (golfcart_geofence): /gps/fix -> /geofence/status

Usage:
  ros2 launch golfcart_geofence geofence.launch.py config_file:=hole5.yaml
"""

import os

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
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
    config_file_arg = DeclareLaunchArgument(
        'config_file', default_value='hole5.yaml',
        description='Geofence boundary config file (in golfcart_geofence/config)')
    config_file = LaunchConfiguration('config_file')

    course_file_arg = DeclareLaunchArgument(
        'course_file', default_value='',
        description='Course file (course.yaml) providing the map origin. '
                    'If empty, origin is read from the hole config (legacy).')
    course_file = LaunchConfiguration('course_file')

    pkg_share = get_package_share_directory('golfcart_geofence')
    config_path = os.path.join(pkg_share, 'config', config_file.perform(None))

    geofence_node = Node(
        package='golfcart_geofence',
        executable='geofence_node',
        name='geofence_node',
        output='screen',
        parameters=[{
            'config_file': config_path,
            'course_file': course_file,
            **_node_params(cfg, 'geofence_node'),
        }],
    )

    return LaunchDescription([
        config_file_arg,
        course_file_arg,
        geofence_node,
    ])