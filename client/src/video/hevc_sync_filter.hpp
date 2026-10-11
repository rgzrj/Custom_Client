#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rm27::video {

// Annex-B HEVC, single active VPS/SPS/PPS profile. This is a decoder input gate,
// not a bitstream validator.
class HevcSyncFilter {
public:
    void reset(bool clearParameters = false);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> accept(
        const std::vector<std::uint8_t>& accessUnit);
    [[nodiscard]] bool waitingForKeyframe() const noexcept { return waiting_; }
    [[nodiscard]] std::uint64_t droppedUnits() const noexcept { return dropped_; }
    [[nodiscard]] std::uint64_t invalidUnits() const noexcept { return invalid_; }
private:
    std::array<std::vector<std::uint8_t>, 3> parameters_;
    bool waiting_{true}, skipRasl_{false};
    std::uint64_t dropped_{0}, invalid_{0};
};

} // namespace rm27::video
