#include "core/connection_profile.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace rm27;

namespace {

void expectParse(const std::string& text, const bool expected)
{
    std::array<unsigned, 4> octets{};
    const bool parsed = parseIpv4(text, octets);
    assert(parsed == expected);
    if (expected) {
        assert(octets[0] <= 255 && octets[1] <= 255 && octets[2] <= 255 && octets[3] <= 255);
    }
}

}  // namespace

int main()
{
    // —— IPv4 字面量解析：只接受严格写法，域名/带端口/前导零一律拒绝 ——
    expectParse("127.0.0.1", true);
    expectParse("0.0.0.0", true);
    expectParse("203.0.113.7", true);
    expectParse("255.255.255.255", true);
    expectParse("", false);
    expectParse("127.0.0", false);
    expectParse("127.0.0.1.5", false);
    expectParse("256.0.0.1", false);
    expectParse("01.2.3.4", false);
    expectParse("referee.local", false);
    expectParse("203.0.113.7:3333", false);
    expectParse("203.0.113.7 ", false);
    expectParse("-1.0.0.1", false);

    // —— 离线哨兵段：203.0.113.0/24 内任意地址都算，段外一律不算 ——
    assert(isOfflineSentinelHost("203.0.113.0"));
    assert(isOfflineSentinelHost("203.0.113.7"));
    assert(isOfflineSentinelHost(kOfflineSentinelHost));
    assert(isOfflineSentinelHost("203.0.113.255"));
    assert(!isOfflineSentinelHost("203.0.114.1"));
    assert(!isOfflineSentinelHost("203.0.112.255"));
    assert(!isOfflineSentinelHost("127.0.0.1"));
    assert(!isOfflineSentinelHost("192.168.12.1"));
    assert(!isOfflineSentinelHost("referee.local"));

    assert(isLoopbackHost("127.0.0.1"));
    assert(isLoopbackHost("127.8.9.10"));
    assert(!isLoopbackHost("192.168.12.2"));

    // —— 默认档：全部环回 → 本机模拟；哨兵段 → 离线演练（也是模拟）；真实地址 → 重装操作 ——
    ConnectionProfile profile;
    assert(profileUsesOfflineSentinel(profile) == false);
    assert(profileIsLocalSimulation(profile));
    assert(modeForProfile(profile) == TerminalMode::Simulation);
    assert(validateProfile(profile) == ProfileIssue::Ok);

    ConnectionProfile sentinel = profile;
    sentinel.refereeHost = kOfflineSentinelHost;
    assert(profileUsesOfflineSentinel(sentinel));
    assert(!profileIsLocalSimulation(sentinel));
    assert(modeForProfile(sentinel) == TerminalMode::Simulation);
    assert(validateProfile(sentinel) == ProfileIssue::Ok);

    ConnectionProfile formal = profile;
    formal.refereeHost = "192.168.12.1";
    formal.videoBindAddress = "0.0.0.0";
    assert(!profileUsesOfflineSentinel(formal));
    assert(!profileIsLocalSimulation(formal));
    assert(modeForProfile(formal) == TerminalMode::HeavyOperator);
    assert(validateProfile(formal) == ProfileIssue::Ok);

    // —— 校验：空主机、域名、0 端口、端口重复、MQTT 主机非法 ——
    ConnectionProfile broken = profile;
    broken.refereeHost.clear();
    assert(validateProfile(broken) == ProfileIssue::RefereeHostEmpty);

    broken = profile;
    broken.refereeHost = "referee.local";
    assert(validateProfile(broken) == ProfileIssue::RefereeHostInvalid);

    broken = profile;
    broken.refereePort = 0;
    assert(validateProfile(broken) == ProfileIssue::RefereePortInvalid);

    broken = profile;
    broken.videoProbePort = broken.refereePort;
    assert(validateProfile(broken) == ProfileIssue::PortCollision);

    broken = profile;
    broken.useMqtt = true;
    broken.mqttHost = "broker.local";
    assert(validateProfile(broken) == ProfileIssue::MqttHostInvalid);

    broken = profile;
    broken.useMqtt = true;
    broken.mqttPort = broken.simulationVideoPort;
    assert(validateProfile(broken) == ProfileIssue::PortCollision);

    broken = profile;
    broken.videoBindAddress = "0.0.0.0.0";
    assert(validateProfile(broken) == ProfileIssue::VideoBindAddressInvalid);

    // —— 每一条问题都必须有可直接显示的中文说明 ——
    const ProfileIssue issues[] = {
        ProfileIssue::Ok, ProfileIssue::RefereeHostEmpty, ProfileIssue::RefereeHostInvalid,
        ProfileIssue::RefereePortInvalid, ProfileIssue::VideoBindAddressInvalid,
        ProfileIssue::VideoProbePortInvalid, ProfileIssue::SimulationVideoPortInvalid,
        ProfileIssue::MqttHostInvalid, ProfileIssue::MqttPortInvalid, ProfileIssue::PortCollision,
    };
    for (const ProfileIssue issue : issues) {
        assert(profileIssueText(issue) != nullptr);
        assert(profileIssueText(issue)[0] != '\0');
    }

    std::cout << "rm27 connection profile checks passed\n";
    return 0;
}
