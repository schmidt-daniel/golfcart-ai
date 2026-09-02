# Desired motion from a behavior/teleop source.
# Represents intent, not permission. The Safety Controller decides whether it is allowed.
float32 linear_velocity_mps
float32 angular_velocity_radps
string source
uint8 priority
builtin_interfaces/Time timestamp
