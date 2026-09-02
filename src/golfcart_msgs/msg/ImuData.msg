# IMU data from the imu_node.
# Uses the standard sensor_msgs/Imu for raw data; this message adds the
# inclination (roll/pitch) derived from the accelerometer, used for
# slope/tip-over detection.
float32 roll_rad
float32 pitch_rad
bool valid
builtin_interfaces/Time timestamp