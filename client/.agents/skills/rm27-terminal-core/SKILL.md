---
name: rm27-terminal-core
description: Build or review the Windows-first Qt 6/QML/C++ RM2027 custom terminal. Use for application architecture, operator workflow, input handling, UI layout, state management, and low-latency desktop behavior.
---

# RM27 Terminal Core

Deliver a stable, attractive, video-first heavy-operator terminal without blocking future command and fallback modes.

Read [references/ui-and-architecture.md](references/ui-and-architecture.md) for the approved layout and module boundaries.

## Architecture

- Production stack: Qt 6, QML, C++, and CMake. Target Windows first while keeping domain and transport code portable.
- Keep transport, domain state, command policy, input mapping, media, recording, and presentation as separate modules.
- QML consumes view models; it must not parse packets, own sockets, or issue transport commands directly.
- Keep the GUI thread free of blocking I/O, decoding, filesystem writes, and reconnect loops.
- Use one normalized state store with timestamps and explicit validity rather than scattered global properties.
- Route wired keyboard, mouse, and controller input through configurable mappings and a single command gate.

## Product modes

1. `Heavy operator`: default MVP, video-first and limited to heavy-robot needs.
2. `Integrated command`: future full-team situational view; keep it as a distinct layout rather than shrinking the heavy video view.
3. `Fallback`: manually switchable, rule-compliant degraded operation when official interfaces are affected. Never use it to evade required masking or penalties.
4. `Simulation/replay`: clearly marked and unable to transmit to real endpoints.

## UI priorities

- Put a thin alert and link-status strip at the top.
- Make video the dominant surface: target at least two-thirds of the usable 16:9 content area during normal heavy operation.
- Keep map/target hints, permitted event actions, and current input mode compact and secondary.
- Prefer a narrow side HUD and collapsible bottom dock over permanent large panels.
- Use restrained spacing, consistent typography, aligned numbers, and a small status color vocabulary. Beauty must improve scanability, not decorate the screen.
- Critical alerts may draw attention but must not hide the aiming region unless safety requires it.

## Completion checks

Verify keyboard-only operation, wired-controller disconnect, window resize, 16:9 layouts, high-DPI scaling, missing video, stale data, partial connection, and manual mode switching. Record the completed change in the change log section of `CLAUDE.md`.
