# Dead-reckoning fallback status from dead_reckoning_node.
# Informs the operator how long the trolley can keep driving without GPS.
string state            # OK / DEGRADED / DRIVING_DR / DR_BUDGET_EXCEEDED
float64 budget_time_s   # configured max DR time (s)
float64 budget_distance_m  # configured max DR distance (m)
float64 used_time_s     # DR time used so far (s)
float64 used_distance_m # DR distance travelled so far (m)
float64 remaining_time_s    # budget remaining (s)
float64 remaining_distance_m # budget remaining (m)
builtin_interfaces/Time timestamp