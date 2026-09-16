#!/usr/bin/env python3
"""Launch the core hardware-facing control pipeline.

Runs the always-on base stack: motor control (odrive), motion controller,
safety controller, battery, IMU, GPS, LiDAR + obstacle detection, hill/rollback
behavior, push assist, mode, steering assist, and auto-shutdown. This is the
service that must always be running on the cart; higher-level services (teleop,
localization, mapping, navigation) build on top of it.

Tuning parameters are read from the central config file (config/golfcart.yaml).
Edit that file to tune behavior without recompiling.

Usage:
  ros2 launch golfcart_bringup core.launch.py implementation:=mock
  ros2 launch golfcart_bringup core.launch.py implementation:=odrive
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

    capability_node = Node(
        package='golfcart_system',
        executable='capability_node',
        name='capability_node',
        parameters=[_node_params(cfg, 'capability_node')],
        output='screen',
    )

    motion_controller = Node(
        package='golfcart_control',
        executable='motion_controller_node',
        name='motion_controller',
        parameters=[_node_params(cfg, 'motion_controller')],
        output='screen',
    )

    safety_controller = Node(
        package='golfcart_control',
        executable='safety_controller_node',
        name='safety_controller',
        parameters=[_node_params(cfg, 'safety_controller')],
        output='screen',
    )

    battery_node = Node(
        package='golfcart_odrive',
        executable='battery_node',
        name='battery_node',
        parameters=[_node_params(cfg, 'battery_node')],
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
        parameters=[_node_params(cfg, 'auto_shutdown_node')],
        output='screen',
    )

    lidar_node = Node(
        package='golfcart_lidar',
        executable='lidar_node',
        name='lidar_node',
        parameters=[_node_params(cfg, 'lidar_node')],
        output='screen',
    )

    obstacle_detection = Node(
        package='golfcart_lidar',
        executable='obstacle_detection_node',
        name='obstacle_detection_node',
        parameters=[_node_params(cfg, 'obstacle_detection_node')],
        output='screen',
    )

    hill_rollback = Node(
        package='golfcart_behavior',
        executable='hill_rollback_node',
        name='hill_rollback_node',
        parameters=[_node_params(cfg, 'hill_rollback_node')],
        output='screen',
    )

    push_assist = Node(
        package='golfcart_behavior',
        executable='push_assist_node',
        name='push_assist_node',
        parameters=[_node_params(cfg, 'push_assist_node')],
        output='screen',
    )

    mode_node = Node(
        package='golfcart_control',
        executable='mode_node',
        name='mode_node',
        parameters=[_node_params(cfg, 'mode_node')],
        output='screen',
    )

    wheel_slip = Node(
        package='golfcart_control',
        executable='wheel_slip_node',
        name='wheel_slip_node',
        parameters=[_node_params(cfg, 'wheel_slip_node')],
        output='screen',
    )

    steering_assist = Node(
        package='golfcart_follow',
        executable='steering_assist_node',
        name='steering_assist_node',
        parameters=[_node_params(cfg, 'steering_assist_node')],
        output='screen',
    )

    energy_saver = Node(
        package='golfcart_power',
        executable='energy_saver_node',
        name='energy_saver_node',
        parameters=[_node_params(cfg, 'energy_saver_node')],
        output='screen',
    )

    return LaunchDescription([
        impl_arg,
        odrive_node,
        capability_node,
        motion_controller,
        safety_controller,
        battery_node,
        imu_node,
        gps_node,
        auto_shutdown,
        energy_saver,
        lidar_node,
        obstacle_detection,
        hill_rollback,
        push_assist,
        mode_node,
        wheel_slip,
        steering_assist,
    ])