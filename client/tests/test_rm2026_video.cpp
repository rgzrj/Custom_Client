#include "protocol/rm2026/video_reassembly.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace rm27::protocol::rm2026;

namespace {

std::vector<std::uint8_t> packet(std::uint16_t sequence, std::uint16_t index,
                                  std::uint32_t total, std::uint8_t payload = 42)
{
    const auto offset = std::size_t(index) * VideoReassembly::kPayloadBytes;
    const auto count = std::min(VideoReassembly::kPayloadBytes, std::size_t(total) - offset);
    std::vector<std::uint8_t> result{
        std::uint8_t(sequence >> 8), std::uint8_t(sequence),
        std::uint8_t(index >> 8), std::uint8_t(index),
        std::uint8_t(total >> 24), std::uint8_t(total >> 16),
        std::uint8_t(total >> 8), std::uint8_t(total)};
    result.resize(8 + count, payload);
    return result;
}

}  // namespace

int main()
{
    VideoReassembly receiver(0);
    // Byte-exact reassembly, last fragment first; duplicate cannot fill a hole.
    assert(!receiver.accept(packet(100, 2, 3000, 9), 0));
    assert(!receiver.accept(packet(100, 2, 3000, 9), 1));
    assert(receiver.duplicatePackets() == 1);
    assert(!receiver.accept(packet(100, 0, 3000, 7), 2));
    const auto frame = receiver.accept(packet(100, 1, 3000, 8), 3);
    assert(frame && frame->sequence == 100 && frame->hevc.size() == 3000);
    for (std::size_t i = 0; i < 3000; ++i)
        assert(frame->hevc[i] == (i < 1392 ? 7 : i < 2784 ? 8 : 9));
    assert(receiver.bufferedBytes() == 0);
    assert(!receiver.accept(packet(100, 0, 3000), 4));
    assert(!receiver.accept(packet(99, 0, 3000), 4));

    // New frame supersedes missing old fragments; incomplete frames expire.
    assert(!receiver.accept(packet(101, 0, 2000), 5));
    assert(!receiver.accept(packet(102, 0, 2000), 6));
    assert(receiver.droppedFrames() == 1);
    assert(!receiver.accept(packet(101, 1, 2000), 7));
    receiver.expire(156);
    assert(receiver.bufferedBytes() == 0 && receiver.droppedFrames() == 2);
    assert(!receiver.accept(packet(102, 1, 2000), 157));

    // A duplicate storm must not keep a frame alive beyond its original deadline.
    assert(!receiver.accept(packet(103, 0, 2000), 200));
    assert(!receiver.accept(packet(103, 0, 2000), 349));
    assert(!receiver.accept(packet(103, 1, 2000), 350));
    assert(receiver.bufferedBytes() == 0);

    receiver.reset();
    assert(receiver.accept(packet(65535, 0, 5), 400));
    assert(receiver.accept(packet(0, 0, 5), 401));
    assert(!receiver.accept(packet(65535, 0, 5), 402));
    assert(!receiver.accept(packet(32768, 0, 5), 402));
    receiver.reset(); // Sender restart must be explicit, not mistaken for wrap.
    assert(receiver.accept(packet(0, 0, 5), 403));

    // Trust boundary: short/oversized packet, zero/huge frame, bad index/length.
    for (std::size_t length = 0; length <= 8; ++length)
        assert(!receiver.accept(std::vector<std::uint8_t>(length), 500));
    assert(!receiver.accept(std::vector<std::uint8_t>(1401), 500));
    auto invalid = packet(1, 0, 5);
    invalid[7] = 0;
    assert(!receiver.accept(invalid, 500));
    invalid[4] = 0xff;
    assert(!receiver.accept(invalid, 500));
    invalid = packet(1, 0, 5);
    invalid[3] = 5;
    assert(!receiver.accept(invalid, 500));
    invalid = packet(1, 0, 5);
    invalid.pop_back();
    assert(!receiver.accept(invalid, 500));
    assert(receiver.bufferedBytes() == 4); // Short non-final fragments are allowed.
    receiver.expire(650);
    receiver.reset();
    assert(receiver.accept(packet(1, 0, 5), 501));

    // Inconsistent duplicate/total fails closed, not a corrupted complete frame.
    assert(!receiver.accept(packet(2, 0, 2000), 510));
    assert(!receiver.accept(packet(2, 0, 2000, 43), 511));
    assert(!receiver.accept(packet(2, 1, 2000), 512));
    assert(!receiver.accept(packet(3, 0, 2000), 520));
    assert(!receiver.accept(packet(3, 1, 2100), 521));
    assert(receiver.bufferedBytes() == 0);

    // Resource cap is enforced before allocation; exact cap remains supported.
    receiver.reset();
    assert(!receiver.accept(packet(4, 0, VideoReassembly::kMaximumFrameBytes + 1), 530));
    assert(receiver.bufferedBytes() == 0);
    assert(!receiver.accept(packet(4, 0, VideoReassembly::kMaximumFrameBytes), 531));
    assert(receiver.bufferedBytes() == VideoReassembly::kPayloadBytes);
    receiver.expire(681);
    assert(receiver.bufferedBytes() == 0);

    // Maximum payload is not a fixed slice size; concatenate contiguous indices.
    receiver.reset();
    auto shortSlice = packet(5, 0, 9, 7);
    shortSlice.resize(11);
    auto finalSlice = shortSlice;
    finalSlice[3] = 1;
    finalSlice.resize(14, 8);
    assert(!receiver.accept(finalSlice, 700));
    const auto variable = receiver.accept(shortSlice, 701);
    assert(variable && variable->hevc.size() == 9);
    assert(variable->hevc[0] == 7 && variable->hevc.back() == 8);
    receiver.reset();
    shortSlice[3] = 2; // Sum matches, but indices 0..1 are missing.
    shortSlice[7] = 3;
    assert(!receiver.accept(shortSlice, 702));
    assert(receiver.bufferedBytes() == 0);
    shortSlice[2] = 8; // Project fragment-count cap checked before insertion.
    shortSlice[3] = 0;
    shortSlice[6] = 0x10;
    shortSlice[7] = 0;
    assert(!receiver.accept(shortSlice, 703));
    assert(receiver.bufferedBytes() == 0);

    // Explicit 1-based reference profile: header index differs, data does not.
    VideoReassembly oneBased(1);
    auto first = packet(4, 0, 2000);
    first[3] = 1;
    auto last = packet(4, 1, 2000);
    last[3] = 2;
    assert(!oneBased.accept(first, 0));
    assert(oneBased.accept(last, 1));
    assert(!oneBased.accept(packet(5, 0, 5), 2));
    bool rejectedBase = false;
    try { VideoReassembly invalidBase(2); }
    catch (const std::invalid_argument&) { rejectedBase = true; }
    assert(rejectedBase);
    std::cout << "RM2026 offline video reference checks passed\n";
}
