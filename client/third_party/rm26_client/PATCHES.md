# RM26_Client backend adoption

Source: ClearWei/RM26_Client, commit `e291f2f65f5a5924509e4e2fe1b148ff9d4b24af`.
MIT; Copyright (c) 2025–2026 Fudan University EGA Team. Original copyright headers
and LICENSE are retained. SOURCE.json records hashes of the unmodified originals.
The normal client builds this local dependency closure, independently of the
archived upstream checkout. No upstream UI, images or simulator are imported.

Local changes:

- `robomaster.proto`: align the 35 verified messages with the project's official
  2026 V2.0.0 transcription, including explicit scalar presence and MapClickInfo/
  MapClickCmd. Legacy upstream containers remain compilation dependencies.
  RadarInfoToClient remains an upstream definition but is not subscribed/accepted:
  the official outer field is ambiguous. No upstream uplink is called by M1.
- `MqttManager`: run synchronous Paho operations on the dedicated backend thread,
  instead of a thread-pool task capturing the manager. Connect waits are two
  seconds; subscription waits use Paho's supported five-second minimum, checked
  for success. Validate subscription grants. Preserve upstream clean sessions,
  20-second keepalive, five-second reconnect and three retries. Timestamp receipt
  with a monotonic clock, isolate generations, drop retained/pre-subscription
  reports and bound payload/topic lengths and queued callbacks. Full-payload
  MqttLogger calls and unused logger/file-naming sources are removed; the application's bounded runtime logger remains.
- `NetworkManager`: subscribe once to the 24 verified official downlink topics.
  Only accept reports after all subscriptions succeed. Reset authoritative reports
  on connection state changes, and check cancellation between subscriptions so
  shutdown/identity changes do not continue an old multi-topic handshake.
  The event dump writer and its file helpers are removed;
  legacy routing calls retain a no-op observation boundary.
- `GameData.h`: add an authoritative received-report store. Its implementation in
  `src/protocol/rm2026/upstream_reports.cpp` preserves presence and integer widths,
  validates shapes/finite floats, expires individual reports after 1500 ms and
  clears prior state/history when reported identity changes. This project policy
  avoids displaying the upstream seeded defaults and narrowed legacy getters.
  Imported legacy business methods still run, but do not feed the HUD.
- `upstream_backend.*`: own GameData/NetworkManager on one Qt worker thread and
  expose one mutex-protected latest snapshot. The existing controller adapts it
  to the existing QML. QML changes only source labels, identity validity and existing
  callbacks; geometry/resources are unchanged. No per-report GUI task queue or additional executable.

The payload limits, queue budget, worker lifetime, freshness policy and UI adapter
are project decisions. Official fields/topics refer to the local 2026 V2.0.0 PDF
§2.2. They do not establish RM2027 compatibility or physical VT13 acceptance.

M2 imports only `HevcDecoder.h/.cpp` for native HEVC decoding. The original
VideoReceiver is inspected, but its private H264/MQTT branch, shared bind,
unbounded allocation/queue and incomplete-frame forwarding are not adopted.
The existing strict, bounded 2026 reassembly and Annex-B gate feed the decoder
on a dedicated worker; one latest image is consumed by the existing VideoOutput.

Local decoder changes: match public/runtime FFmpeg 7.1.5 ABI versions, provide
required zero input padding, use complete-access-unit parser mode, wait for an
initial IRAP, bound decoding to 1920x1080 pixels, guard short NAL headers and a
parser with no progress, expose decode errors and discard corrupt images before
reset/keyframe recovery. Stream resets restore parser flags. The adapter keeps
the upstream parser/send/receive/sws/QImage pipeline and slice-thread settings.
No private Qt APIs, subprocess re-encoding, H264 substitute or GUI decode.

M3.1 replaces the unused, separate NetworkManager send methods (including raw/
legacy UDP send paths and QML invokables) with one typed `sendCommand` entry.
The project domain variant covers the eleven verified uplinks. Protobuf setters
and SerializeToString follow the imported sender, with canonical V2.0.0 fields,
presence and `MapClickCmd` (not MapClickInfoNotify). Validate documented enums,
16-bit keyboard mask, 30-byte CustomControl, map shapes/finite coordinates and
ASCII; business/role/cost/difficulty eligibility is still pending M3.2.

