#!/usr/bin/env python3
"""Launch the course registry + session nodes.

Starts:
  - course_registry_node: scans courses/ for Zips, publishes /course/list,
    serves /course/select, publishes CourseMap + costmap on /map.
  - course_session_node: auto-detects the hole by GPS, publishes /course/hole.

Usage:
  ros2 launch golfcart_navigation course.launch.py courses_dir:=/path/to/courses
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
    courses_dir_arg = DeclareLaunchArgument(
        'courses_dir', default_value='courses',
        description='Directory of course Zips (exported by the map editor)')
    courses_dir = LaunchConfiguration('courses_dir')

    registry_node = Node(
        package='golfcart_navigation',
        executable='course_registry_node',
        name='course_registry_node',
        output='screen',
        parameters=[{'courses_dir': courses_dir}],
    )

    session_node = Node(
        package='golfcart_navigation',
        executable='course_session_node',
        name='course_session_node',
        output='screen',
        parameters=[_node_params(cfg, 'course_session_node')],
    )

    slope_node = Node(
        package='golfcart_navigation',
        executable='slope_node',
        name='slope_node',
        output='screen',
    )

    return LaunchDescription([
        courses_dir_arg,
        registry_node,
        session_node,
        slope_node,
    ])