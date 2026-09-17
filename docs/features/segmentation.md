# Live Course Segmentation

Label the camera view into golf-course classes (fairway, rough, water, bunkers,
etc.) so the trolley understands the terrain around it.

> **Status:** CPU baseline implemented; learned-model acceleration remains
> hardware/model validation work.

## Purpose

For a traveling player on different courses, live segmentation can label an
unfamiliar course on the fly — distinguishing drivable fairway from rough,
water, and bunkers.

## Overview

```text
Pi Camera (RGB) → /camera/image → segmentation_node (CPU baseline / Coral later)
        ↓  /segmentation/status (SegmentationStatus)
        ↓
   HMI (Segmentation ON/OFF) + future semantic layer
```

## How it works

- **`segmentation_node`** subscribes to `/camera/image` (RGB from the Pi
  Camera) and `/capability/status`.
- Works with the camera alone: the node downsamples RGB frames and performs a
  low-cost water/bunker/fairway/rough classification at a configurable rate.
- The current result is a coarse visual hint, not a safety decision. The
  tilted LiDAR remains the source for ditch/stream stopping.
- Publishes `SegmentationStatus` (`active`, `class_count`, `confidence`) on
  `/segmentation/status`.
- Coral acceleration still requires selecting and validating a concrete model,
  tensor contract, and EdgeTPU runtime; no learned model is bundled yet.

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
  publish_rate_hz: 1.0
  processing_width: 160
  processing_height: 90
  min_confidence: 0.35
```

## HMI

The **Camera debug** screen shows Camera OK/MISSING + Segmentation ON/OFF
(`ST_SEGMENTATION`).