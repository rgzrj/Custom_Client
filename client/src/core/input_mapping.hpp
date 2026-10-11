#pragma once

#include <string>
#include <vector>

#include "core/terminal_state.hpp"

namespace rm27 {

// 有线输入的动作映射表（项目决策）。
//
// 界面文案与按键提示**同源**：面板标题、按钮提示都由 `actionHint()` 生成，
// 改键位不会忘记改提示（参考实现的做法）。表里只有有线键盘动作，
// 不含任何无线或真实控制协议语义；动作最终仍要过 `evaluateCommand()` 闸门。
struct InputAction {
    std::string id;             // 稳定标识，不随键位改变
    std::string label;          // 界面文案
    std::string group;          // 所属区域，用于分组显示「标题即快捷键表」
    std::string defaultKeys;    // 默认绑定：Qt 便携式序列文本（如 Ctrl+Shift+O）
    CommandKind kind{CommandKind::RobotMotion};
};

[[nodiscard]] const std::vector<InputAction>& defaultInputActions() noexcept;

[[nodiscard]] const InputAction* findInputAction(const std::string& id) noexcept;

// "标签 [按键]"：面板标题与按钮提示统一用它生成。
[[nodiscard]] std::string actionHint(const InputAction& action);

}  // namespace rm27
