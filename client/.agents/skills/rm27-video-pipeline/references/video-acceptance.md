# Video acceptance criteria

## Functional cases

- Normal stream start and stop.
- Packets lost, duplicated, reordered, delayed, and truncated.
- Decoder starts before the first keyframe.
- Resolution or codec parameters change when the protocol permits it.
- Stream pauses and resumes without restarting the whole application.
- Window resize, high-DPI scaling, full-screen, and mode switching.
- Recording enabled and disabled while displaying.

## Invariants

- Queues are bounded and expose their depth.
- Old frames are dropped instead of creating unbounded latency.
- Decoder faults cannot block the GUI or command gate.
- Reconnect does not display frames from a previous session as current.
- A stale or lost stream is visibly identified.
- Tests use recorded or generated inputs with known redistribution rights.

## Benchmark report

For every tested path, record hardware, OS, codec, resolution, frame rate, decoder, renderer, latency percentiles when measurable, dropped frames, CPU/GPU load, and recovery behavior. Do not publish an unsupported latency claim.
