# Navigation status from the navigation_node.
# Reports the current state of autonomous navigation.
string state        # IDLE / PLANNING / DRIVING / PAUSED / STOPPED / ARRIVED / ERROR
float32 progress     # 0.0 - 1.0 (fraction of path completed)
string error         # empty if no error; else a human-readable message
builtin_interfaces/Time timestamp