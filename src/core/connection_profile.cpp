#include "core/connection_profile.hpp"

namespace rm27 {
namespace {

// 哨兵段前缀：203.0.113.0/24 的前三个八位组固定。
constexpr unsigned kSentinelOctet0 = 203;
constexpr unsigned kSentinelOctet1 = 0;
constexpr unsigned kSentinelOctet2 = 113;

bool parseUnsignedOctet(const std::string& text, const std::size_t begin, const std::size_t end,
                        unsigned& value)
{
    if (begin >= end || end - begin > 3) {
        return false;
    }
    unsigned parsed = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const char character = text[index];
        if (character < '0' || character > '9') {
            return false;
        }
        // 拒绝 010 这类带前导零的写法，避免与八进制/十进制解释产生歧义。
        if (parsed == 0 && character == '0' && end - begin > 1) {
            return false;
        }
        parsed = parsed * 10 + static_cast<unsigned>(character - '0');
    }
    if (parsed > 255) {
        return false;
    }
    value = parsed;
    return true;
}

bool validPort(const std::uint16_t port) noexcept
{
    // std::uint16_t 天然不超过 65535；0 表示「自动分配」，本应用不接受。
    return port != 0;
}

}  // namespace

bool parseIpv4(const std::string& text, std::array<unsigned, 4>& octets) noexcept
{
    octets = {0, 0, 0, 0};
    if (text.empty() || text.size() > 15) {
        return false;
    }

    std::size_t segmentBegin = 0;
    for (std::size_t segment = 0; segment < 4; ++segment) {
        const bool lastSegment = segment == 3;
        const std::size_t dot = text.find('.', segmentBegin);
        if (lastSegment ? dot != std::string::npos : dot == std::string::npos) {
            return false;
        }

        const std::size_t segmentEnd = lastSegment ? text.size() : dot;
        if (!parseUnsignedOctet(text, segmentBegin, segmentEnd, octets[segment])) {
            return false;
        }
        segmentBegin = segmentEnd + 1;
    }
    return true;
}

bool isLoopbackHost(const std::string& host) noexcept
{
    std::array<unsigned, 4> octets{};
    if (!parseIpv4(host, octets)) {
        return false;
    }
    return octets[0] == 127;
}

bool isOfflineSentinelHost(const std::string& host) noexcept
{
    std::array<unsigned, 4> octets{};
    if (!parseIpv4(host, octets)) {
        return false;
    }
    return octets[0] == kSentinelOctet0 && octets[1] == kSentinelOctet1
        && octets[2] == kSentinelOctet2;
}

bool profileUsesOfflineSentinel(const ConnectionProfile& profile) noexcept
{
    return isOfflineSentinelHost(profile.refereeHost);
}

bool profileIsLocalSimulation(const ConnectionProfile& profile) noexcept
{
    if (!isLoopbackHost(profile.refereeHost) || !isLoopbackHost(profile.videoBindAddress)) {
        return false;
    }
    return !profile.useMqtt || isLoopbackHost(profile.mqttHost);
}

TerminalMode modeForProfile(const ConnectionProfile& profile) noexcept
{
    if (profileUsesOfflineSentinel(profile) || profileIsLocalSimulation(profile)) {
        return TerminalMode::Simulation;
    }
    return TerminalMode::HeavyOperator;
}

ProfileIssue validateProfile(const ConnectionProfile& profile) noexcept
{
    std::array<unsigned, 4> octets{};

    if (profile.refereeHost.empty()) {
        return ProfileIssue::RefereeHostEmpty;
    }
    if (!parseIpv4(profile.refereeHost, octets)) {
        return ProfileIssue::RefereeHostInvalid;
    }
    if (!validPort(profile.refereePort)) {
        return ProfileIssue::RefereePortInvalid;
    }
    if (!parseIpv4(profile.videoBindAddress, octets)) {
        return ProfileIssue::VideoBindAddressInvalid;
    }
    if (!validPort(profile.videoProbePort)) {
        return ProfileIssue::VideoProbePortInvalid;
    }
    if (!validPort(profile.simulationVideoPort)) {
        return ProfileIssue::SimulationVideoPortInvalid;
    }
    if (profile.useMqtt) {
        if (!parseIpv4(profile.mqttHost, octets)) {
            return ProfileIssue::MqttHostInvalid;
        }
        if (!validPort(profile.mqttPort)) {
            return ProfileIssue::MqttPortInvalid;
        }
    }

    const std::uint16_t ports[4] = {
        profile.refereePort,
        profile.videoProbePort,
        profile.simulationVideoPort,
        profile.useMqtt ? profile.mqttPort : static_cast<std::uint16_t>(0),
    };
    for (std::size_t first = 0; first < 4; ++first) {
        if (ports[first] == 0) {
            continue;
        }
        for (std::size_t second = first + 1; second < 4; ++second) {
            if (ports[second] != 0 && ports[first] == ports[second]) {
                return ProfileIssue::PortCollision;
            }
        }
    }
    return ProfileIssue::Ok;
}

const char* profileIssueText(const ProfileIssue issue) noexcept
{
    switch (issue) {
    case ProfileIssue::Ok:
        return "链路参数校验通过";
    case ProfileIssue::RefereeHostEmpty:
        return "裁判数据主机不能为空";
    case ProfileIssue::RefereeHostInvalid:
        return "裁判数据主机必须是 IPv4 字面量（不解析域名）";
    case ProfileIssue::RefereePortInvalid:
        return "裁判数据端口必须在 1-65535 之间";
    case ProfileIssue::VideoBindAddressInvalid:
        return "图传绑定地址必须是 IPv4 字面量（0.0.0.0 表示监听本机所有网卡）";
    case ProfileIssue::VideoProbePortInvalid:
        return "图传探针端口必须在 1-65535 之间";
    case ProfileIssue::SimulationVideoPortInvalid:
        return "本机联调解码端口必须在 1-65535 之间";
    case ProfileIssue::MqttHostInvalid:
        return "MQTT 主机必须是 IPv4 字面量（不解析域名）";
    case ProfileIssue::MqttPortInvalid:
        return "MQTT 端口必须在 1-65535 之间";
    case ProfileIssue::PortCollision:
        return "端口重复：裁判数据、图传探针、本机联调解码、MQTT 必须各不相同";
    }
    return "链路参数状态未知";
}

}  // namespace rm27
