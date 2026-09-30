#include "core/terminal_state.hpp"

namespace rm27 {

GateDecision evaluateCommand(const TerminalState& state, const CommandKind kind) noexcept
{
    if (state.mode == TerminalMode::Simulation || state.mode == TerminalMode::Replay) {
        return {false, GateBlock::OfflineMode};
    }

    // 界面占用优先于阶段与链路判断：看不到画面时不该还能操作。
    if (state.uiOccupied) {
        return {false, GateBlock::UiOccupied};
    }

    if (state.phase != MatchPhase::Match) {
        return {false, GateBlock::PhaseBlocked};
    }

    if (!state.referee.connected) {
        return {false, GateBlock::RefereeDisconnected};
    }

    if (!state.referee.fresh) {
        return {false, GateBlock::RefereeDataStale};
    }

    if (kind == CommandKind::RobotMotion) {
        if (!state.controller.connected) {
            return {false, GateBlock::ControllerDisconnected};
        }
        if (!state.controlsArmed) {
            return {false, GateBlock::ControlsDisarmed};
        }
    }

    return {true, GateBlock::None};
}

}  // namespace rm27

