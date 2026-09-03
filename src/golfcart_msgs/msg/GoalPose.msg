# A navigation goal (target) for the trolley.
# Set via the /set_goal service (from web map or HMI).
# Coordinates are in the map frame (from georeference_node).
float64 x
float64 y
float64 theta
string frame_id
string source
builtin_interfaces/Time timestamp