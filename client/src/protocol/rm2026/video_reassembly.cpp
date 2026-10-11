#include "protocol/rm2026/video_reassembly.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rm27::protocol::rm2026 {
namespace {

std::uint16_t read16(const std::uint8_t* value)
{
    return (std::uint16_t(value[0]) << 8) | value[1];
}

std::uint32_t read32(const std::uint8_t* value)
{
    return (std::uint32_t(value[0]) << 24) | (std::uint32_t(value[1]) << 16)
        | (std::uint32_t(value[2]) << 8) | value[3];
}

}  // namespace

VideoReassembly::VideoReassembly(const std::uint16_t firstFragmentIndex)
    : firstIndex_(firstFragmentIndex)
{
    if (firstIndex_ > 1) throw std::invalid_argument("fragment base must be explicitly 0 or 1");
}

void VideoReassembly::discard()
{
    if (!fragments_.empty()) ++dropped_;
    fragments_.clear();
    totalBytes_ = receivedBytes_ = 0;
}

void VideoReassembly::reset()
{
    discard();
    haveSequence_ = false;
}

void VideoReassembly::expire(const std::int64_t nowMs)
{
    if (!fragments_.empty() && nowMs - startedAtMs_ >= kTimeoutMs) discard();
}

std::optional<VideoFrame> VideoReassembly::accept(
    const std::vector<std::uint8_t>& datagram, const std::int64_t nowMs)
{
    expire(nowMs);
    if (datagram.size() <= 8 || datagram.size() > 1400) {
        ++rejected_;
        return std::nullopt;
    }
    const auto sequence = read16(datagram.data());
    const auto wireIndex = read16(datagram.data() + 2);
    const auto total = read32(datagram.data() + 4);
    if (total == 0 || total > kMaximumFrameBytes || wireIndex < firstIndex_) {
        ++rejected_;
        return std::nullopt;
    }
    const std::size_t index = wireIndex - firstIndex_;
    const std::size_t payloadBytes = datagram.size() - 8;
    if (index >= kMaximumFragments || index >= total || payloadBytes > total) {
        ++rejected_;
        return std::nullopt;
    }

    if (haveSequence_ && sequence != sequence_) {
        const auto delta = std::uint16_t(sequence - sequence_);
        if (delta >= 0x8000) { // Old/ambiguous modulo-16-bit sequence, including wrap.
            ++rejected_;
            return std::nullopt;
        }
    } else if (haveSequence_ && fragments_.empty()) {
        ++duplicates_; // Completed/expired frame is never revived in this session.
        return std::nullopt;
    }

    if (!haveSequence_ || sequence != sequence_) {
        discard(); // Keep only the newest in-progress frame, never build a backlog.
        sequence_ = sequence;
        haveSequence_ = true;
        startedAtMs_ = nowMs;
        totalBytes_ = total;
    }
    if (totalBytes_ != total) {
        ++rejected_;
        discard(); // Conflicting headers cannot produce a trusted frame.
        return std::nullopt;
    }
    const auto existing = fragments_.find(static_cast<std::uint16_t>(index));
    if (existing != fragments_.end()) {
        if (existing->second.size() != payloadBytes || !std::equal(
                datagram.begin() + 8, datagram.end(), existing->second.begin())) {
            ++rejected_;
            discard();
        } else {
            ++duplicates_;
        }
        return std::nullopt; // Duplicates do not extend the frame deadline.
    }
    if (payloadBytes > totalBytes_ - receivedBytes_) {
        ++rejected_;
        discard();
        return std::nullopt;
    }
    fragments_.emplace(static_cast<std::uint16_t>(index),
        std::vector<std::uint8_t>(datagram.begin() + 8, datagram.end()));
    receivedBytes_ += payloadBytes;
    if (receivedBytes_ != totalBytes_) return std::nullopt;
    if (fragments_.rbegin()->first + 1u != fragments_.size()) {
        ++rejected_;
        discard(); // Byte count alone cannot prove that all fragment indices exist.
        return std::nullopt;
    }
    VideoFrame frame{sequence_, {}};
    frame.hevc.reserve(totalBytes_);
    for (const auto& fragment : fragments_)
        frame.hevc.insert(frame.hevc.end(), fragment.second.begin(), fragment.second.end());
    fragments_.clear();
    totalBytes_ = receivedBytes_ = 0;
    return frame; // Assembly only, NOT proof of a decoded/live video frame.
}

}  // namespace rm27::protocol::rm2026
