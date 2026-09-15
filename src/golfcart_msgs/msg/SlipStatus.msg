# Wheel-slip / traction status from wheel_slip_node.
# Indicates when the wheels are spinning faster than the trolley is actually
# moving (loss of traction).
bool   slipping          # true when sustained slip is detected
float64 slip_ratio       # (wheel_speed - actual_speed) / wheel_speed (0-1)
float64 wheel_speed_mps  # wheel-derived forward speed (m/s)
float64 actual_speed_mps # fused-pose forward speed (m/s)
bool   valid
builtin_interfaces/Time timestamp