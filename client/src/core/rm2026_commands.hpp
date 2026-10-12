#pragma once

#include "core/terminal_state.hpp"
#include "core/rm2026_state.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace rm27::historical::rm2026 {
// Official 2026 V2.0.0 §2.2. Encoding is not gameplay eligibility or authority.
struct KeyboardMouseCommand {
    std::int32_t x{}, y{}, wheel{};
    bool left{}, right{}, middle{};
    std::uint32_t keys{};
};
struct CustomControlCommand { std::vector<std::uint8_t> data; };
struct MapClickCommand {
    std::uint32_t sendAll{};
    std::array<std::uint8_t, 7> robotIds{};
    std::uint32_t markingType{1}, enemyId{}, ascii{}, markingMode{1};
    float x{}, y{};
};
struct AssemblyCommand { std::uint32_t operation{}, difficulty{}; };
struct PerformanceCommand {
    std::optional<std::uint32_t> shooter, chassis, sentryControl;
};
struct CommonCommand { std::uint32_t type{}, parameter{}; };
struct DeployCommand { std::uint32_t mode{}; };
struct RuneCommand { std::uint32_t activate{1}; };
struct DartCommand { std::uint32_t target{}; bool open{}, confirm{}; };
struct SentryCommand { std::uint32_t id{}; };
struct AirSupportCommand { std::uint32_t id{}; };
using Command = std::variant<KeyboardMouseCommand, CustomControlCommand, MapClickCommand,
    AssemblyCommand, PerformanceCommand, CommonCommand, DeployCommand, RuneCommand,
    DartCommand, SentryCommand, AirSupportCommand>;

enum class MapOperator { Unknown, Gimbal, SemiAutomatic };
struct CommandContext {
    TerminalState terminal;
    std::optional<std::uint32_t> requestedId, reportedId;
    bool identityFresh{false};
    // Not exposed to QML/CLI. Remains false in the current ordinary controller.
    // Must cover role, gameplay eligibility and penalty policy before live use.
    bool businessPolicyReady{false};
    // Requires verified exclusive source evidence; a local selection/identity/video is insufficient.
    bool keyboardSourceSelected{false};
    bool mapCalibrated{false};
    MapOperator mapOperator{MapOperator::Unknown};
};

bool commandPhaseAllowed(const Command &command, Phase phase);
// Necessary observable business constraints; empty does not grant authority.
// The common gate and businessPolicyReady remain independent requirements.
const char *businessCommandBlock(const Command &command, const Snapshot &snapshot,
    const CommandContext &context, std::chrono::steady_clock::time_point now);

// One interval per official topic, monotonic and with no neutral-frame exception.
class CommandRateLimiter {
public:
    using Clock = std::chrono::steady_clock;
    bool acquire(std::size_t topic, std::chrono::nanoseconds interval, Clock::time_point now) {
        if (topic >= last_.size()) return false;
        auto &last = last_[topic];
        if (last && (now < *last || now - *last < interval)) return false;
        last = now;
        return true;
    }
private:
    std::array<std::optional<Clock::time_point>, std::variant_size_v<Command>> last_{};
};
}
