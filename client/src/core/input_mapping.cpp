#include "core/input_mapping.hpp"

#include <algorithm>

namespace rm27 {

const std::vector<InputAction>& defaultInputActions() noexcept
{
    // 只登记有线键盘动作；键位为项目决策，与任何官方协议无关。
    static const std::vector<InputAction> actions{
        {"panel.start", "启动面板", "界面", "F1", CommandKind::EventOperation},
        {"link.offlineDrill", "离线演练进入", "界面", "Ctrl+Shift+O", CommandKind::EventOperation},
        {"video.localPlayback", "打开本地回放", "画面", "Ctrl+O", CommandKind::EventOperation},
        {"video.simulationUdp", "本机 UDP 联调", "画面", "Ctrl+U", CommandKind::EventOperation},
        {"controls.toggleArmed", "解锁 / 锁定控制", "控制", "F2", CommandKind::RobotMotion},
        {"gate.inspectMotion", "检查指令门", "控制", "F3", CommandKind::RobotMotion},
        {"mode.toggleCustomClient", "切换自定义客户端", "控制", "F4", CommandKind::EventOperation},
    };
    return actions;
}

const InputAction* findInputAction(const std::string& id) noexcept
{
    const auto& actions = defaultInputActions();
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [&id](const InputAction& action) { return action.id == id; });
    return found == actions.end() ? nullptr : &(*found);
}

std::string actionHint(const InputAction& action)
{
    if (action.defaultKeys.empty()) {
        return action.label;
    }
    return action.label + " [" + action.defaultKeys + "]";
}

}  // namespace rm27
