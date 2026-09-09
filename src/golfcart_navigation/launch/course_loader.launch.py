#!/usr/bin/env python3
"""Launch the course loader node.

Loads a course exported by the map editor (a Zip bundle) and publishes it as a
CourseMap on /course/map (transient-local) for the HMI, web app, and future
semantic layers.

Usage:
  ros2 launch golfcart_navigation course_loader.launch.py course_zip:=/path/to/golfcart-*.zip
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    course_zip_arg = DeclareLaunchArgument(
        'course_zip', default_value='',
        description='Path to the exported course Zip bundle (from the map editor)')
    course_zip = LaunchConfiguration('course_zip')

    course_loader_node = Node(
        package='golfcart_navigation',
        executable='course_loader_node',
        name='course_loader_node',
        output='screen',
        parameters=[{'course_zip': course_zip}],
    )

    return LaunchDescription([
        course_zip_arg,
        course_loader_node,
    ])