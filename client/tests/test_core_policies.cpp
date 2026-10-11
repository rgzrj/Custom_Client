#include "core/freshness_grade.hpp"
#include "core/input_mapping.hpp"
#include "core/video_source_policy.hpp"

#include <cassert>
#include <iostream>

using namespace rm27;

namespace {

void freshnessChecks()
{
    assert(gradeForAge(-1) == FreshnessGrade::Unknown);
    assert(gradeForAge(0) == FreshnessGrade::Fresh);
    assert(gradeForAge(kFreshUntilMs - 1) == FreshnessGrade::Fresh);
    assert(gradeForAge(kFreshUntilMs) == FreshnessGrade::Degraded);
    assert(gradeForAge(kDegradedUntilMs - 1) == FreshnessGrade::Degraded);
    assert(gradeForAge(kDegradedUntilMs) == FreshnessGrade::Stale);
    assert(gradeForAge(kStaleUntilMs - 1) == FreshnessGrade::Stale);
    assert(gradeForAge(kStaleUntilMs) == FreshnessGrade::Expired);
    assert(gradeForAge(60000) == FreshnessGrade::Expired);

    assert(freshnessScale(FreshnessGrade::Fresh) == 1.0);
    assert(freshnessScale(FreshnessGrade::Degraded) == 0.7);
    assert(freshnessScale(FreshnessGrade::Stale) == 0.4);
    assert(freshnessScale(FreshnessGrade::Expired) == 0.0);
    assert(freshnessScale(FreshnessGrade::Unknown) == 0.0);

    // 每一级都必须有可直接显示的中文说明。
    for (const FreshnessGrade grade : {FreshnessGrade::Unknown, FreshnessGrade::Fresh,
                                       FreshnessGrade::Degraded, FreshnessGrade::Stale,
                                       FreshnessGrade::Expired}) {
        assert(freshnessGradeText(grade)[0] != '\0');
    }
}

void inputMappingChecks()
{
    const auto& actions = defaultInputActions();
    assert(actions.size() >= 5);

    // id 必须唯一且可查；表里不允许空字段。
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const InputAction& action = actions[index];
        assert(!action.id.empty() && !action.label.empty() && !action.group.empty());
        assert(!action.defaultKeys.empty());
        assert(findInputAction(action.id) == &action);
        for (std::size_t other = index + 1; other < actions.size(); ++other) {
            assert(actions[other].id != action.id);
        }
    }

    assert(findInputAction("panel.start") != nullptr);
    assert(findInputAction("no.such.action") == nullptr);

    // 「标题即快捷键表」：提示由映射生成。
    const InputAction* offline = findInputAction("link.offlineDrill");
    assert(offline != nullptr);
    assert(actionHint(*offline) == "离线演练进入 [Ctrl+Shift+O]");

}

void videoSourceChecks()
{
    // 同源短路：不重启解码。
    auto decision = decideVideoSourceSwitch(VideoSource::LocalPlayback, VideoSource::LocalPlayback, -1);
    assert(!decision.allowed && decision.sameSource);

    // 首次切源（从未切换过）允许。
    decision = decideVideoSourceSwitch(VideoSource::None, VideoSource::LocalPlayback, -1);
    assert(decision.allowed && !decision.sameSource);

    // 冷却期内不允许，冷却结束后允许。
    decision = decideVideoSourceSwitch(VideoSource::LocalPlayback, VideoSource::SimulationUdp, 200);
    assert(!decision.allowed && !decision.sameSource);
    decision = decideVideoSourceSwitch(VideoSource::LocalPlayback, VideoSource::SimulationUdp,
                                       kSourceSwitchCooldownMs);
    assert(decision.allowed);
    decision = decideVideoSourceSwitch(VideoSource::LocalPlayback, VideoSource::SimulationUdp,
                                       kSourceSwitchCooldownMs + 5000);
    assert(decision.allowed);

    for (const VideoSource source : {VideoSource::None, VideoSource::LocalPlayback,
                                     VideoSource::SimulationUdp}) {
        assert(videoSourceText(source)[0] != '\0');
    }
}

}  // namespace

int main()
{
    freshnessChecks();
    inputMappingChecks();
    videoSourceChecks();
    std::cout << "rm27 policy checks passed\n";
    return 0;
}
