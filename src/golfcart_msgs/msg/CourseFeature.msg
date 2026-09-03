# A course feature (tee box, hole, exit point, etc.) on the golf course.
# Geo-referenced onto the metric map via the fused pose.
string type          # TEE_BOX / HOLE / EXIT_POINT / GREEN / FAIRWAY / HAZARD
string label          # e.g. "Hole 5", "Tee 5", "Exit A"
float64 x             # map-frame x
float64 y             # map-frame y
float64 theta         # orientation (rad)
uint32 hole_number     # hole number (0 if not applicable)
builtin_interfaces/Time timestamp