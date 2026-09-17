# Hazard assessment from the tilted forward-facing LiDAR.
uint8 NONE=0
uint8 DROP_OFF=1
uint8 ROUGH_GROUND=2
uint8 OBSTRUCTION=3
uint8 NO_RETURN=4
uint8 BUNKER=5

uint8 hazard_type
bool hazard
bool valid
float32 confidence
float32 coverage
float32 nearest_range_m
builtin_interfaces/Time timestamp
