#pragma once

namespace rm27 {

enum class TerminalMode {
    HeavyOperator,
    IntegratedCommand,
    Fallback,
    Simulation,
    Replay,
};

enum class MatchPhase {
    Boot,
    Preparation,
    SelfCheck,
    Countdown,
    Match,
    Ended,
};

enum class CommandKind {
    RobotMotion,
    EventOperation,
};

enum class GateBlock {
    None,
    // 界面正在占用操作视野（启动面板、弹窗、对话框等）：此时不允许下发任何指令。
    // 这条把规则要求的「遮挡」落成结构性门，而不是只画一层遮罩。
    UiOccupied,
    OfflineMode,
    PhaseBlocked,
    RefereeDisconnected,
    RefereeDataStale,
    ControllerDisconnected,
    ControlsDisarmed,
};

struct LinkState {
    bool connected{false};
    bool fresh{false};
};

struct TerminalState {
    TerminalMode mode{TerminalMode::HeavyOperator};
    MatchPhase phase{MatchPhase::Boot};
    LinkState referee{};
    LinkState video{};
    LinkState controller{};
    bool controlsArmed{false};
    // 由表现层上报：启动面板、弹窗、对话框等正在占用操作视野。
    bool uiOccupied{false};
};

struct GateDecision {
    bool allowed{false};
    GateBlock block{GateBlock::OfflineMode};
};

[[nodiscard]] GateDecision evaluateCommand(const TerminalState& state, CommandKind kind) noexcept;

}  // namespace rm27

