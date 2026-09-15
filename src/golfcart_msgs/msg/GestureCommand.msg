# A recognized gesture from gesture_recognition_node.
# Published on /gesture/command. The gesture_controller_node maps this to an
# action (summon / stop / follow / slow).
uint8 gesture    # 0=NONE, 1=SUMMON, 2=STOP, 3=FOLLOW, 4=SLOW
float32 confidence
builtin_interfaces/Time stamp