# Geofence status from the geofence_node.
# Single source of truth for geofence state (web/HMI notification + operator tooling).
bool      enabled         # false while temporarily disarmed (operator override)
string    state           # ARMED / NEAR / CROSSED / OUT_OF_FIX / DISARMED
bool      inside          # inside the boundary polygon
float64   distance_to_boundary_m   # signed: negative = outside the polygon
float64   latest_lat
float64   latest_lon
float64   last_fix_age_s
string    source          # e.g. "hole5"
builtin_interfaces/Time timestamp