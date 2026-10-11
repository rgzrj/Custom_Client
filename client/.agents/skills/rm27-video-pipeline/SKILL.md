---
name: rm27-video-pipeline
description: Build, optimize, or diagnose the RM2027 low-latency UDP video path. Use for packet reception, H.264/H.265 decoding, rendering, recovery, recording, and latency or frame-loss analysis.
---

# RM27 Video Pipeline

Keep the operator video large, responsive, and isolated from failures in the rest of the terminal.

Read [references/video-acceptance.md](references/video-acceptance.md) before choosing buffers, decoder settings, or recovery behavior.

## Baseline

- Implement official UDP framing only from the official protocol.
- Support H.264/H.265 first. Learned low-bitrate compression is a later optional adapter, not an MVP dependency.
- Keep receive, reassembly, decode, render, and record stages independently observable.
- Use bounded latency-oriented queues. Prefer dropping obsolete video to accumulating delay.
- Keep decode and recording off the GUI thread.
- Preserve the last valid frame only when it helps orientation; clearly show that the stream is stale.
- Video failure must not freeze state updates, input handling, alerts, or mode switching.

## Optimize with measurements

Measure capture or packet timestamp to display when the protocol makes it possible, plus receive jitter, reorder count, loss, decode time, render time, queue depth, dropped frames, and recovery time. Compare hardware and software decode with the same replay input before changing the default.

## UI contract

The video item is the dominant heavy-operator surface. Overlays stay minimal, stable, and outside the aiming region when possible. Diagnostics belong in the top strip or compact HUD, not in a permanent panel over video.
