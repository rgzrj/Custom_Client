---
name: rm27-simulator-release
description: Build deterministic RM2027 simulation, replay, verification, and offline release workflows. Use for the Python simulator, integration scenarios, regression evidence, packaging, and field-readiness checks.
---

# RM27 Simulator and Release

Make the production C++ terminal testable without official field equipment and package it for reliable offline use.

Read [references/scenarios-and-release.md](references/scenarios-and-release.md) when adding a scenario, replay format, or release check.

## Simulator

- Keep the Python simulator separate from the production client.
- Mark simulated protocols and UI state clearly; simulation must never connect to real endpoints by default.
- Use deterministic seeds, monotonic scenario time, and explicit event scripts.
- Support live simulation and replay through the same production adapter interfaces.
- Record inbound messages, outbound decisions, inputs, link changes, and video timing on a shared timeline.

## Regression workflow

For a bug, add the smallest scenario or replay that reproduces it, verify failure, implement the fix, and preserve the scenario. Test partial failures independently instead of representing the whole system as simply connected or disconnected.

## Release

- Target Windows first and verify the core remains portable.
- Package all runtime dependencies for offline operation.
- Keep field configuration outside source control and validate it before startup.
- Default release configuration to safe endpoints and require explicit field activation.
- Produce a version manifest, dependency inventory, configuration checksum, test summary, and rollback artifact.
- Append the release or workflow change to the change log section of `CLAUDE.md`.
