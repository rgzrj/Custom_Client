// SPDX-License-Identifier: MIT
// Respawn conditions adapted from RM26_Client e291f2f PopupOverlayPolicy.
// Copyright (c) 2025-2026 Fudan University EGA Team. See third_party/rm26_client/LICENSE.
#include "core/rm2026_commands.hpp"
#include <type_traits>

namespace rm27::historical::rm2026 {
namespace {
template<class T> bool fresh(const std::optional<Received<T>> &value,
                            std::chrono::steady_clock::time_point now) {
    return value && now >= value->at && now - value->at < std::chrono::milliseconds(1500);
}
bool infantry(unsigned role) { return role >= 3 && role <= 5; }
const char *budget(const Snapshot &s, std::uint64_t cost,
                   std::chrono::steady_clock::time_point now) {
    if (!fresh(s.economy, now) || !s.economy->value.remaining) return "economy_unknown";
    return *s.economy->value.remaining >= cost ? "" : "insufficient_gold";
}
}

bool commandPhaseAllowed(const Command &command, Phase phase) {
    if (std::holds_alternative<PerformanceCommand>(command))
        return phase == Phase::Preparation || phase == Phase::SelfCheck;
    return phase == Phase::Playing;
}

const char *businessCommandBlock(const Command &command, const Snapshot &s,
                                const CommandContext &context,
                                std::chrono::steady_clock::time_point now) {
    if (!fresh(s.selfConfig, now) || !s.selfConfig->value.robotId
        || s.selfConfig->value.robotId != context.requestedId
        || s.selfConfig->value.robotId != context.reportedId) return "current_identity_unknown_or_changed";
    if (!fresh(s.game, now) || !commandPhaseAllowed(command, s.game->value.phase)
        || s.game->value.paused != std::optional<bool>(false)) return "current_game_unavailable_or_paused";
    const auto &robot = s.selfConfig->value;
    const unsigned id = *robot.robotId, role = id % 100;
    if (!((id >= 1 && id <= 7) || (id >= 101 && id <= 107))) return "identity_unsupported";
    if (robot.connectionState != std::optional<std::uint32_t>(1)) return "robot_connection_unconfirmed";
    const bool performance = std::holds_alternative<PerformanceCommand>(command);
    if (!performance && robot.fieldState != std::optional<std::uint32_t>(0)) return "robot_not_on_field";
    if (s.penaltyReleaseUnverified || (s.penalty && (s.penalty->value.kind == PenaltyKind::YellowCard
        || s.penalty->value.kind == PenaltyKind::BothYellowCards
        || s.penalty->value.kind == PenaltyKind::RedCard
        || s.penalty->value.kind == PenaltyKind::Unknown))) return "penalty_release_unverified";
    const auto *common = std::get_if<CommonCommand>(&command);
    const bool respawn = common && (common->type == 3 || common->type == 4);
    const auto *assembly = std::get_if<AssemblyCommand>(&command);
    const auto *deployment = std::get_if<DeployCommand>(&command);
    const bool cancellation = (assembly && assembly->operation == 2)
        || (deployment && deployment->mode == 0)
        || (std::get_if<AirSupportCommand>(&command) && std::get<AirSupportCommand>(command).id == 0);
    // Conservative project policy; engineer temporary activation needs separate evidence.
    if (!performance && !respawn && !cancellation
        && robot.aliveState != std::optional<std::uint32_t>(1)) return "robot_life_unconfirmed";
    return std::visit([&](const auto &value) -> const char * {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, KeyboardMouseCommand> || std::is_same_v<T, CustomControlCommand>) {
            return "";
        } else if constexpr (std::is_same_v<T, MapClickCommand>) {
            bool recipient = false;
            for (std::size_t i = 0; i < value.robotIds.size(); ++i) {
                const auto target = value.robotIds[i];
                if (!target) continue;
                recipient = true;
                if ((target > 100) != (id > 100)) return "map_recipient_wrong_team";
                for (std::size_t j = 0; j < i; ++j)
                    if (value.robotIds[j] == target) return "map_duplicate_recipient";
            }
            if (!value.sendAll && !recipient) return "map_recipient_missing";
            if (value.markingMode == 2 && (value.enemyId > 100) == (id > 100)) return "map_target_not_enemy";
            return ""; // Common gate still requires explicit role and calibration.
        } else if constexpr (std::is_same_v<T, PerformanceCommand>) {
            if (value.sentryControl) return "sentry_operator_unverified";
            if (role == 1) {
                if (!value.shooter || !value.chassis || *value.shooter != *value.chassis
                    || (*value.shooter != 3 && *value.shooter != 4)) return "hero_requires_whole_robot_performance";
            } else if (infantry(role)) {
                if ((value.shooter && *value.shooter > 2) || (value.chassis && *value.chassis > 2))
                    return "infantry_performance_out_of_range";
            } else return "performance_role_unavailable";
            return "";
        } else if constexpr (std::is_same_v<T, CommonCommand>) {
            if (respawn) {
                if (role == 6) return "respawn_role_unavailable";
                if (robot.aliveState != std::optional<std::uint32_t>(2)) return "robot_not_reported_dead";
                if (value.parameter) return "respawn_parameter_unverified";
                if (!fresh(s.respawn, now) || s.respawn->value.pending != std::optional<bool>(true))
                    return "respawn_pending_unconfirmed";
                const auto &r = s.respawn->value;
                if (value.type == 3)
                    return r.reportedCanFree == std::optional<bool>(true) ? "" : "free_respawn_not_allowed";
                if (r.reportedCanPay != std::optional<bool>(true)) return "paid_respawn_not_allowed";
                if (!r.goldCost || !*r.goldCost) return "respawn_cost_unconfirmed";
                return budget(s, *r.goldCost, now);
            }
            if (value.type == 1 || value.type == 2) {
                if (value.type == 1 ? !(infantry(role) || role == 7) : role != 1) return "ammo_role_mismatch";
                if (!value.parameter || (value.type == 1 && value.parameter % 10)) return "ammo_quantity_invalid";
                // Rule V2.2.0 table 5-6: even one request cannot exceed the whole-team cap.
                // This is not a measurement of the team's remaining exchange allowance.
                if (value.parameter > (value.type == 1 ? 1000u : 100u)) return "ammo_quantity_exceeds_team_limit";
                const auto cost = std::uint64_t(value.parameter) * (value.type == 2 ? 10 : 1);
                const auto *block = budget(s, cost, now);
                if (*block) return block;
                return "exchange_area_and_team_quota_unverified";
            }
            if (value.type == 5 || value.type == 6) {
                if (!(role == 1 || infantry(role) || role == 7)) return "remote_exchange_role_unavailable";
                if (!fresh(s.selfDynamic, now) || s.selfDynamic->value.outOfCombat != std::optional<bool>(true))
                    return "out_of_combat_unconfirmed";
                const auto permitted = value.type == 5 ? s.selfDynamic->value.canRemoteAmmo : s.selfDynamic->value.canRemoteHeal;
                if (permitted != std::optional<bool>(true)) return "remote_exchange_not_allowed";
                std::uint64_t cost = 150; // Minimum one remote-ammo group; param semantics remain unknown.
                if (value.type == 6) {
                    const auto sec = s.game->value.countdownSec;
                    if (!sec || *sec < 0 || *sec > 420) return "remote_heal_cost_unknown";
                    // Table 5-6 rounds after multiplication by 20, not before it.
                    cost = 50 + (std::uint64_t(420 - *sec) * 20 + 59) / 60;
                }
                const auto *block = budget(s, cost, now);
                if (*block) return block;
                return "remote_exchange_parameter_unverified";
            }
            return "invalid_common_command";
        } else if constexpr (std::is_same_v<T, AssemblyCommand>) {
            if (role != 2) return "assembly_role_mismatch";
            if (!fresh(s.techCore, now)) return "tech_core_unknown";
            const auto &core = s.techCore->value;
            if (value.operation == 2)
                return core.basicState && (*core.basicState == 2 || *core.basicState == 3) ? "" : "assembly_not_active";
            if (!core.maximumDifficulty || !value.difficulty || value.difficulty > 4
                || value.difficulty > *core.maximumDifficulty) return "assembly_difficulty_unavailable";
            if (value.operation == 0)
                return core.basicState == std::optional<std::uint32_t>(1)
                    && core.putIn == std::optional<std::uint32_t>(0) ? "" : "assembly_start_not_ready";
            if (core.basicState != std::optional<std::uint32_t>(3) || core.putIn != std::optional<std::uint32_t>(1)
                || (value.difficulty >= 2 && core.move != std::optional<std::uint32_t>(1))
                || (value.difficulty >= 3 && core.rotate != std::optional<std::uint32_t>(1))) return "assembly_steps_incomplete";
            return ""; // Server validates the actual selected difficulty and assembly geometry.
        } else if constexpr (std::is_same_v<T, DeployCommand>) {
            if (role != 1) return "deploy_role_mismatch";
            if (!fresh(s.deployment, now)) return "deployment_unknown";
            if (!value.mode) return s.deployment->value.state == DeployState::Deployed ? "" : "robot_not_deployed";
            if (s.deployment->value.state != DeployState::NotDeployed) return "deployment_not_confirmed_inactive";
            if (robot.shooter != std::optional<std::uint32_t>(4) || robot.chassis != std::optional<std::uint32_t>(4))
                return "deploy_requires_ranged_hero";
            return "deployment_area_or_cooldown_unverified";
        } else if constexpr (std::is_same_v<T, RuneCommand>) {
            if (!infantry(role)) return "rune_operator_unverified";
            return fresh(s.rune, now) && s.rune->value.code == std::optional<std::uint32_t>(1) ? "" : "rune_not_inactive";
        } else if constexpr (std::is_same_v<T, AirSupportCommand>) {
            if (role != 6 || context.mapOperator != MapOperator::Gimbal) return "air_support_operator_unverified";
            if (!fresh(s.airSupport, now)) return "air_support_unknown";
            const auto &air = s.airSupport->value;
            if (!value.id) return air.status == std::optional<std::uint32_t>(1) ? "" : "air_support_not_active";
            if (value.id == 3) {
                if (air.shooterStatus != std::optional<std::uint32_t>(2)) return "air_counter_not_releasable";
                const auto sec = s.game->value.countdownSec;
                if (!sec || *sec < 0 || *sec > 420 || !robot.level || *robot.level < 1 || *robot.level > 10)
                    return "air_counter_cost_unknown";
                const std::uint64_t rounds = (420 - *sec + 59) / 60;
                return budget(s, 2 * (rounds * 80 + std::uint64_t(*robot.level) * 20), now);
            }
            if (air.status != std::optional<std::uint32_t>(0)) return "air_support_already_active_or_unknown";
            if (!air.freeTime) return "air_support_free_time_unknown";
            if (*air.freeTime) return ""; // Paid request also uses free time first (PDF74).
            return value.id == 1 ? "air_support_free_time_exhausted" : budget(s, 1, now);
        } else if constexpr (std::is_same_v<T, SentryCommand>) {
            return "sentry_operator_unverified";
        } else {
            return "dart_operator_unverified";
        }
    }, command);
}
}
