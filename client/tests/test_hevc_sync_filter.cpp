#include "video/hevc_sync_filter.hpp"
#include "protocol/rm2026/video_reassembly.hpp"
#include <cassert>
#include <iostream>
#include <utility>

using Bytes = std::vector<std::uint8_t>;
using rm27::video::HevcSyncFilter;

static Bytes nal(unsigned type, std::uint8_t value = 42)
{
    return {0, 0, 0, 1, std::uint8_t(type << 1), 1, value};
}
static Bytes join(Bytes a, const Bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

int main()
{
    const auto parameters = join(join(nal(32), nal(33)), nal(34));
    const auto key = join(parameters, nal(19));
    HevcSyncFilter filter;
    assert(!filter.accept(nal(1)));
    assert(!filter.accept(nal(19))); // Missing parameter sets.
    assert(!filter.accept(parameters));
    assert(filter.accept(nal(19)) == join(parameters, nal(19)));
    assert(!filter.waitingForKeyframe());
    assert(filter.accept(nal(1)) == nal(1));
    filter.reset(); // Loss: cache survives; dependent pictures do not.
    assert(!filter.accept(nal(1)));
    assert(filter.accept(nal(21)) == join(parameters, nal(21)));
    assert(filter.accept(nal(6))); // Decodable leading RADL does not release RASL suppression.
    assert(!filter.accept(nal(8))); // CRA's leading RASL pictures cannot recover references.
    assert(!filter.accept(nal(9)));
    assert(filter.accept(nal(1)));
    assert(filter.accept(nal(8)));

    auto invalid = nal(19);
    invalid[4] |= 0x80;
    assert(!filter.accept(invalid));
    assert(filter.waitingForKeyframe());
    invalid = nal(19);
    invalid[5] = 0;
    assert(!filter.accept(invalid));
    invalid = nal(19);
    invalid[5] = 9; // Multi-layer streams are outside this reference profile.
    assert(!filter.accept(invalid));
    assert(!filter.accept({0, 0, 1, 38, 1}));
    assert(!filter.accept({1, 2, 3, 4}));
    assert(!filter.accept(join(nal(32, 99), {0, 0, 1}))); // Invalid AU cannot poison cache.
    assert(filter.accept(nal(19)) == join(parameters, nal(19)));

    assert(!filter.accept(nal(32, 99))); // New VPS invalidates dependent parameter sets.
    assert(!filter.accept(nal(19)));
    assert(!filter.accept(join(nal(33), nal(34))));
    assert(filter.accept(nal(19)));
    filter.reset(true); // Sender restart must not inherit parameters.
    assert(!filter.accept(nal(19)));
    assert(filter.accept(key));
    auto threeByte = nal(1);
    threeByte.erase(threeByte.begin());
    assert(filter.accept(threeByte) == threeByte);
    auto oversized = nal(32);
    oversized.resize(65537, 42);
    assert(!filter.accept(oversized));
    assert(!filter.accept(Bytes(rm27::protocol::rm2026::VideoReassembly::kMaximumFrameBytes + 1, 42)));
    Bytes storm;
    for (int i = 0; i < 1025; ++i) storm = join(std::move(storm), nal(35));
    assert(!filter.accept(storm));
    assert(filter.invalidUnits() >= 9 && filter.droppedUnits() > filter.invalidUnits());
    std::cout << "HEVC synchronization filter checks passed\n";
}