All topics use the same monotonic interval limiter. Keyboard/CustomControl use
ceil(1s/75)=13,333,334ns, including neutral frames; seven event topics use 100ms,
map uses 500ms/3000ms only with an explicit operator role, and AirSupport uses
the upstream 1s project policy (not an official numeric limit). No sender queue
or automatic command retries. The terminal gate, fresh/matching identity,
configured clientID, current GameStatus/paused state, subscription generation
and explicit business-policy permit precede publishing. The ordinary controller
does not provide that permit yet; UI/control behavior remains unchanged.

M3.2 replaces the M3.1 bounded waitForCompletion with deliveryCompleteCallback,
queued back to the owning worker and checked against its session generation.
Publish admission returns Queued/token separately from asynchronous broker ACK
or delivery unknown. QoS1, retained=false; at most eight in-flight commands and
one second per ACK deadline. Timeout/cancellation destroys the old handle and
its QoS state. Connection loss also destroys it before a clean reconnect, so old
commands cannot be retried on the next handle. The shared ingress guard accepts
only strict two-byte, nonzero-ID PUBACK. PUBACK is not an event/robot receipt.

MainWindow's official key-bit mapping and keyboard/mouse press/delta/reset
behavior are adapted into src/app/operator_input.* with MIT attribution. Window
focus loss, hiding, destruction or a closed terminal gate clears capture. Held
controls remain quarantined until release and a fresh press, including across
window changes. No upstream UI or simulator is copied. Gain is currently one;
unbounded pointer capture and hardware sensitivity remain unverified.

UpstreamBackend consumes a mutex-protected latest input and a priority release
slot on a 14ms worker timer; it also accepts one typed pending event. There is no
per-frame GUI task queue. Rate limits apply equally to releases. Input/event
pending freshness is 100ms; cancellation closes active/pending control sessions,
without bypassing the common gate to send a neutral frame. A frame already
admitted to the transport cannot be retracted or claimed as a physical stop.
Admission/ACK/unknown and bounded slot depths are separate diagnostics. All
limits are project decisions. Ordinary business authority remains unavailable;
the wired software tests alone create a policy permit, with no UI/CLI unlock.


M3.3 adapts PopupOverlayPolicy's explicit respawn pending/free/paid conditions
into src/core/rm2026_commands.cpp, retaining MIT attribution. MainWindow's role
and performance checks are rechecked against official communication V2.0.0 and
competition rules V2.2.0. NetworkManager re-reads its actual GameData Snapshot
for connection/field/life, phase, role, eligibility and fees before rate/publish.
Performance has a preparation/self-check window; revival requires reported death,
not life. Cancellation/exit conditions are action-specific. Upstream UI's ordinary
17mm 100-shot step is not adopted: the official command permits 10-shot multiples.
Unobserved exchange area/quota, remote parameters, deployment area/cooldown and
cross-robot operator registration keep the respective requests blocked. Necessary
checks never create the ordinary controller's full business/source/map permit.

GameData maps four additional report types and optional status/eligibility fields;
explicit zero/false is preserved separately from absence. Future receipt times are
rejected. SentryCtrlResult has no unique request ID: its typed/JSON representation
is a received report, not an ACK correlated to a local request. Penalty persistence
and authoritative release remain unimplemented; stale/missing/zero is not release
evidence. The existing full permit remains unavailable in ordinary use.


M3.4 adapts MainWindow::tryHandleRespawnShortcut into the existing domain action
registry/controller/QML shortcuts. Y requests free revival. Paid revival uses
Shift+F1, a project choice preserving the existing F1 start panel. No Widgets or
new UI controls are imported. No auto repeat; UI occupation disables shortcuts.
The controller constructs typed CommonCommand 3/4 (existing zero-param policy),
never dispatches these as presentation-only actions. Queueing is not revival
success. Business authority remains unavailable in the ordinary controller.

