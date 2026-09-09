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

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
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
        parameters=[{'auto_radius_m': 30.0}],
    )

    return LaunchDescription([
        courses_dir_arg,
        registry_node,
        session_node,
    ])