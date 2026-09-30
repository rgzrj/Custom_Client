#include "core/freshness_grade.hpp"

namespace rm27 {

FreshnessGrade gradeForAge(const std::int64_t ageMs) noexcept
{
    if (ageMs < 0) {
        return FreshnessGrade::Unknown;
    }
    if (ageMs < kFreshUntilMs) {
        return FreshnessGrade::Fresh;
    }
    if (ageMs < kDegradedUntilMs) {
        return FreshnessGrade::Degraded;
    }
    if (ageMs < kStaleUntilMs) {
        return FreshnessGrade::Stale;
    }
    return FreshnessGrade::Expired;
}

double freshnessScale(const FreshnessGrade grade) noexcept
{
    switch (grade) {
    case FreshnessGrade::Fresh:
        return 1.0;
    case FreshnessGrade::Degraded:
        return 0.7;
    case FreshnessGrade::Stale:
        return 0.4;
    case FreshnessGrade::Expired:
        return 0.0;
    case FreshnessGrade::Unknown:
        return 0.0;
    }
    return 0.0;
}

const char* freshnessGradeText(const FreshnessGrade grade) noexcept
{
    switch (grade) {
    case FreshnessGrade::Fresh:
        return "新鲜";
    case FreshnessGrade::Degraded:
        return "轻微延迟";
    case FreshnessGrade::Stale:
        return "延迟偏大";
    case FreshnessGrade::Expired:
        return "已过期";
    case FreshnessGrade::Unknown:
        return "无数据";
    }
    return "无数据";
}

}  // namespace rm27
