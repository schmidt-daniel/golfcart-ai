# Course segmentation status from segmentation_node.
# Indicates whether live course segmentation (fairway/rough/water/bunkers/etc.)
# is active and what classes it is detecting.
bool   active           # true when segmentation is running
bool   valid
uint8  class_count      # number of recognized course classes (0 if none)
float32 confidence      # average confidence of the latest frame (0-1)
builtin_interfaces/Time timestamp