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
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration, PythonExpression
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

    lidar_driver_arg = DeclareLaunchArgument(
        'lidar_driver', default_value='mock',
        description='LiDAR driver: mock or ldlidar')

    lidar_device_horizontal_arg = DeclareLaunchArgument(
        'lidar_device_horizontal', default_value='/dev/ttyUSB0',
        description='Serial device used by the horizontal ldlidar_ros2')

    lidar_device_tilted_arg = DeclareLaunchArgument(
        'lidar_device_tilted', default_value='/dev/ttyUSB1',
        description='Serial device used by the tilted ldlidar_ros2')

    gps_implementation_arg = DeclareLaunchArgument(
        'gps_implementation', default_value='mock',
        description='GPS implementation: mock or real (gpsd)')

    gps_device_arg = DeclareLaunchArgument(
        'gps_device', default_value='/dev/ttyUSB0',
        description='GPS device selected by gpsd')

    gpsd_host_arg = DeclareLaunchArgument(
        'gpsd_host', default_value='127.0.0.1',
        description='gpsd host')

    gpsd_port_arg = DeclareLaunchArgument(
        'gpsd_port', default_value='2947',
        description='gpsd TCP port')

    implementation = LaunchConfiguration('implementation')
    lidar_driver = LaunchConfiguration('lidar_driver')
    lidar_device_horizontal = LaunchConfiguration('lidar_device_horizontal')
    lidar_device_tilted = LaunchConfiguration('lidar_device_tilted')
    gps_implementation = LaunchConfiguration('gps_implementation')
    gps_device = LaunchConfiguration('gps_device')
    gpsd_host = LaunchConfiguration('gpsd_host')
    gpsd_port = LaunchConfiguration('gpsd_port')

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

    sensor_health_logger = Node(
        package='golfcart_system',
        executable='sensor_health_logger_node',
        name='sensor_health_logger',
        parameters=[_node_params(cfg, 'sensor_health_logger')],
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
        parameters=[{
            'implementation': gps_implementation,
            'device': gps_device,
            'gpsd_host': gpsd_host,
            'gpsd_port': gpsd_port,
        }],
        output='screen',
    )

    auto_shutdown = Node(
        package='golfcart_power',
        executable='auto_shutdown_node',
        name='auto_shutdown_node',
        parameters=[_node_params(cfg, 'auto_shutdown_node')],
        output='screen',
    )

    mock_lidar_node = Node(
        package='golfcart_lidar',
        executable='lidar_node',
        name='lidar_node',
        parameters=[_node_params(cfg, 'lidar_node')],
        condition=UnlessCondition(PythonExpression(["'", lidar_driver, "' == 'ldlidar'"])),
        output='screen',
    )

    ldlidar_horizontal_node = Node(
        package='ldlidar_ros2',
        executable='ldlidar_ros2_node',
        name='ldlidar_horizontal_node',
        parameters=[{
            'product_name': 'LDLiDAR_LD19',
            'laser_scan_topic_name': 'ldlidar/horizontal_scan',
            'point_cloud_2d_topic_name': 'ldlidar/horizontal_pointcloud2d',
            'frame_id': 'lidar_link',
            'port_name': lidar_device_horizontal,
            'serial_baudrate': 230400,
            'laser_scan_dir': True,
            'enable_angle_crop_func': False,
            'angle_crop_min': 135.0,
            'angle_crop_max': 225.0,
            'range_min': 0.1,
            'range_max': 12.0,
        }],
        condition=IfCondition(PythonExpression(["'", lidar_driver, "' == 'ldlidar'"])),
        output='screen',
    )

    ldlidar_tilted_node = Node(
        package='ldlidar_ros2',
        executable='ldlidar_ros2_node',
        name='ldlidar_tilted_node',
        parameters=[{
            'product_name': 'LDLiDAR_LD19',
            'laser_scan_topic_name': 'ldlidar/tilted_scan',
            'point_cloud_2d_topic_name': 'ldlidar/tilted_pointcloud2d',
            'frame_id': 'lidar_tilted_link',
            'port_name': lidar_device_tilted,
            'serial_baudrate': 230400,
            'laser_scan_dir': True,
            'enable_angle_crop_func': False,
            'angle_crop_min': 135.0,
            'angle_crop_max': 225.0,
            'range_min': 0.1,
            'range_max': 12.0,
        }],
        condition=IfCondition(PythonExpression(["'", lidar_driver, "' == 'ldlidar'"])),
        output='screen',
    )

    ldlidar_horizontal_bridge = Node(
        package='golfcart_lidar',
        executable='ldlidar_bridge_node',
        name='ldlidar_horizontal_bridge',
        parameters=[{
            'input_topic': 'ldlidar/horizontal_scan',
            'output_topic': 'scan',
            'output_frame': 'lidar_link',
            'blind_spot_center_rad': cfg.get('lidar_node', {}).get(
                'blind_spot_center_rad', 3.14159),
            'blind_spot_half_angle_rad': cfg.get('lidar_node', {}).get(
                'blind_spot_half_angle_rad', 0.0),
        }],
        condition=IfCondition(PythonExpression(["'", lidar_driver, "' == 'ldlidar'"])),
        output='screen',
    )

    ldlidar_tilted_bridge = Node(
        package='golfcart_lidar',
        executable='ldlidar_bridge_node',
        name='ldlidar_tilted_bridge',
        parameters=[{
            'input_topic': 'ldlidar/tilted_scan',
            'output_topic': 'scan_tilted',
            'output_frame': 'lidar_tilted_link',
        }],
        condition=IfCondition(PythonExpression(["'", lidar_driver, "' == 'ldlidar'"])),
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
        lidar_driver_arg,
        lidar_device_horizontal_arg,
        lidar_device_tilted_arg,
        gps_implementation_arg,
        gps_device_arg,
        gpsd_host_arg,
        gpsd_port_arg,
        odrive_node,
        capability_node,
        sensor_health_logger,
        motion_controller,
        safety_controller,
        battery_node,
        imu_node,
        gps_node,
        auto_shutdown,
        energy_saver,
        mock_lidar_node,
        ldlidar_horizontal_node,
        ldlidar_tilted_node,
        ldlidar_horizontal_bridge,
        ldlidar_tilted_bridge,
        obstacle_detection,
        hill_rollback,
        push_assist,
        mode_node,
        wheel_slip,
        steering_assist,
    ])