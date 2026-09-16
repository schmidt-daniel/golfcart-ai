# Multi-Round Battery Learning

Persist the range estimator's learned energy model (`Wh/m` per slope bucket)
across reboots, so it improves with every round instead of resetting to the
default each boot.

> **Status:** Implemented. `EnergyModel.serialize()/deserialize()` +
> `range_estimator_node` persistence.

## Purpose

The range estimator learns the trolley's real energy consumption from measured
battery current. Without persistence, that learning was lost on every reboot.
Multi-round learning makes the estimate genuinely improve over time.

## How it works

- **`EnergyModel.serialize()`** — outputs the learned `Wh/m` per slope bucket
  (downhill/flat/uphill) as a comma-separated string.
- **`EnergyModel.deserialize()`** — loads it back, clamping to a sane range and
  rejecting bad input.
- **`range_estimator_node`** loads the model from disk at startup and saves it
  each update cycle (creating the directory if needed).

## Configuration

See `config/golfcart.yaml`:

```yaml
range_estimator_node:
  model_file: /var/lib/golfcart/range_model.txt   # persisted model
```

## Behavior

- Round 1: model starts at `default_wh_per_m` (0.02), learns from measured
  current.
- End of round: model saved to disk.
- Round 2 boot: model loaded → the estimator starts with the learned values.
- Each round improves the estimate further.