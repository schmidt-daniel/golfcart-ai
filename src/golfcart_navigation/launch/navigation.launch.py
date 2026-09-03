#!/usr/bin/env python3
"""Launch the Nav2 navigation stack + navigation bridge node.

Manual bringup equivalent of nav2_bringup (which is not packaged for Lyrical).
Launches the Nav2 servers (planner, controller, costmaps, bt_navigator,
velocity_smoother) plus the golfcart navigation_node bridge.

Usage:
  ros2 launch golfcart_navigation navigation.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('golfcart_navigation')

    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='false',
        description='Use simulation (Gazebo) time')
    use_sim_time = LaunchConfiguration('use_sim_time')

    # Config files
    planner_cfg = os.path.join(pkg_share, 'config', 'planner_server.yaml')
    controller_cfg = os.path.join(pkg_share, 'config', 'controller_server.yaml')
    global_costmap_cfg = os.path.join(pkg_share, 'config', 'global_costmap.yaml')
    local_costmap_cfg = os.path.join(pkg_share, 'config', 'local_costmap.yaml')
    bt_cfg = os.path.join(pkg_share, 'config', 'bt_navigator.yaml')
    smoother_cfg = os.path.join(pkg_share, 'config', 'velocity_smoother.yaml')

    # Nav2 servers
    planner = Node(
        package='nav2_planner',
        executable='planner_server',
        name='planner_server',
        output='screen',
        parameters=[planner_cfg],
    )

    controller = Node(
        package='nav2_controller',
        executable='controller_server',
        name='controller_server',
        output='screen',
        parameters=[controller_cfg],
    )

    bt_navigator = Node(
        package='nav2_bt_navigator',
        executable='bt_navigator',
        name='bt_navigator',
        output='screen',
        parameters=[bt_cfg],
    )

    velocity_smoother = Node(
        package='nav2_velocity_smoother',
        executable='velocity_smoother',
        name='velocity_smoother',
        output='screen',
        parameters=[smoother_cfg],
    )

    # Costmaps (global + local) - launched via the nav2_costmap_2d node
    global_costmap = Node(
        package='nav2_costmap_2d',
        executable='nav2_costmap_2d',
        name='global_costmap',
        output='screen',
        parameters=[global_costmap_cfg],
    )

    local_costmap = Node(
        package='nav2_costmap_2d',
        executable='nav2_costmap_2d',
        name='local_costmap',
        output='screen',
        parameters=[local_costmap_cfg],
    )

    # Golf cart navigation bridge node
    navigation_node = Node(
        package='golfcart_navigation',
        executable='navigation_node',
        name='navigation_node',
        output='screen',
    )

    return LaunchDescription([
        use_sim_time_arg,
        planner,
        controller,
        bt_navigator,
        velocity_smoother,
        global_costmap,
        local_costmap,
        navigation_node,
    ])