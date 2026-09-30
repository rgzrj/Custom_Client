#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "core/terminal_state.hpp"

namespace rm27 {

// —— 离线演练哨兵段（项目决策，非官方协议）——
//
// RFC 5737 把 203.0.113.0/24 保留给文档示例，公网与内网都不会路由到它，
// 因此它不可能与真实裁判系统或局域网设备重名。把裁判数据主机填进这一段，
// 终端就直接进入「离线演练」：不解析域名、不拨号、不依赖任何网卡已连接，
// 只在本机环回上跑项目模拟数据。
//
// 注意：哨兵段只是本地入口，它不绕过任何规则限制——evaluateCommand() 对
// Simulation/Replay 模式一律返回 GateBlock::OfflineMode，结构上无法下发指令。
inline constexpr const char* kOfflineSentinelCidr = "203.0.113.0/24";
inline constexpr const char* kOfflineSentinelHost = "203.0.113.7";

// 本机模拟链路的默认端点（与既有模拟器约定一致，全部为本机环回）。
inline constexpr std::uint16_t kDefaultRefereePort = 39001;
inline constexpr std::uint16_t kDefaultVideoProbePort = 39002;
inline constexpr std::uint16_t kDefaultSimulationVideoPort = 39003;
inline constexpr std::uint16_t kDefaultMqttPort = 39004;

// 端口与地址只表示「本应用打算监听/连接什么」，不表示 RM2027 官方参数。
struct ConnectionProfile {
    std::string refereeHost{"127.0.0.1"};
    std::uint16_t refereePort{kDefaultRefereePort};
    std::string videoBindAddress{"127.0.0.1"};
    std::uint16_t videoProbePort{kDefaultVideoProbePort};
    std::uint16_t simulationVideoPort{kDefaultSimulationVideoPort};
    bool useMqtt{false};
    std::string mqttHost{"127.0.0.1"};
    std::uint16_t mqttPort{kDefaultMqttPort};
};

enum class ProfileIssue {
    Ok,
    RefereeHostEmpty,
    RefereeHostInvalid,
    RefereePortInvalid,
    VideoBindAddressInvalid,
    VideoProbePortInvalid,
    SimulationVideoPortInvalid,
    MqttHostInvalid,
    MqttPortInvalid,
    PortCollision,
};

// 严格 IPv4 字面量解析：不接受域名、不接受端口、不接受多余前后缀。
// 不解析域名是刻意的——面板填错也不会因此产生 DNS 查询或对外流量。
[[nodiscard]] bool parseIpv4(const std::string& text, std::array<unsigned, 4>& octets) noexcept;

[[nodiscard]] bool isLoopbackHost(const std::string& host) noexcept;

// true 表示地址落在 203.0.113.0/24 内（即离线演练哨兵段）。
[[nodiscard]] bool isOfflineSentinelHost(const std::string& host) noexcept;

[[nodiscard]] bool profileUsesOfflineSentinel(const ConnectionProfile& profile) noexcept;

// 裁判数据、MQTT 与图传绑定地址都在本机环回上：纯本机模拟，不涉及实物链路。
[[nodiscard]] bool profileIsLocalSimulation(const ConnectionProfile& profile) noexcept;

// 哨兵段或纯环回 → 模拟；其余 → 重装操作（正式模式仍需安全门放行才可能下发）。
[[nodiscard]] TerminalMode modeForProfile(const ConnectionProfile& profile) noexcept;

[[nodiscard]] ProfileIssue validateProfile(const ConnectionProfile& profile) noexcept;

// 中文说明，用于界面与告警文本。
[[nodiscard]] const char* profileIssueText(ProfileIssue issue) noexcept;

}  // namespace rm27
