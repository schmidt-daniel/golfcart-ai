# HMI Display

The TFT display provides the user with live system information.

## Purpose

Show the operator real-time status while driving:

- current speed (linear velocity)
- battery state (voltage, charge %)
- safety state (READY / MOVING / STOPPED / FAULT)
- current operating mode (manual, follow me, etc.)
- warnings and fault information

## Data Sources

The HMI display consumes existing topics:

```text
/motor/state        → speed
/battery/state      → battery voltage, charge %
/safety/state       → safety state
```

## Node

A `hmi_node` subscribes to the above topics and renders them on the TFT
display. It is a non-critical HMI component (Python is appropriate).

The HMI must never bypass the Safety Controller. Displayed information is for
the operator only; it does not command motion.

## Future Enhancements

- Distance to green/pin (requires GPS + course data)
- Operating-mode selection
- Configuration menus