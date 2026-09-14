# Operating mode of the trolley, published by mode_node on /mode/state.
# Single source of truth for the current mode (web + HMI + safety render this).
# The Safety Controller uses this to decide which safety stops apply:
#   - MANUAL: the operator has full control (no obstacle hard-stop; steering
#     assist nudges instead)
#   - FOLLOW / AUTONOMOUS / TELEOP: safety stops (e.g. obstacle) still apply

uint8 mode        # 0=MANUAL, 1=FOLLOW, 2=AUTONOMOUS, 3=TELEOP
string mode_name  # "MANUAL" / "FOLLOW" / "AUTONOMOUS" / "TELEOP"
builtin_interfaces/Time timestamp