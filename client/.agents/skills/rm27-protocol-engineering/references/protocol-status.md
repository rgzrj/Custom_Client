# Protocol status

## Confirmed transport-level facts

The RM2027 preview states:

- Video: UDP.
- Competition data: MQTT plus Protobuf.
- Physical connection: wired 1 GbE RJ45.
- Wireless transmit and receive devices are prohibited in the custom terminal.

## Not yet defined in the repository

- Broker address and authentication.
- MQTT topics, QoS, retained-message behavior, session settings, and keepalive.
- Protobuf files, package names, field meanings, enums, versions, and compatibility rules.
- UDP port, framing, fragmentation, codec signalling, sequence numbers, and timing.
- Heavy-robot motion-control path and command format.
- Event-command acknowledgements, rate limits, and error codes.

The RM2026 rule manual does not fill these gaps. It describes allowed behavior and refers protocol structure to a separate communication protocol.

## Available historical communication protocol

The user supplied `规则/RoboMaster 2026 机甲大师高校系列赛通信协议 V2.0.0（20260626）.pdf`
and explicitly authorized a separately versioned 2026 reference implementation.
Section 2 (PDF page 45) defines the 2026 MQTT endpoints/topics and UDP header;
section 2.2 supplies message definitions. These do NOT confirm RM2027 values.
See `contracts/rm2026-v2.0.0/README.md` for exact sources and unresolved issues.
Current scope is offline schemas/parsing/freshness, video assembly tests and
separate Python and native C++ read-only MQTT references restricted to literal
127.0.0.1. Native dependencies are source-built with the pinned MinGW toolchain;
RM27_BUILD_RM2026_PROBE defaults OFF and is not linked into the normal terminal. The
references test subscription/QoS1/reconnect; native SUBACK has a 5-second timeout.
The native observer also returns a pure C++ historical snapshot for static/dynamic
robot values, relative five-slot unit health/structures, own economy, game,
penalty, module and respawn reports. Module code 2 means installation-related
offline, not online; missing/unknown codes stay unknown. Respawn eligibility is
reported only, not command authority or proof of life; progress has no assumed
seconds/percentage conversion. Module online does not prove live video.
Game results/reasons are meaningful only in settlement with
known values; penalty zero/absence/staleness is NOT release evidence. Each
block carries monotonic receive time; absent/stale data remains unknown, and
reported identity changes clear cached topics. This is not TerminalState,
authentication or command/penalty enforcement. Latest Event (known IDs 1..15,
raw parameters only) and world RobotPosition reports are also typed. Event
parameters do not mutate gameplay; latest sample is not complete event history.
Position yaw is degrees/north zero, not gimbal yaw; missing axes/owner stay
absent. Do not infer XYZ units/origin/map transforms or a self-robot fallback;
robot_id ownership is documented as effective only for the gimbal-operator
client. CustomByteBlock and unselected fields remain raw diagnostics.
No external referee connection, decoding, production HUD mapping or control publication
is enabled; Python is not a native runtime/release dependency. Both reject DUP
data conservatively, so application-level at-least-once delivery is not claimed.
The native pinned Paho packet factory now checks type/Remaining Length before
body allocation, structural lengths before constructors, and complete-packet
budgets (256 packets/1 MiB per fixed second). A build-only source copy preserves
upstream notices; downloaded source remains intact. This is a project-local
profile, not a full hostile-broker audit; loopback confinement stays mandatory.
Slow partial packets, authentication/TLS and source freshness remain unverified.
See the version README, stage twelve, for exact limits and tested cases.
In particular, RadarInfoToClient (PDF pages 66-67) is contradictory/incomplete;
do not infer its wire schema from a community implementation.

The opt-in RM2026_BUILD_OBSERVER_VIEW target provides a separate Qt Widgets
developer viewer using the same probe CLI and typed diagnostic state. It is
loopback-only and read-only; it is not a normal HUD/TerminalState binding or
release dependency. Expired fields disappear, disconnect clears all blocks,
and event parameters are plain text. The console and viewer run the same wire
tests; a separate offscreen view check covers unknown/zero/false/expiry/reset.
No local Protobuf broker means no data, not a license to inject sim.v1 JSON.
An explicit synthetic demo now runs a short-lived stdlib loopback wire peer
and its own labelled test-HUD child (the viewer remains available). It uses artificial binary fixtures, checks
expiry/disconnect/re-subscription and old-field replacement, and is not a
general broker or a normal terminal/release dependency. The test peer's input
bound is independent of the native guards and does not certify production safety.

The user authorized a local read-only bridge into the existing QML HUD.
RM27_RM2026_LOCAL_HUD defaults OFF; its dedicated build requires the historical
adapter, explicit version/synthetic acknowledgement and literal loopback host.
Typed Snapshot -> TerminalController -> existing QML keys, never wire JSON.
Self health, relative robot slots, base remaining HP, verified outpost life,
own economy, phase label and permitted shots are displayed. Missing aerial HP,
base maximum, physical ammo, gimbal angles and radar coordinates stay unknown.
Per-topic expiry clears only that block; disconnect clears all display data.
This controller is permanently Simulation/Replay and cannot apply normal link
profiles or arm controls. Production launch/package explicitly disable the option.

Historical Event messages now also reach the existing plain-text feed: at most
24 entries, 4096 UTF-8 bytes per history parameter, 512 displayed characters.
Local receipt sequence is not an official event ID. Polling does not append
again; separate non-DUP arrivals may have the same type/parameter. Expiry removes
the latest fresh sample but not labelled history; identity/session changes and
disconnect clear history. Evictions are counted; completeness is not claimed.

## Allowed work before the protocol arrives

- Define adapter interfaces and domain types without official field names.
- Build a simulator namespace such as `sim.v1` that cannot be mistaken for the official protocol.
- Test connection, freshness, queueing, reconnect, and failure behavior through dependency injection.
- Add official schemas later behind the same adapters.
