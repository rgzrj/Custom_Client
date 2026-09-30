---
name: rm27-protocol-engineering
description: Design, implement, and verify RM2027 MQTT, Protobuf, UDP, and robot-control adapters. Use for protocol code, message schemas, connection handling, command gating, and packet-level tests.
---

# RM27 Protocol Engineering

Build protocol adapters that are evidence-based, replaceable, and safe under disconnects.

## Before implementation

Read [references/protocol-status.md](references/protocol-status.md). Also use `$rm27-rule-governance` when behavior depends on a competition rule.

## Design rules

- Separate `EventDataAdapter`, `VideoTransport`, and `RobotControlAdapter`; do not combine them into one network manager.
- Translate transport messages into domain values before exposing them to QML.
- Attach monotonic receive time and freshness to every state update.
- Route every outbound action through a central command gate that checks phase, role, freshness, connection state, rate limits, and acknowledgement state.
- Use bounded queues and explicit overflow behavior. Never allow delayed movement or event commands to replay after reconnect.
- Preserve raw packet or message samples in tests when their source is official and redistribution is allowed.
- Keep simulation schemas visibly separate from official schemas so test placeholders cannot ship by accident.

## Verification

Cover valid, unknown, truncated, duplicated, reordered, delayed, and version-mismatched messages. Verify reconnect, subscription restoration, stale-state handling, command rejection, and no replay of old commands.

Do not create official-looking field names or constants until an official communication protocol or schema supplies them.
