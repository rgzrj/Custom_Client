#include "core/input_mapping.hpp"

#include <algorithm>
#include <cctype>

namespace rm27 {
namespace {

const std::vector<InputAction>& actionTable()
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

bool isModifier(const std::string& token)
{
    return token == "Ctrl" || token == "Shift" || token == "Alt" || token == "Meta";
}

bool isSingleKey(const std::string& token)
{
    if (token.size() == 1) {
        const unsigned char character = static_cast<unsigned char>(token[0]);
        return std::isalnum(character) != 0;
    }
    if (token.size() >= 2 && token.size() <= 3 && (token[0] == 'F' || token[0] == 'f')) {
        const std::string digits = token.substr(1);
        return std::all_of(digits.begin(), digits.end(),
                           [](const char character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; });
    }
    static const char* named[] = {"Space", "Esc", "Tab", "Enter", "Up", "Down", "Left", "Right"};
    return std::any_of(std::begin(named), std::end(named),
                       [&token](const char* candidate) { return token == candidate; });
}

}  // namespace

const std::vector<InputAction>& defaultInputActions() noexcept
{
    return actionTable();
}

const InputAction* findInputAction(const std::string& id) noexcept
{
    const auto& actions = actionTable();
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [&id](const InputAction& action) { return action.id == id; });
    return found == actions.end() ? nullptr : &(*found);
}

bool isValidKeySequence(const std::string& text) noexcept
{
    if (text.empty() || text.size() > 32 || text.front() == ' ' || text.back() == ' ') {
        return false;
    }
    std::size_t begin = 0;
    bool sawKey = false;
    while (begin <= text.size()) {
        const std::size_t plus = text.find('+', begin);
        const std::string token = text.substr(begin, plus == std::string::npos ? std::string::npos : plus - begin);
        if (token.empty()) {
            return false;
        }
        if (isModifier(token)) {
            // 修饰键可以连续出现。
        } else if (!sawKey && isSingleKey(token)) {
            sawKey = true;
        } else {
            return false;
        }
        if (plus == std::string::npos) {
            break;
        }
        begin = plus + 1;
    }
    return sawKey;
}

std::string actionHint(const InputAction& action)
{
    if (action.defaultKeys.empty()) {
        return action.label;
    }
    return action.label + " [" + action.defaultKeys + "]";
}

}  // namespace rm27
