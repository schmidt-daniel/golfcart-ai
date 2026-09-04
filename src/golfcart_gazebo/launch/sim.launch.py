#!/usr/bin/env python3
"""Launch the golf cart in a simulated course (gz-sim).

Starts:
  - gz-sim server + GUI with the course world
  - robot_state_publisher (publishes TF from the URDF)
  - controller_manager with diff_drive_controller + joint_state_broadcaster
  - ros_gz_bridge bridging sensor topics (LiDAR/IMU/GPS/camera) to ROS

Usage:
  ros2 launch golfcart_gazebo sim.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, SetEnvironmentVariable, TimerAction
from launch.conditions import UnlessCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('golfcart_gazebo')

    # ---- Arguments ----
    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='true',
        description='Use simulation (Gazebo) time')
    headless_arg = DeclareLaunchArgument(
        'headless', default_value='false',
        description='Run gz-sim without the GUI (for CI/testing)')

    use_sim_time = LaunchConfiguration('use_sim_time')
    headless = LaunchConfiguration('headless')

    # ---- Load the gazebo URDF model ----
    # robot_state_publisher publishes TF from this URDF. We use the gazebo
    # xacro (not the clean URDF) because it carries the <ros2_control> tag
    # that the diff_drive_controller needs to import joint limiters. The
    # TF frames are unchanged since it includes the base golfcart model.
    xacro_file = os.path.join(pkg_share, 'urdf', 'golfcart.gazebo.xacro')
    robot_desc = os.popen(f'xacro {xacro_file}').read()

    # ---- Prepare a world copy with absolute plugin-param paths ----
    # gz-sim does not resolve $(find <pkg>) inside plugin <parameters> strings
    # (the literal string is passed to rclcpp). We substitute the installed
    # package share path so the gz_ros2_control params file is found.
    world_src = os.path.join(pkg_share, 'worlds', 'course.sdf')
    with open(world_src) as f:
        world_text = f.read()
    world_text = world_text.replace(
        '$(find golfcart_gazebo)', pkg_share)
    world_abs = os.path.join(pkg_share, 'worlds', 'course_generated.sdf')
    with open(world_abs, 'w') as f:
        f.write(world_text)

    # ---- gz-sim server ----
    # GZ_SIM_SYSTEM_PLUGIN_PATH lets gz-sim find gz_ros2_control's plugin lib.
    set_plugin_path = SetEnvironmentVariable(
        name='GZ_SIM_SYSTEM_PLUGIN_PATH',
        value='/opt/ros/' + os.environ.get('ROS_DISTRO', 'lyrical') + '/lib',
    )
    gz_server = ExecuteProcess(
        cmd=[
            'gz', 'sim', '-s', '-r',
            world_abs,
        ],
        output='screen',
    )

    # ---- gz-sim GUI (unless headless) ----
    gz_gui = ExecuteProcess(
        cmd=['gz', 'sim', '-g'],
        output='screen',
        condition=UnlessCondition(headless),
    )

    # ---- robot_state_publisher ----
    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'robot_description': robot_desc,
            'use_sim_time': use_sim_time,
        }],
    )

    # ---- Spawn controllers against the gz_ros2_control controller_manager ----
    # The gz_ros2_control plugin (embedded in the world SDF) creates its own
    # controller_manager; we only need to spawn the controllers. We pass the
    # controller params file explicitly so the diff_drive_controller receives
    # wheel_separation/wheel_radius before it declares its (0, max) range.
    controller_params = os.path.join(pkg_share, 'config', 'ros2_control.yaml')
    spawn_diff_drive = Node(
        package='controller_manager',
        executable='spawner',
        arguments=[
            'diff_drive_controller',
            '--param-file', controller_params,
            '--controller-ros-args',
            '--remap /diff_drive_controller/cmd_vel:=/cmd_vel_stamped --remap /diff_drive_controller/odom:=/odom',
        ],
        output='screen',
    )
    spawn_joint_state = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['joint_state_broadcaster', '--param-file', controller_params],
        output='screen',
    )

    # ---- cmd_vel converter (navigation Twist -> controller TwistStamped) ----
    # The diff_drive_controller in Lyrical expects geometry_msgs/TwistStamped on
    # /cmd_vel, but the nav stack publishes geometry_msgs/Twist. This small node
    # bridges /cmd_vel (Twist) to /cmd_vel_stamped (TwistStamped) with sim time.
    converter = Node(
        package='golfcart_gazebo',
        executable='cmd_vel_converter.py',
        output='screen',
        parameters=[{'use_sim_time': use_sim_time}],
    )

    # ---- ros_gz_bridge: sensor topics ----
    bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        name='gz_bridge',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
        }],
        arguments=[
            # Simulation clock (gz /clock -> ROS /clock)
            '/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock',
            # LiDAR (gz /lidar -> ROS /scan)
            '/lidar@sensor_msgs/msg/LaserScan[gz.msgs.LaserScan',
            # IMU
            '/imu@sensor_msgs/msg/Imu[gz.msgs.IMU',
            # GPS (gps_msgs/GPSFix <-> gz.msgs.NavSat)
            '/gps@gps_msgs/msg/GPSFix[gz.msgs.NavSat',
            # Camera
            '/camera/image@sensor_msgs/msg/Image[gz.msgs.Image',
        ],
        remappings=[
            ('/lidar', '/scan'),
        ],
    )

    return LaunchDescription([
        use_sim_time_arg,
        headless_arg,
        set_plugin_path,
        gz_server,
        gz_gui,
        robot_state_publisher,
        # Spawn controllers after gz_ros2_control's controller_manager is up.
        TimerAction(period=5.0, actions=[spawn_diff_drive]),
        TimerAction(period=5.0, actions=[spawn_joint_state]),
        converter,
        bridge,
    ])