# Summon status from the summon_node.
# Single source of truth for summon state (phone page + HMI render this).
bool active
string state        # IDLE / TRACKING / DRIVING / HOLDING / ARRIVED / TARGET_LOST / TIMEOUT / CANCELLED / OBSTACLE / ERROR
float64 target_lat
float64 target_lon
float64 distance_m
float32 progress     # 0.0 - 1.0 (fraction of route completed)
builtin_interfaces/Time timestamp