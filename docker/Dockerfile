# ROS 2 Lyrical development image for the golf cart.
# Builds and tests the workspace without installing ROS on the host.
FROM ros:lyrical

# Install build/test tooling and common ROS 2 packages.
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-pip \
    python3-serial \
    git \
    && rm -rf /var/lib/apt/lists/*

# Install ROS 2 packages used by the workspace.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-${ROS_DISTRO}-rclcpp \
    ros-${ROS_DISTRO}-rclpy \
    ros-${ROS_DISTRO}-std-msgs \
    ros-${ROS_DISTRO}-std-srvs \
    ros-${ROS_DISTRO}-sensor-msgs \
    ros-${ROS_DISTRO}-geometry-msgs \
    ros-${ROS_DISTRO}-builtin-interfaces \
    ros-${ROS_DISTRO}-rosidl-default-generators \
    ros-${ROS_DISTRO}-rosidl-default-runtime \
    ros-${ROS_DISTRO}-ament-cmake-gtest \
    ros-${ROS_DISTRO}-launch \
    ros-${ROS_DISTRO}-launch-ros \
    && rm -rf /var/lib/apt/lists/*

# Source ROS in every shell.
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> /root/.bashrc

WORKDIR /workspace
