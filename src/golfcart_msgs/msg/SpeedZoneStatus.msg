# Published by speed_zone_node on /speed_zone/status.
# Carries the active speed limit for the trolley's current position.
# The Safety Controller clamps requested velocity to limit_mps.

bool valid            # false until a course with speed zones is loaded
float64 limit_mps     # active max speed (m/s); -1 = no zone limit (use configured max)
string zone           # label of the limiting zone ("" if none)
float64 pose_age_s    # age of the fused pose used for the check
builtin_interfaces/Time timestamp