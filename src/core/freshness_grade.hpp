#pragma once

#include <cstdint>

namespace rm27 {

// 数据新鲜度分级（项目决策）。
//
// 参考实现用「分级权重」而不是二值判断，好处是降级可以逐级表达（读数渐隐、颜色递进），
// 而不是从正常突然跳到未知。这里只做**显示与标注**用：指令安全门仍然只看
// `TerminalState::referee.fresh`（600ms 过期），分级不参与放行判断，避免悄悄改变安全行为。
enum class FreshnessGrade {
    Unknown,   // 还没有任何数据
    Fresh,     // 年龄 < 300ms
    Degraded,  // 300-800ms
    Stale,     // 800-1500ms
    Expired,   // >= 1500ms
};

inline constexpr std::int64_t kFreshUntilMs = 300;
inline constexpr std::int64_t kDegradedUntilMs = 800;
inline constexpr std::int64_t kStaleUntilMs = 1500;

// ageMs < 0 表示「从未收到」→ Unknown。
[[nodiscard]] FreshnessGrade gradeForAge(std::int64_t ageMs) noexcept;

// 分级权重：1.0 / 0.7 / 0.4 / 0，供将来的降级显示与推荐使用。
[[nodiscard]] double freshnessScale(FreshnessGrade grade) noexcept;

[[nodiscard]] const char* freshnessGradeText(FreshnessGrade grade) noexcept;

}  // namespace rm27
