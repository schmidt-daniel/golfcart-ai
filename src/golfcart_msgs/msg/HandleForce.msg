# Handle push/brake force measured by the load cell in the handle unit.
# Published by the handle_gateway (calibrated on the Pi). Consumed by the
# push-assist controller in golfcart_behavior.

float32 force_n        # calibrated push force, N (positive = push, negative = brake)
bool valid
builtin_interfaces/Time timestamp