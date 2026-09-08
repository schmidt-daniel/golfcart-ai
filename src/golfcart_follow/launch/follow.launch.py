#!/usr/bin/env python3
"""Launch the Follow Me stack.

Starts:
  - person_detection_node (golfcart_lidar): /scan -> /person/target
  - obstacle_awareness_node (golfcart_follow): /scan -> /obstacles/awareness
  - follow_controller_node (golfcart_follow): /person/target -> /motion/request

Usage:
  ros2 launch golfcart_follow follow.launch.py
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='false',
        description='Use simulation (Gazebo) time')
    use_sim_time = LaunchConfiguration('use_sim_time')

    person_detection = Node(
        package='golfcart_lidar',
        executable='person_detection_node',
        name='person_detection_node',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            # Allow narrower clusters (a person's legs/torso may appear ~0.1-0.4m
            # wide to the LiDAR) and merge body parts with a wider cluster gap.
            'min_width_m': 0.1,
            'cluster_gap_m': 0.3,
        }],
    )

    obstacle_awareness = Node(
        package='golfcart_follow',
        executable='obstacle_awareness_node',
        name='obstacle_awareness_node',
        output='screen',
        parameters=[{'use_sim_time': use_sim_time}],
    )

    follow_controller = Node(
        package='golfcart_follow',
        executable='follow_controller_node',
        name='follow_controller_node',
        output='screen',
        parameters=[{'use_sim_time': use_sim_time}],
    )

    return LaunchDescription([
        use_sim_time_arg,
        person_detection,
        obstacle_awareness,
        follow_controller,
    ])