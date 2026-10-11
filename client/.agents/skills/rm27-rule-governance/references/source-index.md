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

- The user supplied the RM2026 V2.0.0 communication protocol (20260626) and
  authorized a historical reference implementation. The offline first phase
  and subsequent Python/native C++ local-only read-only references are documented
  in `contracts/rm2026-v2.0.0/README.md`. Its section 2/2.2 facts are confirmed
  ONLY for 2026, not promoted to final RM2027 facts. These references have no
  control publisher, no production HUD integration and no external network endpoint.
  The native build is opt-in; basic packet bounds/flood guards are locally tested,
  not a full hostile-broker audit or authorization for external endpoints.
  Its selected typed mappings use section 2.2.4/5/10/11 and appendix two
  (PDF 53-55, 58-60, 80-81). Relative ally/enemy slots are 1/2/3/4/7;
  remaining_ammo is permitted shots, not physical ammunition. Unknown stays
  unknown; reported identity is not authentication. No phase/penalty gate
  integration or absolute map-coordinate inference is enabled.
  GameStatus/PenaltyInfo typed reports follow sections 2.2.3/15 (PDF 51-53,
  63-64). Unknown enums remain unknown; result/reason applies only in settlement.
  Penalty type 0 is not documented as clear; expiry or zero duration must not
  be used to release masking or open a gate. No enforcement is claimed.
  Module/respawn reports follow sections 2.2.12/9 (PDF 60-61, 57-58).
  All eleven module codes 0/1/2 mean offline/online/installation-related offline;
  unknown codes stay unknown. Respawn bool/zero/progress fields are reports,
  not life inference, timers or permission to publish a command. Module online
  does not certify live decoded video. No automatic respawn is enabled.
  Event/RobotPosition follow sections 2.2.7/13 (PDF 55-56, 61-62).
  Known event IDs are 1..15; parameters stay raw, with no gameplay effects
  inferred. Latest sample is not complete event history. Position is world
  XYZ and degrees with north zero; units/origin/map transform are unconfirmed.
  Reported robot_id is ownership (documented gimbal-client-only), not login
  or a universal self ID; missing values are not filled. No map/HUD integration.
  A separate opt-in loopback-only developer viewer displays the typed blocks;
  it does not change normal HUD, authenticate an operator, clear penalties or
  enable publication. Reception freshness is not source-time/live-video proof.
  User-authorized local test HUD now consumes selected typed historical fields;
  it is an opt-in, visibly synthetic Simulation/Replay build, not a production
  referee entry or identity/control authority. No aerial HP or base maximum is
  invented; unknown outpost codes remain unknown and positions stay off the map.
  Its bounded 24-entry Event feed is labelled historical and plain text; local
  sequence keys and raw parameters are not official event uniqueness or effects.

- Confirmed ONLY for 2026: rule V2.2.0 (20260807), section 7.1.2 (PDF 133-135)
  distinguishes accumulating yellow-card masking and red-card consequences;
  yellow-card masking covers video/map/custom UI. Dart masking is a separate
  mechanism (section 5.6.5, PDF 117-118); deployment loss of video still allows
  map/custom UI (section 5.6.1, PDF 112-113). Do not apply one shared mask timer.
  Unknown: PenaltyInfo (communication PDF 63-64) does not establish an authoritative
  release, repeated-update or reconnect recovery strategy. No enforcement/release
  is implemented; absent/stale/zero reports must not clear an active penalty.
  RadarInfoToClient (communication PDF 66-67) remains contradictory; obtain the
  official compilable schema/correction and map calibration before real fusion.

- MVP role: heavy-robot operator terminal only.
- Windows first, with a cross-platform core.
- Qt 6, QML, and C++ for the production terminal; Python for the separate simulator.
- H.264/H.265 first; learned low-bitrate compression is deferred.
- Future modes: integrated command view and manual fallback to the custom terminal when permitted data remains available.

## Review trigger

Re-run rule review whenever a newer RM2027 rule, communication protocol, supplement, or official answer is added under `规则/`.
