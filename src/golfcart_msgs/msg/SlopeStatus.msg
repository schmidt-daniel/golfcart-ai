# Predicted terrain slope at the trolley's current position, projected onto
# the trolley's heading (published by slope_node).
# Used by the safety controller for predictive tip-over prevention.

float32 roll_rad      # predicted roll (trolley-relative), rad
float32 pitch_rad     # predicted pitch (trolley-relative), rad
float32 slope_deg     # terrain slope magnitude at this cell, deg
bool valid
builtin_interfaces/Time timestamp