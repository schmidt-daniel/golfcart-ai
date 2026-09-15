# Camera Vision — Sensor & Compute Decision

> **Status:** Decision record (2026-09-15). Camera vision is **deferred** until
> the hardware-validation phase. This doc captures the chosen sensor stack so
> the decision isn't re-litigated.

## Decision

**Drop the Intel RealSense D435i.** Use a **Raspberry Pi Camera Module 3** for
RGB vision and a **Coral USB Accelerator** for compute.

| Role | Chosen sensor | Why |
| --- | --- | --- |
| Object detection / segmentation / gesture | **Pi Camera Module 3** (RGB) | Plain RGB works in sunlight; connects via CSI (no USB cost) |
| Vision model inference | **Coral USB Accelerator** (NPU) | Runs YOLO-nano / segmentation on the freed USB port |
| Streams / ditches | **2nd tilted FHL-LD19P LiDAR** | Sunlight-proof, measured ground-plane breaks (not the camera's job) |

## Why not the RealSense

- The D435i's **active IR depth** is washed out by **direct sunlight** — a real
  problem on a golf course.
- It was the **4th USB device**, filling the last Pi 5 USB port.

## USB port budget (Pi 5 = 4 ports)

| # | Device | Interface |
| --- | --- | --- |
| 1 | ODrive 3.6 | USB |
| 2 | ESP32 handle unit | USB |
| 3 | USB GPS dongle | USB |
| 4 | **Coral USB Accelerator** | USB (freed by dropping the RealSense) |
| — | Pi Camera Module 3 | CSI (no USB) |

- Dropping the RealSense freed the 4th port → the **Coral USB** fits with **no
  HAT** needed.
- Because it's USB (not a HAT), the sensor HAT's GPIOs (GPIO 2/3 I2C, 14/15
  UART) stay **completely untouched** — no stacking or passthrough concerns.
- *(If a HAT NPU were ever used instead, it MUST have GPIO passthrough for
  GPIO 2/3/14/15.)*

## Compute

| Host | Detection | Segmentation |
| --- | --- | --- |
| Pi 5 CPU-only | Lightweight models (YOLO-nano/MobileNet), low FPS | Slow |
| + Coral USB NPU | Real-time YOLO-nano | Usable |

## Full sensor set (after this decision)

- **Pi Camera Module 3** (RGB) → detection / segmentation / gesture
- **LiDAR horizontal** (FHL-LD19P) → follow-me legs / obstacle stopping zone
- **LiDAR tilted ~25°** (2nd FHL-LD19P) → streams / ditches
- **Coral USB Accelerator** → vision model inference
- GPS dongle, ODrive, ESP32 handle → remaining USB ports

## Open items

- Camera mounting angle for detection/segmentation (see `camera-vision` notes).
- Whether Pi 5 CPU-only is acceptable before adding the Coral.