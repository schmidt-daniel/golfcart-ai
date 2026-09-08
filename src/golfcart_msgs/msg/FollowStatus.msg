# Follow status from the follow_controller_node.
# Single source of truth for follow state (web + HMI render this).
bool active
string state        # IDLE / FOLLOWING / HOLDING / TARGET_LOST / STOPPED / ERROR
float64 distance_m
float64 lateral_offset_m
float32 confidence
builtin_interfaces/Time timestamp