# Energy Dashboard (HMI)

Surface the battery range estimator's `/range/status` on the ESP32 handle-unit
HMI as a new **Energy** screen, so the operator can see remaining range,
remaining hole distance, return distance, and the OK/CAUTION/CRITICAL warning
at a glance.

> **Status:** Implemented. See `plans/plan-energy-dashboard.prompt.md` for the
> full design.

## Purpose

The range estimator (`/range/status`) already computes the remaining range and
warning state. This feature puts that information on the handle-unit display,
where the operator can see it without reaching for a phone.

## Overview

```text
range_estimator_node → /range/status (RangeStatus)
        ↓  handle_gateway.on_range (subscribe)
        ↓  _send_state(ST_RANGE_*, ...)  [STATE_UPDATE frames]
        ↓  ESP32 main.ino → screens_set_state(...)
        ↓  screens.cpp build_energy()  [SCR_ENERGY]
        ↓  operator taps ENERGY in the main menu
```

## How it works

- **New state IDs** (`protocol.py` / `handle_protocol.h`):
  - `ST_RANGE_M` (0x40, uint16) — estimated remaining range (m).
  - `ST_RETURN_M` (0x41, uint16) — estimated return distance (m).
  - `ST_RANGE_STATE` (0x42, uint8) — 0=OK, 1=CAUTION, 2=CRITICAL.
- **Gateway** (`handle_gateway.py`): subscribes `/range/status` and forwards
  the values to the ESP32; adds an **ENERGY** item to the main menu.
- **Screen** (`screens.cpp`): `build_energy()` shows range (colored by state),
  state badge, remaining hole distance, return distance, battery %, and a
  **Main Menu** back button.
- **Firmware** (`main.ino`): decodes the new state IDs.

## Safety considerations

- The energy dashboard is **informational only** — it never commands motion.
- The CAUTION/CRITICAL states come from the range estimator's conservative
  model (reserve + margin), so the operator is warned before running out.

## Configuration

No new configuration — the dashboard reads the existing `range_estimator_node`
output (`/range/status`).

## Messages

- `golfcart_msgs/RangeStatus` — `/range/status` (reused, no change).

## Validation

- `test_protocol.py` — encode tests for the new state IDs.
- Firmware builds (RAM 29.7%, Flash 15.5%).
- Full workspace test suite passes.