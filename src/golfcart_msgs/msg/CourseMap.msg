# Per-course map for the golf course.
# Stores the metric occupancy grid + slope layers + forbidden zones + map origin + course features.
# Used by Nav2 (costmap), the HMI, and future course-mapping semantic layer.

# Map origin (datum for georeferencing: lat/lon + rotation)
float64 origin_latitude_deg
float64 origin_longitude_deg
float64 origin_rotation_rad

# Course identity
string course_name
string course_id

# Tee taxonomy (parallel arrays; index i pairs tee_ids[i] with tee_names[i]).
# Tee IDs are flexible (colors, names, symbols, numbers).
string[] tee_ids
string[] tee_names

# Course features (tee boxes, holes, exit points)
CourseFeature[] features

# Occupancy grid (2D, from SLAM)
nav_msgs/OccupancyGrid grid

# Slope layers (roll/pitch, per-cell)
nav_msgs/OccupancyGrid roll_slope
nav_msgs/OccupancyGrid pitch_slope

# Forbidden zones (greens, tees, water, steep) as polygons
geometry_msgs/Polygon[] forbidden_zones

# Speed-limit zones (polygons) + their max speed, in the map frame.
# Parallel arrays: speed_zones[i] pairs with speed_zone_limits_mps[i] and
# speed_zone_labels[i]. A limit of -1 means "no limit" (use configured max).
geometry_msgs/Polygon[] speed_zones
float64[] speed_zone_limits_mps
string[] speed_zone_labels

builtin_interfaces/Time timestamp