# Energy-Saving Mode

Put non-essential systems to sleep while the cart is standing still, and wake
them with the IMU when motion resumes.

## Purpose

A golf cart stands still a lot during a round — waiting for other players, on
the green, during a swing. Running every sensor and system at full rate the
whole time wastes battery. Energy-saving mode reduces power draw while idle,
without compromising safety.

## Behavior

The `energy_saver_node` (in `golfcart_power`) monitors activity and manages the
power state:

1. **ACTIVE** — all systems run normally.
2. After the cart is idle for `sleep_timeout_s` (default 60 s), it enters
   **SLEEP**:
   - publishes `PowerState=SLEEP` on `/power/state`
   - publishes `sleep` on `/power/sleep_cmd` so other nodes can reduce their
     rates / suspend non-essential work
3. The **IMU stays active**. When it detects motion (inclination change), or any
   activity arrives (wheel motion, safety state change), it wakes back to
   **ACTIVE** and publishes `PowerState=ACTIVE` + `wake` on `/power/sleep_cmd`.

## Inputs

- IMU data (`/imu/data`) — the wake trigger while asleep; also slope detection
- wheel motion (`/motor/state`)
- safety state (`/safety/state`)
- joystick / operator activity

## Outputs

- `/power/state` (`golfcart_msgs/PowerState`) — ACTIVE / SLEEP / WAKING
- `/power/sleep_cmd` (`std_msgs/String`) — `sleep` / `wake` commands for other
  nodes to react to

## Safety

- **Never sleep while at roll-away risk.** If the cart is on a slope (IMU
  inclination > `max_slope_rad`) or moving, sleep is suppressed — the cart must
  stay powered to hold position / brake. This mirrors `auto_shutdown_node`.
- The IMU is the one sensor that must **always** stay active so the cart can
  wake itself.
- Waking is immediate on any motion, so the cart is responsive when the operator
  returns.

## Relationship to Auto-Shutdown

Energy-saving mode is a **lighter** state than auto-shutdown:

| | Energy-Saving (SLEEP) | Auto-Shutdown |
| --- | --- | --- |
| Power | reduced (sensors/rates down) | off |
| Restart | automatic (IMU motion) | manual |
| Timeout | `sleep_timeout_s` (60 s) | `idle_timeout_s` (300 s) |
| Use case | during a round (frequent stops) | end of day / long idle |

Both are suppressed by roll-away risk. Energy-saving mode is the first line of
defense; auto-shutdown is the final one.

## Effort

Low. A single watchdog node (`energy_saver_node`) plus a message
(`PowerState.msg`). Other nodes opt in to the `sleep`/`wake` commands to reduce
their rates.