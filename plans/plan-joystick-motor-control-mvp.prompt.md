# Plan: Joystick Motor Control MVP

**TL;DR** — Build the first working slice of the golf trolley: drive the motors with a joystick (and keyboard teleop), routed through the documented **full safety + motion pipeline** so no high-level code commands motors directly. Includes a **mock ODrive** so the whole pipeline is testable without physical hardware.

**Key decision:** Stack is **ROS 2 Lyrical / Ubuntu 26.04** (already applied to `AGENT.md` and `docs/architecture.md`). The GIXVISION stereo cam is **provisional** — if it's unsupported on Lyrical/ARM64, swap to a different camera later (not part of this MVP).

**Steps**

**Phase 0 — Workspace scaffolding**
1. Create colcon workspace `src/` with packages: `golfcart_msgs`, `golfcart_odrive`, `golfcart_control`, `golfcart_teleop`, `golfcart_bringup`.
2. Define the `MotionRequest` message (`linear_velocity_mps`, `angular_velocity_radps`, `source`, `priority`, `timestamp`).

**Phase 1 — MotorController interface + ODrive + mock** *(depends on 1)*
3. Define the `MotorController` interface (`command_velocity`, `get_velocity`, `get_position`, `get_state`, `get_fault`, `stop`).
4. Implement the **ODrive** driver (C++) behind that interface.
5. Implement a **mock** `MotorController` for hardware-free testing.

**Phase 2 — Motion Controller** *(depends on 3)*
6. C++ node converting `linear/angular` → `left/right` wheel velocity (differential drive), publishing `SafeMotionCommand`.

**Phase 3 — Safety Controller** *(depends on 3)*
7. C++ node with the state machine (`SAFE_STOPPED/READY/MOVING/LIMITED/STOPPING/FAULT`), speed limits, and enable/stop handling.

**Phase 4 — Teleop nodes** *(parallel with 6, 7)*
8. `joystick_node` (`sensor_msgs/Joy` → `MotionRequest`) and a keyboard teleop node.

**Phase 5 — Bringup + launch** *(depends on all)*
9. Launch files wiring the pipeline; RViz; config to select mock vs. real ODrive.

**Phase 6 — Verification** *(depends on all)*
10. Unit tests (safety state machine, differential drive), integration test with mock ODrive, manual hardware test.

**Relevant files**
- `AGENT.md`, `docs/architecture.md` — reference the documented pipeline, `MotionRequest`, `MotorController`, safety states, differential-drive math.
- New packages under `src/` (see Phase 0).

**Verification**
1. `colcon build` succeeds for all packages on Lyrical/26.04.
2. Unit tests pass for the safety state machine and differential-drive conversion.
3. Integration: run the full pipeline with the mock ODrive, drive via keyboard/joystick, confirm correct motor commands.
4. Manual: real ODrive, joystick moves the motors, and the safety stop works.

**Decisions**
- **Distro:** ROS 2 Lyrical / Ubuntu 26.04 (already updated in docs).
- **Camera:** GIXVISION provisional — swap later if unsupported on Lyrical/ARM64.
- **Full safety + motion pipeline** — joystick → `MotionRequest` → Safety Controller → Motion Controller → ODrive. No direct motor commands (per `AGENT.md`).
- **Both joystick and keyboard teleop** for flexibility.
- **Mock ODrive included** for hardware-free testing.
- **C++** for safety/motion; **Python** for teleop/tooling.

**Further Considerations**
1. **ODrive comms interface** — USB vs. CAN is still an open decision (§34). The MVP should pick one (USB is simplest to start).
2. **Enable/arm sequence** — how the Safety Controller transitions from `SAFE_STOPPED` to `READY` (e.g., a joystick button or a service call) needs a concrete choice.
3. **Camera swap** — deferred; only revisit if the GIXVISION cam proves unsupported on Lyrical/ARM64.
