#pragma once

#include <cstdint>

namespace rm27 {

// 画面来源（项目决策）：与官方协议无关，只描述本应用当前在看什么。
enum class VideoSource {
    None,           // 不加载任何画面
    LocalPlayback,  // 本地文件回放
    SimulationUdp,  // 本机 UDP 联调
};

struct VideoSourceDecision {
    bool allowed{false};
    bool sameSource{false};
    const char* reason{""};
};

// 切源的最短驻留时间：重启解码/重新绑定是有代价的，参考实现用面积阈值控制重启，
// 这里用更简单也更可测的形式——同源直接短路，异源要满足最短驻留。
inline constexpr std::int64_t kSourceSwitchCooldownMs = 1500;

// msSinceLastSwitch < 0 表示尚未切换过。
[[nodiscard]] VideoSourceDecision decideVideoSourceSwitch(
    VideoSource current, VideoSource target, std::int64_t msSinceLastSwitch,
    std::int64_t cooldownMs = kSourceSwitchCooldownMs) noexcept;

[[nodiscard]] const char* videoSourceText(VideoSource source) noexcept;

}  // namespace rm27
