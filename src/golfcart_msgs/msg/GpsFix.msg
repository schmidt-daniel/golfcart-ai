# GPS fix from the gps_node.
# Provides coarse global position (WGS84).
float64 latitude_deg
float64 longitude_deg
float64 altitude_m
float64 speed_mps
float64 heading_rad
bool valid
uint8 fix_type
uint8 satellites
float32 hdop
float32 vdop
float32 pdop
builtin_interfaces/Time timestamp