UpstreamBackend prechecks observable business eligibility before occupying its
single event slot; NetworkManager independently re-reads actual reports at send
time. Known ineligible events are rejected at admission. The same identity,
phase, life, eligibility, cost, freshness and mode/source authority gates remain.

M3.5 isolates worker-side event rejection and 100ms expiry from valid ongoing
input. NetworkManager exposes a worker-only read check for keyboard authority,
using the existing common/business gates and actual GameData. It does not encode,
publish or consume rate/in-flight limits. The backend uses the current input
context and calls the check outside its mutex: the subscription-generation
callback acquires that same mutex. Shared gate changes and transport failure
still clear input and cancel the session. event_status/events_rejected describe
local worker outcomes, never gameplay receipts. This scheduling fix is a project
decision, not upstream protocol semantics; no UI or ordinary permit is added.

M3.7 retains a local penaltyReleaseUnverified constraint in the single GameData
owner after an accepted yellow/both-yellow/red/unknown PenaltyInfo. Report expiry,
zero duration/count, replacement by another penalty type, session reset and
reported identity changes do not clear it. UpstreamBackend retains that GameData
across stop/start and endpoint/identity changes, clearing only received reports.
The worker's existing business gate and event admission read the constraint;
diagnostics expose it separately under state.control_constraints, including
offline snapshots. No release method, private topic or countdown release is added.

This backend-wide conservative block is a project decision. It is not a complete
video/map/custom-UI masking policy, per-robot attribution, or persistence across
backend destruction/process exit. Types 4..6 alone do not establish a card lock;
they cannot clear a prior lock. Rejected reports do not mutate the constraint.
Official V2.0.0 section 2.2.1 requires one keyboard source, but the checked schema,
VT03/VT13 manual and pinned source supply no verified source-selection receipt.
The ordinary controller therefore still supplies no business/source/map permit.

M3.8 checks further necessary eligibility without creating new wire fields or
ordinary authority. A normal ammo request cannot exceed the entire team's cap
(1000 17mm / 100 42mm shots, rule V2.2.0 table 5-6); a request below that ceiling
still has no proof of remaining team allowance or exchange area. A remote ammo
purchase requires at least one group's 150 coins, while its param/total fee
remain unverified. Remote healing checks 50+ceil((420-countdown)*20/60), with a
fresh explicit countdown in 0..420 and actual economy. This follows the official
table's parentheses, not upstream ExchangePanel.qml's ceil(minutes)*20 formula.
Sufficient money still returns remote_exchange_parameter_unverified.

Deployment entry requires an explicitly NotDeployed received status before the
existing performance/area/cooldown checks. The deployed exit path, including a
dead hero, is preserved. Local ammo exchange counters and RuneVoicePromptTracker
are not authoritative team allowance/opportunity recovery; they are not used to
open the ordinary gate. See docs/upstream-migration.md section 16 for the remaining
field-level evidence gaps and source comparisons. UI and canonical schema are unchanged.

M3.9 extends the existing domain snapshot with SentryStatusSync and
DartSelectTargetStatusSync and completes RuneStatusSync arms/raw average fields.
Presence is preserved; each new sample replaces the previous fields. Sentry
postures 1..3 and dart targets 1..5/gates 0..2 have explicit unknown semantics
without changing their raw codes. Optional false is retained as a JSON boolean.
The report gate is uint32, unlike the outgoing command's bool open.

The pinned NetworkManager subscribes to SentryStatusSync without a corresponding
message handler. Its dart updater clamps target/gate codes and treats an unchanged
report as a frontend switching receipt; neither behavior is imported. Rune defaults,
non-finite-to-zero conversion, local voice/opportunity counters and inferred team
activation are not used. A raw average is only meaningful for an activating large
rune, and this message supplies no large/small flag. The existing ingress rejects
non-finite values; freshness/reset/disconnect rules apply to all new typed blocks.
These reports grant no operator authority or uniquely correlated request ACK.

