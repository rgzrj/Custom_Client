#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <map>
#include <vector>

namespace rm27::protocol::rm2026 {

// Section 2, PDF page 45: big-endian 2/2/4-byte header, <=1400-byte UDP.
// Separately versioned reference. First fragment index must be selected explicitly;
// 1392 is a maximum payload size, not a required size for every non-final fragment.
struct VideoFrame {
    std::uint16_t sequence;
    std::vector<std::uint8_t> hevc;
};

class VideoReassembly {
public:
    static constexpr std::size_t kPayloadBytes = 1392;
    // Project resource/latency decisions, NOT official 2026 limits.
    static constexpr std::size_t kMaximumFrameBytes = 2 * 1024 * 1024;
    static constexpr std::size_t kMaximumFragments = 2048;
    static constexpr std::int64_t kTimeoutMs = 150;

    explicit VideoReassembly(std::uint16_t firstFragmentIndex);
    [[nodiscard]] std::optional<VideoFrame> accept(
        const std::vector<std::uint8_t>& datagram, std::int64_t nowMs);
    void expire(std::int64_t nowMs);
    void reset();
    [[nodiscard]] std::size_t bufferedBytes() const noexcept { return receivedBytes_; }
    [[nodiscard]] std::uint64_t rejectedPackets() const noexcept { return rejected_; }
    [[nodiscard]] std::uint64_t duplicatePackets() const noexcept { return duplicates_; }
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept { return dropped_; }

private:
    void discard();
    std::uint16_t firstIndex_;
    std::uint16_t sequence_{0};
    bool haveSequence_{false};
    std::int64_t startedAtMs_{0};
    std::map<std::uint16_t, std::vector<std::uint8_t>> fragments_;
    std::size_t totalBytes_{0}, receivedBytes_{0};
    std::uint64_t rejected_{0};
    std::uint64_t duplicates_{0};
    std::uint64_t dropped_{0};
};

}  // namespace rm27::protocol::rm2026
