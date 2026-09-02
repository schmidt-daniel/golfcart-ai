#!/usr/bin/env python3
"""Launch the pipeline with keyboard teleop (for hardware-free testing)."""

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

    keyboard_node = Node(
        package='golfcart_teleop',
        executable='keyboard_teleop_node',
        name='keyboard_teleop_node',
        output='screen',
    )

    return LaunchDescription([
        impl_arg,
        odrive_node,
        motion_controller,
        safety_controller,
        keyboard_node,
    ])
