# Voice Control

Control the trolley with spoken commands.

## Purpose

Provide hands-free control: "follow me," "stop," "go to hole 5."

## Approach

- A microphone captures audio.
- Speech recognition (on the RPi 5) converts speech to commands.
- Commands map to existing behaviors (Follow Me, stop, navigation).

## Architecture

```text
Microphone
    ↓
Speech Recognition
    ↓
Command Mapping
    ↓
Behavior / MotionRequest
    ↓
Safety Controller
```

## Safety

- Voice commands are **request sources only**; they must never bypass the
  Safety Controller.
- Ambiguous or unrecognized commands must not cause motion.
- A stop command should be reliable and high-priority.

## Considerations

- Speech recognition on the RPi 5 is compute-heavy; consider a lightweight
  offline recognizer or a wake-word + limited command set.
- Microphone and audio processing add hardware and complexity.

## Effort

High. Adds microphone hardware, speech recognition, and command mapping.