#include "core/video_source_policy.hpp"

namespace rm27 {

VideoSourceDecision decideVideoSourceSwitch(const VideoSource current, const VideoSource target,
                                            const std::int64_t msSinceLastSwitch,
                                            const std::int64_t cooldownMs) noexcept
{
    if (current == target) {
        // 同源不重启：这一条既省事，也避免「点两次把画面重启一遍」。
        return {false, true, "已是当前画面来源，无需重启解码"};
    }
    if (msSinceLastSwitch >= 0 && cooldownMs > 0 && msSinceLastSwitch < cooldownMs) {
        return {false, false, "距上次切换时间过短，低于最短驻留，暂不允许再次切源"};
    }
    return {true, false, "允许切换"};
}

const char* videoSourceText(const VideoSource source) noexcept
{
    switch (source) {
    case VideoSource::None:
        return "不加载画面";
    case VideoSource::LocalPlayback:
        return "本地回放";
    case VideoSource::SimulationUdp:
        return "本机 UDP 联调";
    case VideoSource::Rm2026Hevc:
        return "RM2026 HEVC";
    }
    return "未知来源";
}

}  // namespace rm27
