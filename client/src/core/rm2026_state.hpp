#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rm27::historical::rm2026 {

// Historical read-only values, not TerminalState or authority to open a gate.
enum class Team { Unknown, Red, Blue };
enum class OutpostLife { Unknown, Alive, Destroyed };
struct SelfConfig {
    std::optional<std::uint32_t> robotId, maxHealth, level;
    Team team{Team::Unknown}; // Reported identity, not authenticated login.
};
struct SelfDynamic {
    std::optional<std::uint32_t> health, permittedShots;
    std::optional<float> heat, lastProjectileFireRate; // No assumed unit conversion.
};
struct Structure {
    std::optional<std::uint32_t> health, status, shield;
    OutpostLife life{OutpostLife::Unknown}; // Used only for outposts.
};
struct RobotHealth {
    std::uint32_t roleId;
    std::optional<std::uint32_t> health;
};
struct SideUnits {
    Structure base, outpost;
    // Relative ally/enemy order in PDF 53; no aerial HP or invented maxima.
    std::array<RobotHealth, 5> robots{{{1, {}}, {2, {}}, {3, {}}, {4, {}}, {7, {}}}};
};
struct Units { SideUnits ally, enemy; };
struct Economy { std::optional<std::uint32_t> remaining; };
enum class Phase { Unknown = -1, NotStarted = 0, Preparation, SelfCheck, Countdown, Playing, Settlement };
struct Game {
    std::optional<std::uint32_t> currentRound, totalRounds, redScore, blueScore, stageCode;
    Phase phase{Phase::Unknown};
    std::optional<std::int32_t> countdownSec, elapsedSec;
    std::optional<bool> paused;
    std::optional<std::uint32_t> reportedResult, reportedEndReason;
    // Meaningful only during settlement, with known codes (PDF 52).
    std::optional<std::uint32_t> result, endReason;
};
enum class PenaltyKind { Unknown = 0, YellowCard, BothYellowCards, RedCard, Power, Heat, FireRate };
struct Penalty {
    std::optional<std::uint32_t> typeCode, effectSec, total;
    PenaltyKind kind{PenaltyKind::Unknown};
    // No "cleared" or "active" flag: zero/absence/staleness is not release evidence.
};
enum class ModuleState { Unknown = -1, Offline = 0, Online, InstallationOffline };
struct Module {
    std::optional<std::uint32_t> code;
    ModuleState state{ModuleState::Unknown};
};
struct Modules {
    Module powerManager, rfid, lightStrip, smallShooter, bigShooter, uwb, armor;
    Module videoTransmission, capacitor, mainController, laserDetectionModule;
    // Module online is not proof of live video, MQTT health or control authority.
};
struct Respawn {
    std::optional<bool> pending, reportedCanFree, reportedCanPay;
    std::optional<std::uint32_t> requiredProgress, currentProgress, goldCost;
    // Reported eligibility only; no inferred life state or command permission.
};
enum class EventKind {
    Unknown = 0, Kill, OutpostDestroyed, LargeEnergyResult, EnergyActivated,
    AllySniperDamage, EnemySniperDamage, EnemyAirSupport, EnemyAirSupportCountered,
    DartHit, EnemyDartGateOpened, BaseAttacked, EnemyOutpostStopped,
    EnemyBaseArmorOpened, EnemyAssemblyForcedExit, AssemblyResult
};
struct EventReport {
    std::optional<std::int32_t> id;
    std::optional<std::string> param; // Raw parameter; no gameplay changes inferred.
    EventKind kind{EventKind::Unknown};
};
struct EventEntry {
    std::uint64_t sequence; // Local receipt order, not an official event identifier.
    EventReport value; // Bounded display copy; latestEvent retains the original parameter.
};
struct Position {
    std::optional<float> x, y, z, yawDeg; // World XYZ; yaw degrees, north = 0.
    std::optional<std::uint32_t> reportedRobotId;
    // No assumed XYZ unit/origin, map conversion or missing-ID self fallback.
};

enum class BuffKind { Unknown = 0, Attack, DefenseOrVulnerability, Cooling,
    ChassisPower, Recovery, ExchangeableShots, TerrainPrewarning };
struct BuffReport {
    std::optional<std::uint32_t> reportedRobotId, typeCode, maxTime, leftTime;
    std::optional<std::int32_t> level; // Signed, type-specific value; no unit conversion.
    BuffKind kind{BuffKind::Unknown};
    // Latest report only, not all active buffs or a missing-ID self fallback.
};
struct Performance {
    std::optional<std::uint32_t> shooter, chassis, sentryControl;
    // Raw reported codes, not authorization or confirmation of a local request.
};
enum class DeployState { Unknown = -1, NotDeployed = 0, Deployed };
struct Deployment {
    std::optional<std::uint32_t> code;
    DeployState state{DeployState::Unknown};
};

template<class T> struct Received {
    T value;
    std::chrono::steady_clock::time_point at;
};
struct Snapshot {
    // Each topic expires independently. Missing block/field remains unknown.
    std::optional<Received<SelfConfig>> selfConfig;
    std::optional<Received<SelfDynamic>> selfDynamic;
    std::optional<Received<Units>> units;
    std::optional<Received<Economy>> economy;
    std::optional<Received<Game>> game;
    std::optional<Received<Penalty>> penalty;
    std::optional<Received<Modules>> modules;
    std::optional<Received<Respawn>> respawn;
    std::optional<Received<EventReport>> latestEvent;
    std::vector<EventEntry> recentEvents; // Last 24 accepted events in this session; not complete history.
    std::uint64_t eventHistoryEvicted{0};
    std::optional<Received<Position>> position;
    std::optional<Received<BuffReport>> latestBuff;
    std::optional<Received<Performance>> performance;
    std::optional<Received<Deployment>> deployment;
};

} // namespace rm27::historical::rm2026
