# Scenarios and release gates

## Minimum deterministic scenarios

- Cold start with all links healthy.
- MQTT unavailable, delayed, disconnected, and restored.
- Competition state becomes stale while video remains live.
- Video loss, corruption, high jitter, and recovery.
- Controller disconnect and reconnect with controls held.
- Countdown or another blocked phase rejects commands.
- Manual switch between heavy, fallback, and simulation modes.
- Dart-related or other interface effects represented exactly as the active official rules require.
- Application restart during an active simulated match.
- Recorder output replayed to the same visible and command-gate result.

## Release gates

- Clean configure and build on the supported Windows environment.
- Unit, protocol, replay, UI smoke, and packaging checks pass.
- No simulation endpoints, debug shortcuts, or guessed protocol values are enabled.
- No wireless dependency is required.
- Offline launch succeeds on a clean machine image.
- Logs and replay files do not expose secrets or unauthorized official data.
- The previous known-good package remains available for rollback.
