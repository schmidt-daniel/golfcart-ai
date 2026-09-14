# Assist configuration, published by the handle_gateway on /assist/config.
# Carries the operator's assist toggles from the HMI assist screen.
# Consumed by the push-assist and steering-assist behavior nodes.

bool steering_assist_enabled   # steering assist on/off (HMI Assist screen)
bool push_assist_enabled       # push assist on/off
uint8 assist_level             # 0-5 assist level
builtin_interfaces/Time timestamp