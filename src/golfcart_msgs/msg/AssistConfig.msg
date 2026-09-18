# Assist configuration, published by the handle_gateway on /assist/config.
# Carries the operator's assist toggles from the HMI assist screen.
# Consumed by the push-assist, steering-assist, and hill-assist behavior nodes.

bool steering_assist_enabled   # steering assist on/off (HMI Assist screen)
bool push_assist_enabled       # push assist on/off
bool hill_assist_enabled       # hill assist on/off (HMI Assist screen)
uint8 assist_level             # 0-5 assist level
builtin_interfaces/Time timestamp