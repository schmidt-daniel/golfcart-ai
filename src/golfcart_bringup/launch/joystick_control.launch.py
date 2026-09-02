#!/usr/bin/env python3
"""Launch the full joystick-control pipeline.

Usage:
  ros2 launch golfcart_bringup joystick_control.launch.py implementation:=mock
  ros2 launch golfcart_bringup joystick_control.launch.py implementation:=odrive
  ros2 launch golfcart_bringup joystick_control.launch.py port:=/dev/ttyACM0
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    impl_arg = DeclareLaunchArgument(
        'implementation', default_value='mock',
        description='Motor controller implementation: mock or odrive')

    port_arg = DeclareLaunchArgument(
        'port', default_value='/dev/ttyACM0',
        description='Arduino serial port')

    implementation = LaunchConfiguration('implementation')
    port = LaunchConfiguration('port')

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

    auto_shutdown = Node(
        package='golfcart_power',
        executable='auto_shutdown_node',
        name='auto_shutdown_node',
        output='screen',
    )

    lidar_node = Node(
        package='golfcart_lidar',
        executable='lidar_node',
        name='lidar_node',
        output='screen',
    )

    obstacle_detection = Node(
        package='golfcart_lidar',
        executable='obstacle_detection_node',
        name='obstacle_detection_node',
        output='screen',
    )

    joystick_node = Node(
        package='golfcart_teleop',
        executable='arduino_joystick_node',
        name='arduino_joystick_node',
        parameters=[{'port': port}],
        output='screen',
    )

    return LaunchDescription([
        impl_arg,
        port_arg,
        odrive_node,
        motion_controller,
        safety_controller,
        battery_node,
        imu_node,
        gps_node,
        auto_shutdown,
        lidar_node,
        obstacle_detection,
        joystick_node,
    ])
