# Power management state from the energy_saver_node.
# Describes the current power/sleep state of the cart.
#
# state:
#   0 = ACTIVE   - all systems running normally
#   1 = SLEEP    - energy-saving mode; non-essential systems reduced/suspended
#   2 = WAKING   - transitioning back to ACTIVE (IMU detected motion)
string state
builtin_interfaces/Time timestamp