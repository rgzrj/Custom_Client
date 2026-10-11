#include "video/hevc_sync_filter.hpp"
#include "protocol/rm2026/video_reassembly.hpp"
#include <utility>

namespace rm27::video {
namespace {
std::size_t prefixAt(const std::vector<std::uint8_t>& bytes, std::size_t i)
{
    if (i + 3 > bytes.size() || bytes[i] || bytes[i + 1]) return 0;
    if (bytes[i + 2] == 1) return 3;
    return i + 4 <= bytes.size() && bytes[i + 2] == 0 && bytes[i + 3] == 1 ? 4 : 0;
}
}

void HevcSyncFilter::reset(bool clearParameters)
{
    waiting_ = true;
    skipRasl_ = false;
    if (clearParameters) for (auto& parameter : parameters_) parameter.clear();
}

std::optional<std::vector<std::uint8_t>> HevcSyncFilter::accept(
    const std::vector<std::uint8_t>& bytes)
{
    struct Nal { std::size_t begin, end; unsigned type; };
    std::vector<Nal> nals;
    const auto invalid = [&]() -> std::optional<std::vector<std::uint8_t>> {
        ++invalid_; ++dropped_; reset(); return std::nullopt;
    };
    if (bytes.empty() || bytes.size() > protocol::rm2026::VideoReassembly::kMaximumFrameBytes)
        return invalid();
    bool irap = false, vcl = false, rasl = false, radl = false, cra = false;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto prefix = prefixAt(bytes, i);
        if (!prefix || i + prefix + 2 >= bytes.size() || nals.size() >= 1024)
            return invalid();
        const auto first = bytes[i + prefix], second = bytes[i + prefix + 1];
        // H.265 NAL header: forbidden bit zero, layer zero, temporal_id_plus1 nonzero.
        if ((first & 0x81) || (second & 0xf8) || !(second & 7)) return invalid();
        const unsigned type = (first >> 1) & 63;
        auto end = i + prefix + 2;
        while (end < bytes.size() && !prefixAt(bytes, end)) ++end;
        if (end == i + prefix + 2 || (type >= 32 && type <= 34 && end - i > 65536))
            return invalid();
        nals.push_back({i, end, type});
        vcl |= type <= 31;
        irap |= type >= 16 && type <= 21;
        rasl |= type == 8 || type == 9;
        radl |= type == 6 || type == 7;
        cra |= type == 21;
        i = end;
    }
    // Validate the whole access unit before letting it alter cached parameters.
    for (const auto& nal : nals) {
        if (nal.type < 32 || nal.type > 34) continue;
        std::vector<std::uint8_t> value(bytes.begin() + nal.begin, bytes.begin() + nal.end);
        auto& cached = parameters_[nal.type - 32];
        if (!cached.empty() && cached != value) {
            reset();
            for (unsigned i = nal.type - 31; i < parameters_.size(); ++i) parameters_[i].clear();
        }
        cached = std::move(value);
    }
    if (!vcl || (skipRasl_ && rasl) || (waiting_ && (!irap
        || parameters_[0].empty() || parameters_[1].empty() || parameters_[2].empty()))) {
        ++dropped_;
        return std::nullopt;
    }
    std::vector<std::uint8_t> output;
    if (waiting_) {
        // Re-inject complete parameter sets before the random-access picture.
        for (const auto& parameter : parameters_) output.insert(output.end(), parameter.begin(), parameter.end());
        waiting_ = false;
        skipRasl_ = cra;
    } else if (!rasl && !radl) {
        skipRasl_ = false;
    }
    output.insert(output.end(), bytes.begin(), bytes.end());
    return output;
}

} // namespace rm27::video
