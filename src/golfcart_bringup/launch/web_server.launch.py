#!/usr/bin/env python3
"""Launch only the web teleop servers (rosbridge + HTTP web server).

This is the teleop-only companion to core.launch.py. It exposes the ROS 2 graph
over WebSocket (rosbridge, port 9090) and serves the web teleop page (port 8080).
The control pipeline itself is launched separately by core.launch.py.

Usage:
  ros2 launch golfcart_bringup web_server.launch.py
"""

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # rosbridge_server exposes ROS 2 over WebSocket (port 9090).
    rosbridge = Node(
        package='rosbridge_server',
        executable='rosbridge_websocket',
        name='rosbridge_websocket',
        output='screen',
    )

    # HTTP server serving the web teleop page (port 8080).
    web_server = Node(
        package='golfcart_teleop',
        executable='web_teleop_server',
        name='web_teleop_server',
        output='screen',
    )

    return LaunchDescription([
        rosbridge,
        web_server,
    ])