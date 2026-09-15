# Plan: Energy Dashboard on the HMI

**TL;DR** — Surface the battery range estimator's `/range/status` on the ESP32
handle-unit HMI as a new **Energy** screen, so the operator can see remaining
range, remaining hole distance, return distance, and the OK/CAUTION/CRITICAL
warning at a glance.

**User decisions (confirmed)**
- Scope: Energy dashboard on the HMI only (no web changes).
- Reuses the existing range estimator (`/range/status`) + existing HMI screen
  infrastructure (STATE_UPDATE downlink, LVGL screens, menu navigation).

---

## Current state (verified)

- **Range estimator** (`golfcart_navigation/range_estimator_node`) publishes
  `RangeStatus` on `/range/status`:
  `state` (OK/CAUTION/CRITICAL), `range_m`, `remaining_m`, `return_m`,
  `can_finish`, `wh_per_m`.
- **HMI downlink** (`handle_gateway.py`): subscribes to ROS state topics and
  pushes `STATE_UPDATE` frames to the ESP32 via `_send_state(id, value)`.
- **Protocol** (`protocol.py` + `handle_protocol.h`): state IDs `0x01`-`0x1A`
  used; IDs `0x40`-`0x7F` reserved for future state values.
- **Screens** (`screens.cpp` + `screens.h`): LVGL screen builders indexed by
  `SCR_*`; the main menu (`build_menu`) lists items; the gateway maps
  `MENU_SELECT` items to screens via `_menu_main`.
- **main.ino**: decodes `STATE_UPDATE` frames and calls `screens_set_state(id,
  value)`; `DL_SCREEN_NAV` shows a screen.

### Key gaps for the energy dashboard (what this plan fills)
1. **No range state on the HMI** — the ESP32 doesn't know about `/range/status`.
2. **No Energy screen** — no way to display range/remaining/return.
3. **No menu entry** — the main menu has no ENERGY item.

---

## Architecture

```text
range_estimator_node → /range/status (RangeStatus)
        ↓  handle_gateway.on_range (subscribe)
        ↓  _send_state(ST_RANGE_*, ...)  [STATE_UPDATE frames]
        ↓  ESP32 main.ino → screens_set_state(...)
        ↓  screens.cpp build_energy()  [SCR_ENERGY]
        ↓  operator taps ENERGY in the main menu
```

### New state IDs (protocol)
Add to `protocol.py`, `handle_protocol.h`, and `docs/handle-protocol.md`:

| ID | Name | Type | Example |
| --- | --- | --- | --- |
| `0x40` | `RANGE_M` | uint16 | 2500 → 2500 m |
| `0x41` | `HOLE_REMAINING_M` (reuse 0x13) | — | — |
| `0x42` | `RETURN_M` | uint16 | 1200 → 1200 m |
| `0x43` | `RANGE_STATE` | uint8 enum | 0=OK,1=CAUTION,2=CRITICAL |

(`HOLE_REMAINING_M` already exists as `0x13`; reuse it.)

### Gateway (`handle_gateway.py`)
- Import `RangeStatus`; subscribe to `/range/status`.
- `on_range(msg)`: `_send_state(ST_RANGE_M, int(msg.range_m))`,
  `_send_state(ST_RETURN_M, int(msg.return_m))`,
  `_send_state(ST_RANGE_STATE, {OK:0, CAUTION:1, CRITICAL:2}[msg.state])`.
- Add an **ENERGY** item to the main menu (`_menu_main`): insert after ASSIST
  (item 2), shifting CHANGE HOLE etc. down. Map it to `SCREEN_ENERGY`.

### New screen: `SCR_ENERGY` (0x10)
- Add `SCR_ENERGY = 0x10` to `screens.h`, `handle_protocol.h`, `protocol.py`.
- `build_energy()` in `screens.cpp`: a dashboard showing:
  - **Range** (m) — big number, colored by state (OK green / CAUTION amber /
    CRITICAL red).
  - **Remaining** (m) — remaining hole distance.
  - **Return** (m) — estimated return distance.
  - **State badge** — OK / CAUTION / CRITICAL.
  - A **Back** button → main menu.
- Register `build_energy` in `screen_builders[0x10]`.
- Add `HandleState` fields: `range_m`, `return_m`, `range_state`.

### main.ino
- Decode `ST_RANGE_M` (uint16), `ST_RETURN_M` (uint16), `ST_RANGE_STATE`
  (uint8) → `screens_set_state`.

### hmi-spec.md
- Add the ENERGY screen to the screen map + a section describing it.

---

## Safety notes
- The energy dashboard is **informational only** — it never commands motion.
- The CAUTION/CRITICAL states come from the range estimator's conservative
  model (reserve + margin), so the operator is warned before running out.

## Effort
Small-Medium. New state IDs + gateway subscription + one LVGL screen + menu
entry. No new messages (reuses `RangeStatus`).

## Validation
- `test_protocol.py`: add tests for the new state IDs (encode/decode).
- Firmware builds (`build_firmware.sh full`).
- Full workspace test suite passes.