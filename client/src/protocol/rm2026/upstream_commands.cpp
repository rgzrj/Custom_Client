// SPDX-License-Identifier: MIT
// Serialization adapted from ClearWei/RM26_Client e291f2f NetworkManager.cpp.
// Copyright (c) 2025-2026 Fudan University EGA Team. See third_party/rm26_client/LICENSE.
#include "protocol/rm2026/upstream_commands.hpp"
#include "robomaster.pb.h"
#include <cmath>
#include <type_traits>

namespace rm27::protocol::rm2026 {
using namespace historical::rm2026;
namespace {
template<class T> QByteArray serialize(const T &message) {
    std::string buffer;
    if (!message.SerializeToString(&buffer)) return {};
    return QByteArray(buffer.data(), int(buffer.size()));
}
bool robotId(std::uint32_t id) {
    return (id >= 1 && id <= 7) || (id >= 101 && id <= 107);
}
}
EncodedCommand encodeCommand(const Command &command, MapOperator mapOperator) {
    EncodedCommand out;
    out.interval = std::chrono::milliseconds(100);
    std::visit([&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, KeyboardMouseCommand>) {
            out.topic = "KeyboardMouseControl";
            out.interval = std::chrono::nanoseconds(13'333'334); // ceil(1 second / 75).
            if (value.keys > 0xffff) { out.error = "keyboard_mask_out_of_range"; return; }
            robomaster::KeyboardMouseControl msg;
            msg.set_mouse_x(value.x); msg.set_mouse_y(value.y); msg.set_mouse_z(value.wheel);
            msg.set_left_button_down(value.left); msg.set_right_button_down(value.right);
            msg.set_mid_button_down(value.middle); msg.set_keyboard_value(value.keys);
            out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, CustomControlCommand>) {
            out.topic = "CustomControl";
            out.interval = std::chrono::nanoseconds(13'333'334);
            if (value.data.size() > 30) { out.error = "custom_control_exceeds_30_bytes"; return; }
            robomaster::CustomControl msg;
            msg.set_data(std::string(value.data.begin(), value.data.end()));
            out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, MapClickCommand>) {
            out.topic = "MapClickCmd";
            if (mapOperator == MapOperator::Unknown) { out.error = "map_operator_unknown"; return; }
            out.interval = mapOperator == MapOperator::Gimbal
                ? std::chrono::milliseconds(500) : std::chrono::milliseconds(3000);
            const bool asciiValid = (value.ascii >= 'C' && value.ascii <= 'L')
                || value.ascii == 'N' || value.ascii == 'O'
                || (value.ascii >= 'Q' && value.ascii <= 'Z');
            if (value.sendAll > 2 || value.markingType < 1 || value.markingType > 4
                || value.markingMode < 1 || value.markingMode > 2
                || !std::isfinite(value.x) || !std::isfinite(value.y)
                || (value.markingType == 4 ? !asciiValid : value.ascii != 0)
                || (value.markingMode == 1 ? value.enemyId != 0
                    : !robotId(value.enemyId) || value.x != 0 || value.y != 0)) {
                out.error = "invalid_map_fields"; return;
            }
            for (auto id : value.robotIds) if (id && !robotId(id)) {
                out.error = "invalid_map_recipient"; return;
            }
            robomaster::MapClickCmd msg;
            msg.set_is_send_all(value.sendAll);
            msg.set_robot_id(value.robotIds.data(), value.robotIds.size());
            msg.set_mode(value.markingType); msg.set_enemy_id(value.enemyId);
            msg.set_ascii(value.ascii); msg.set_type(value.markingMode);
            msg.set_map_x(value.x); msg.set_map_y(value.y); out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, AssemblyCommand>) {
            out.topic = "AssemblyCommand";
            if (value.operation > 2) { out.error = "invalid_assembly_operation"; return; }
            // No guessed difficulty maximum: eligibility needs fresh TechCore report.
            robomaster::AssemblyCommand msg;
            msg.set_operation(value.operation); msg.set_difficulty(value.difficulty);
            out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, PerformanceCommand>) {
            out.topic = "RobotPerformanceSelectionCommand";
            if ((!value.shooter && !value.chassis && !value.sentryControl)
                || (value.shooter && (*value.shooter < 1 || *value.shooter > 4))
                || (value.chassis && (*value.chassis < 1 || *value.chassis > 4))
                || (value.sentryControl && *value.sentryControl > 1)) {
                out.error = "invalid_performance_codes"; return;
            }
            robomaster::RobotPerformanceSelectionCommand msg;
            if (value.shooter) msg.set_shooter(*value.shooter);
            if (value.chassis) msg.set_chassis(*value.chassis);
            if (value.sentryControl) msg.set_sentry_control(*value.sentryControl);
            out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, CommonCommand>) {
            out.topic = "CommonCommand";
            if (value.type < 1 || value.type > 6 || (value.type == 1 && value.parameter % 10)) {
                out.error = "invalid_common_command"; return;
            }
            robomaster::CommonCommand msg;
            msg.set_cmd_type(value.type); msg.set_param(value.parameter); out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, DeployCommand>) {
            out.topic = "HeroDeployModeEventCommand";
            if (value.mode > 1) { out.error = "invalid_deploy_mode"; return; }
            robomaster::HeroDeployModeEventCommand msg;
            msg.set_mode(value.mode); out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, RuneCommand>) {
            out.topic = "RuneActivateCommand";
            if (value.activate != 1) { out.error = "invalid_rune_activation"; return; }
            robomaster::RuneActivateCommand msg;
            msg.set_activate(value.activate); out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, DartCommand>) {
            out.topic = "DartCommand";
            if (value.target < 1 || value.target > 5) { out.error = "invalid_dart_target"; return; }
            robomaster::DartCommand msg;
            msg.set_target_id(value.target); msg.set_open(value.open); msg.set_launch_confirm(value.confirm);
            out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, SentryCommand>) {
            out.topic = "SentryCtrlCommand";
            if (value.id != 1 && (value.id < 3 || value.id > 12)) {
                out.error = "invalid_sentry_command"; return;
            }
            robomaster::SentryCtrlCommand msg;
            msg.set_command_id(value.id); out.payload = serialize(msg);
        } else if constexpr (std::is_same_v<T, AirSupportCommand>) {
            out.topic = "AirSupportCommand";
            out.interval = std::chrono::seconds(1); // Project policy, official table says triggered.
            if (value.id > 3) { out.error = "invalid_air_support_command"; return; }
            robomaster::AirSupportCommand msg;
            msg.set_command_id(value.id); out.payload = serialize(msg);
        }
    }, command);
    if (out.error.isEmpty() && out.payload.isEmpty()) out.error = "serialization_failed";
    return out;
}
QString commandBlock(const Command &command, const CommandContext &context) {
    const bool motion = std::holds_alternative<KeyboardMouseCommand>(command)
        || std::holds_alternative<CustomControlCommand>(command);
    auto terminal = context.terminal;
    // Only performance selection has a documented preparation/self-check window.
    // The worker separately verifies the actual received GameStatus phase.
    if (std::holds_alternative<PerformanceCommand>(command)
        && (terminal.phase == MatchPhase::Preparation || terminal.phase == MatchPhase::SelfCheck))
        terminal.phase = MatchPhase::Match;
    else if (std::holds_alternative<PerformanceCommand>(command)) return "performance_phase_blocked";
    const auto gate = evaluateCommand(terminal,
        motion ? CommandKind::RobotMotion : CommandKind::EventOperation);
    if (!gate.allowed) return QStringLiteral("terminal_gate_%1").arg(int(gate.block));
    if (!context.identityFresh || !context.requestedId || !context.reportedId)
        return "identity_unknown";
    if (context.requestedId != context.reportedId) return "identity_mismatch";
    if (!robotId(*context.reportedId)) return "identity_unsupported";
    if (!context.businessPolicyReady) return "business_policy_unavailable";
    if (std::holds_alternative<KeyboardMouseCommand>(command) && !context.keyboardSourceSelected)
        return "keyboard_source_unconfirmed";
    if (std::holds_alternative<MapClickCommand>(command)) {
        if (context.mapOperator == MapOperator::Unknown) return "map_operator_unknown";
        if (!context.mapCalibrated) return "map_uncalibrated";
    }
    return {};
}
}
