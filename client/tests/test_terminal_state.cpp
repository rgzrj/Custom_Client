#include "core/terminal_state.hpp"

#include <cassert>
#include <iostream>

using namespace rm27;

int main()
{
    TerminalState state;
    state.phase = MatchPhase::Match;
    state.referee = {true, true};
    state.controller = {true, true};

    auto decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(!decision.allowed);
    assert(decision.block == GateBlock::ControlsDisarmed);

    state.controlsArmed = true;
    decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(decision.allowed);
    assert(decision.block == GateBlock::None);

    state.phase = MatchPhase::Countdown;
    decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(decision.block == GateBlock::PhaseBlocked);

    state.phase = MatchPhase::Match;
    state.referee.fresh = false;
    decision = evaluateCommand(state, CommandKind::EventOperation);
    assert(decision.block == GateBlock::RefereeDataStale);

    state.referee.fresh = true;
    state.mode = TerminalMode::Simulation;
    decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(decision.block == GateBlock::OfflineMode);

    // 界面占用优先于阶段与链路判断：看不到画面时不该还能操作。
    state.mode = TerminalMode::HeavyOperator;
    state.uiOccupied = true;
    decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(!decision.allowed);
    assert(decision.block == GateBlock::UiOccupied);
    decision = evaluateCommand(state, CommandKind::EventOperation);
    assert(decision.block == GateBlock::UiOccupied);

    // 即使阶段被挡，界面占用仍是先报告的原因（优先级更靠前）。
    state.phase = MatchPhase::Countdown;
    decision = evaluateCommand(state, CommandKind::EventOperation);
    assert(decision.block == GateBlock::UiOccupied);

    state.uiOccupied = false;
    state.phase = MatchPhase::Match;
    decision = evaluateCommand(state, CommandKind::RobotMotion);
    assert(decision.allowed);

    std::cout << "rm27 core checks passed\n";
    return 0;
}
