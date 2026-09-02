#!/usr/bin/env python3
"""Launch the web teleop pipeline.

Starts the control pipeline (odrive, motion, safety, battery) plus
rosbridge_server (WebSocket, port 9090) and the web teleop HTTP server
(port 8080).

Usage:
  ros2 launch golfcart_bringup web_teleop.launch.py implementation:=mock
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    impl_arg = DeclareLaunchArgument(
        'implementation', default_value='mock',
        description='Motor controller implementation: mock or odrive')

    implementation = LaunchConfiguration('implementation')

    odrive_node = Node(
        package='golfcart_odrive',
        executable='odrive_node',
        name='odrive_node',
        parameters=[{'implementation': implementation}],
        output='screen',
    )

    motion_controller = Node(
        package='golfcart_control',
        executable='motion_controller_node',
        name='motion_controller',
        output='screen',
    )

    safety_controller = Node(
        package='golfcart_control',
        executable='safety_controller_node',
        name='safety_controller',
        output='screen',
    )

    battery_node = Node(
        package='golfcart_odrive',
        executable='battery_node',
        name='battery_node',
        output='screen',
    )

    imu_node = Node(
        package='golfcart_imu',
        executable='imu_node',
        name='imu_node',
        output='screen',
    )

    gps_node = Node(
        package='golfcart_gps',
        executable='gps_node',
        name='gps_node',
        output='screen',
    )

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
        impl_arg,
        odrive_node,
        motion_controller,
        safety_controller,
        battery_node,
        rosbridge,
        web_server,
    ])