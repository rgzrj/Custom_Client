# Rule source index

## Authority order

1. Final RM2027 communication protocol and formal rule documents.
2. Official RM2027 supplements and answers.
3. `规则/RoboMaster 2027 高校系列赛规则变更前瞻手册（20260909）.pdf`.
4. Final RM2026 documents, for historical behavior only.
5. Public team repositories, as implementation references rather than rules.

## Current confirmed information

From the RM2027 preview, sections 1 and 2.2:

- The former custom controller and custom client are merged into a custom terminal.
- The terminal may monitor and control a robot, receive event data and video, and send permitted event commands.
- Event data uses MQTT and Protobuf; video uses UDP.
- The terminal connects by wired RJ45 and must not contain wireless transmit or receive devices.
- The preview is incomplete; the final rules and communication protocol remain authoritative.

From the RM2026 rule manual, sections 5.7 and 6.5:

- A custom client can receive permitted event information and video and send supported event commands.
- Commands are not accepted during the five-second countdown.
- The manual does not define MQTT topics, Protobuf schemas, ports, QoS, or message fields; it points to a separate communication protocol.

## Project decisions

- MVP role: heavy-robot operator terminal only.
- Windows first, with a cross-platform core.
- Qt 6, QML, and C++ for the production terminal; Python for the separate simulator.
- H.264/H.265 first; learned low-bitrate compression is deferred.
- Future modes: integrated command view and manual fallback to the custom terminal when permitted data remains available.

## Review trigger

Re-run rule review whenever a newer RM2027 rule, communication protocol, supplement, or official answer is added under `规则/`.
