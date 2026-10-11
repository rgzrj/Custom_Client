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

The historical RM2026 test HUD has a bounded synthetic loopback demo:
`scripts/run-rm2026-demo.ps1` runs `tests/rm2026_observer_demo.py` with stdlib
only. It owns an ephemeral port and one QML HUD child, visibly labels synthetic
data, and checks reception, expiry, disconnect, re-subscription and replacement
of old fields. `rm2026_hud_demo_tests` uses the same script offscreen; the original
`rm2026_demo_tests` still checks the optional developer viewer. QML checks inspect
actual health/base/outpost/coin labels, per-block clearing, zero/unknown values,
red/blue identity and permanently locked controls. Normal launch/package disable
the historical HUD flag explicitly. This is not
the independent simulator, a general broker, official captures or a release
dependency. Its local elapsed timeline is not end-to-end latency evidence.
The test peer's packet bound is independent of the native ingress checks.
`rm2026_ingress_tests` exercises oversized headers without bodies, malformed
packets, packet/byte floods, reconnect and bounded historical events. This is
loopback evidence, not a full security audit or permission to use real endpoints.
The actual HUD demo verifies disconnect clears old events and recovery displays
only newly received history; it does not equate event history with fresh state.

- Clean configure and build on the supported Windows environment.
- Unit, protocol, replay, UI smoke, and packaging checks pass.
- No simulation endpoints, debug shortcuts, or guessed protocol values are enabled.
- No wireless dependency is required.
- Offline launch succeeds on a clean machine image.
- Logs and replay files do not expose secrets or unauthorized official data.
- The previous known-good package remains available for rollback.
