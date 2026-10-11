# UI and architecture baseline

## Normal heavy-operator layout

```text
┌──────────────────────────────────────────────────────────────┐
│ Alert / phase / time / MQTT / video / control link           │
├───────────────────────────────────────────────┬──────────────┤
│                                               │ Compact HUD  │
│                                               │ HP / ammo    │
│               Primary video                   │ heat / power │
│          with minimal aiming overlay           │ freshness    │
│                                               │              │
│                                               │              │
├───────────────────────────────────────────────┴──────────────┤
│ Small dock: map/target | permitted actions | input mode       │
└──────────────────────────────────────────────────────────────┘
```

The top strip is one line in normal operation. Expand details only when the operator requests them or a critical fault needs action. The video remains the largest region.

## Module direction

```text
Transport -> Domain state -> View models -> QML
Input -> Input mapper -> Command gate -> Transport
Video transport -> Decoder -> Renderer -> QML scene
All boundaries -> Recorder -> Replay source
```

Dependencies point inward toward domain values. Domain code does not depend on Qt Quick, FFmpeg, MQTT libraries, or generated Protobuf types.

## Fallback behavior

- Switching to fallback is explicit and available without navigating through settings.
- If only video remains permitted, show video plus link state; do not reconstruct information that the rules require to be hidden.
- Never replay an input held before or during the switch.
- Returning to normal mode requires fresh state and released controls.

## Integrated command future

Reserve compatible domain state and navigation, but do not place a full team dashboard in the MVP heavy layout. The integrated command mode may later use a separate screen with map, team health, objectives, event history, and multi-terminal wired coordination.
