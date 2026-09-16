# Range estimate from range_estimator_node.
# Informs the operator how far the trolley can drive on the remaining charge.
string state            # OK / CAUTION / CRITICAL
float64 range_m         # estimated remaining range (m)
float64 remaining_m     # remaining hole distance (m)
float64 return_m        # estimated distance back to base (m)
bool   can_finish       # true if range covers remaining + return
float64 wh_per_m        # current learned energy consumption (Wh/m)
int32  holes_remaining  # estimated holes left on the remaining charge (-1 if unknown)
builtin_interfaces/Time timestamp