M3.10 completes the existing TechCore domain mapper with optional raw
enemy_core_status, remain_time_all and remain_time_step, preserving all eight
wire fields without defaulting absent values or narrowing uint32. Official
V2.0.0 section 2.2.22 makes the enemy status conditional on own level-4
availability and the durations conditional on own level-4 assembly. It supplies
neither active selected difficulty nor duration units. No seconds conversion,
local countdown, completion/timeout inference or request correlation is added.
The pinned engineer QML's assemblyCountdownSeconds/int/default-zero conventions
are not imported. The existing GameData freshness, replacement and reset rules
apply; business eligibility, ordinary permits and UI remain unchanged.

M3.11 maps GlobalSpecialMechanism as ordered uint32 ID/int32-second pairs,
preserving duplicates, unknown IDs and signed bounds after equal-length ingress
validation. Unlike the pinned updater, it does not truncate arrays, clamp negative
seconds, collapse known IDs or emit alerts. A valid empty repeated-list message
replaces the active list with an empty received sample; unknown-only messages and
empty singular-field topics remain rejected. Transport/session/freshness guards
are unchanged. This complete-list interpretation is a local implementation decision.

RobotInjuryStat preserves all ten optional uint32 fields independently, including
killer_id, without merging damage categories or defaulting missing fields to zero.
It describes cumulative damage during one life, not inferred health/life state,
identity, a kill event or a command ACK. Both new blocks use the existing GameData
replacement, expiry and reset rules. UI and ordinary authority remain unchanged.

M3.12 maps RobotPathPlanInfo as optional raw intention/start decimeters/sender ID
and ordered signed offset pairs. Existing ingress enforces paired lengths, at most
49 entries and -128..127 values; accepting a shorter report does not certify a
complete official 49-pair path. The pinned updateSentryPath coordinate guessing,
280/150 normalization, negative clipping and dropped intention/sender are not used.

MapClickInfo preserves all eight optional fields, the exact seven recipient bytes
and finite raw floats. Field mode maps to marking type; type maps to marking mode.
Recipient/enemy IDs do not supply a sender, self identity or operator authority.
The pinned backend lacks this downlink handler and uses legacy MapClickInfoNotify
for outgoing clicks; no legacy alias is imported. Both reports use existing
replacement/expiry/reset behavior, with no UI coordinate transform, drawing,
command correlation or permission change. See migration section 20 for sources.

M3.13 maps CustomByteBlock optional data as opaque uint8 bytes, bounded to 300
bytes by existing ingress. Present-empty replaces the previous payload; missing,
unknown-only, malformed and oversized messages remain rejected. Diagnostic hex
is lossless for zero/non-UTF8 bytes. Raw report bytes also become explicit hex
QString values, so present-empty is a JSON empty string rather than QVariant null.
The pinned video forwarding, 303-byte prefix
stripping and length-based RobotCustomStatus parsing/self-ID fallback are not
imported. Existing expiry/reset/session guards apply; no UI/video/authority changes.

The field/UI audit in migration section 22 distinguishes a typed topic from full
field coverage: 18 fields across static/dynamic/unit/logistics reports still live
only in raw reports. Typed blocks and command entry points do not certify UI
bindings, parameter selection, business authority or real device execution.

M3.14 fills those 18 fields in the existing four domain blocks. Static/dynamic
uint32 values and float cooling rate retain presence and width, without the pinned
uint8/16 narrowing, experience aggregation or chassis-energy-to-power alias.
Global unit ammunition remains an ordered signed raw array, not guessed health
slots; the official paragraph does not give its order/length. Existing 0-or-10
ingress policy is preserved as a local compatibility check. Negative values are
not clamped. Relative team damage totals remain separate from per-life injury.

Economy retains optional uint64 total income and raw tech/encryption levels;
diagnostic income is always a decimal string, including zero and UINT64_MAX.
Remaining-needed experience and out-of-combat countdown do not infer progress,
life, eligibility or command receipts. A field-reference audit now covers every
field of all subscribed topics; semantic/UI/authority/device gaps remain separate.
No schema, subscription, HUD or ordinary permission changes are introduced.
