# A detected person target from a person detector.
# Consumed by the follow controller. The source field identifies which
# detector produced the target (lidar, camera, fusion).
float64 distance_m
float64 lateral_offset_m
float64 relative_velocity_mps
float32 confidence
string source        # lidar / camera / fusion
bool valid
builtin_interfaces/Time timestamp