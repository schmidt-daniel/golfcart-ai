# Hardware capability status, published by capability_node on /capability/status.
# Tracks which hardware is present (via heartbeat + valid flag) so downstream
# nodes can gate features that require specific sensors.
#
# A capability is "present" when the sensor's messages are arriving (heartbeat)
# AND reporting valid data. If a sensor stops publishing or reports valid=false
# for a timeout, the capability flips to absent.
#
# Manual driving (joystick) always works regardless of capabilities; only
# autonomous/assisted features gate on these.
bool lidar_horizontal    # horizontal LiDAR present (follow-me/obstacles)
bool lidar_tilted        # tilted LiDAR present (ditches/streams)
bool gps                 # GPS present (autonomous/recording)
bool imu                 # IMU present (tip-over/slope)
bool battery             # battery monitor present
bool camera              # Pi Camera present (vision/gesture)
bool coral               # Coral NPU present (vision compute)
bool odrive              # ODrive present (motion)
builtin_interfaces/Time stamp