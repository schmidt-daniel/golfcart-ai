# The active hole session (published by course_session_node).
# Describes the current hole, the selected tee, and the trolley's progress.

uint32 hole_number     # 1-based hole number
string hole_name       # optional hole name
string tee_id          # selected tee (ID in CourseMap.tee_ids)
float64 distance_m     # hole distance for the selected tee (from hole distances)
float64 remaining_m    # remaining distance from trolley to the hole (green)

# Trolley position in map frame (meters).
float64 trolley_x
float64 trolley_y

# Tee box and green positions in map frame (meters).
float64 tee_x
float64 tee_y
float64 green_x
float64 green_y

# Hole boundary polygon (map-frame x/y points).
geometry_msgs/Polygon boundary

# Hole features (tee boxes, greens, hazards) in map frame.
CourseFeature[] features

builtin_interfaces/Time timestamp