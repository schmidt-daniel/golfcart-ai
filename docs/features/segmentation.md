# Live Course Segmentation

Label the camera view into golf-course classes (fairway, rough, water, bunkers,
etc.) so the trolley understands the terrain around it.

> **Status:** Implemented (node + capability gating). The model inference
> (zero-shot SAM / fine-tuned model on the Coral NPU) is abstracted for the
> hardware phase.

## Purpose

For a traveling player on different courses, live segmentation can label an
unfamiliar course on the fly — distinguishing drivable fairway from rough,
water, and bunkers.

## Overview

```text
Pi Camera (RGB) → /camera/image → segmentation_node (Coral NPU)
        ↓  /segmentation/status (SegmentationStatus)
        ↓
   HMI (Segmentation ON/OFF) + future semantic layer
```

## How it works

- **`segmentation_node`** subscribes to `/camera/image` (RGB from the Pi
  Camera) and `/capability/status`.
- **Gated on camera + Coral** capability: segmentation is `active` only when
  both are present AND frames are arriving.
- Publishes `SegmentationStatus` (`active`, `class_count`, `confidence`) on
  `/segmentation/status`.
- The **model inference** (zero-shot SAM first, fine-tune only if needed) is
  abstracted — the node reports status; the actual model runs in the hardware
  phase.

## Model strategy

- **Zero-shot SAM / Grounding-DINO first** — segment "water", "bunker", etc.
  from a text prompt with no training.
- **Fine-tune only if needed** — a pretrained model (DeepLabV3+/SegFormer/
  YOLO-seg) with a small, safety-focused class set (water, bunker, fairway,
  rough). Skip cosmetic classes (fringe, green) initially.

## Configuration

See `config/golfcart.yaml`:

```yaml
segmentation_node:
  publish_rate_hz: 5.0
```

## HMI

The **Camera debug** screen shows Camera OK/MISSING + Segmentation ON/OFF
(`ST_SEGMENTATION`).