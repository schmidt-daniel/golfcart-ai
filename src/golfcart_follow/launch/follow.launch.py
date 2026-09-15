#!/usr/bin/env python3
"""Launch the Follow Me stack.

Starts:
  - person_detection_node (golfcart_lidar): /scan -> /person/target
  - obstacle_awareness_node (golfcart_follow): /scan -> /obstacles/awareness
  - follow_controller_node (golfcart_follow): /person/target -> /motion/request

Tuning parameters are read from the central config file (config/golfcart.yaml).

Usage:
  ros2 launch golfcart_follow follow.launch.py
"""

import os

import yaml
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
            **_node_params(cfg, 'person_detection_node'),
        }],
    )

    obstacle_awareness = Node(
        package='golfcart_follow',
        executable='obstacle_awareness_node',
        name='obstacle_awareness_node',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            **_node_params(cfg, 'obstacle_awareness_node'),
        }],
    )

    follow_controller = Node(
        package='golfcart_follow',
        executable='follow_controller_node',
        name='follow_controller_node',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            **_node_params(cfg, 'follow_controller_node'),
        }],
    )

    return LaunchDescription([
        use_sim_time_arg,
        person_detection,
        obstacle_awareness,
        follow_controller,
    ])