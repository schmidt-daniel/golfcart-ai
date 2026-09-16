# Person re-identification status from person_reid_node.
# Indicates whether the follow-me operator has been re-acquired after a
# target loss, and the re-acquisition state.
bool   searching        # true when actively searching for the lost operator
bool   reacquired       # true when the operator was re-acquired
string state            # IDLE / SEARCHING / REACQUIRED / TIMEOUT
builtin_interfaces/Time timestamp