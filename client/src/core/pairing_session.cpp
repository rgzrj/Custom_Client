#include "core/pairing_session.hpp"
#include <limits>

namespace rm27 {

bool PairingSession::begin(PairingRegion region, std::uint32_t robotId,
                           std::int64_t nowMs, int timeoutMs)
{
    // Supported operator roles, not a claim that every official ID is supported.
    const auto role = robotId >= 101 && robotId <= 107 ? robotId - 100 : robotId;
    if ((role != 1 && role != 2 && role != 3 && role != 4 && role != 6 && role != 7)
        || nowMs < 0 || timeoutMs < 1 || timeoutMs > 60000
        || nowMs > std::numeric_limits<std::int64_t>::max() - timeoutMs
        || (region != PairingRegion::Global && region != PairingRegion::Japan)) return false;
    ++token_;
    clearEvidence();
    region_ = region;
    targetId_ = robotId;
    startedAtMs_ = nowMs;
    deadlineMs_ = nowMs + timeoutMs;
    active_ = true;
    outcome_ = PairingPhase::Idle;
    return true;
}

void PairingSession::clearEvidence()
{
    operatorConfirmed_ = reviewed_ = false;
    lastVideoAtMs_.reset();
    reportedId_.reset();
}

bool PairingSession::videoFresh(std::int64_t nowMs) const
{
    // 700 ms is the existing local video liveness window, not hardware pairing timing.
    return active_ && lastVideoAtMs_ && nowMs >= *lastVideoAtMs_ && nowMs - *lastVideoAtMs_ < 700;
}

PairingPhase PairingSession::phase(std::int64_t nowMs) const
{
    if (!active_) return outcome_;
    if (!reviewed_ && nowMs >= deadlineMs_) return PairingPhase::TimedOut;
    if (reportedId_ && reportedId_ != targetId_) return PairingPhase::IdentityMismatch;
    if (!operatorConfirmed_) return PairingPhase::ExternalRequired;
    return videoFresh(nowMs) ? PairingPhase::VideoReview : PairingPhase::VideoRequired;
}

void PairingSession::tick(std::int64_t nowMs)
{
    if (phase(nowMs) == PairingPhase::TimedOut && active_) {
        active_ = false;
        outcome_ = PairingPhase::TimedOut;
        clearEvidence();
        ++token_; // Late acknowledgements cannot revive a timed-out attempt.
    }
}

bool PairingSession::accepts(Token token, std::int64_t nowMs)
{
    if (token != token_ || nowMs < startedAtMs_) return false;
    tick(nowMs);
    return active_ && token == token_;
}

bool PairingSession::confirmExternal(Token token, std::int64_t nowMs)
{
    if (!accepts(token, nowMs) || (reportedId_ && reportedId_ != targetId_)) return false;
    operatorConfirmed_ = true;
    reviewed_ = reviewed_ || videoFresh(nowMs);
    return true;
}

bool PairingSession::observeVideo(Token token, std::int64_t nowMs)
{
    if (!accepts(token, nowMs)) return false;
    lastVideoAtMs_ = nowMs;
    if (operatorConfirmed_) reviewed_ = true;
    return true;
}

bool PairingSession::reportIdentity(Token token, std::optional<std::uint32_t> id, std::int64_t nowMs)
{
    if (!accepts(token, nowMs)) return false;
    reportedId_ = id;
    if (id && id != targetId_) {
        operatorConfirmed_ = reviewed_ = false;
        lastVideoAtMs_.reset();
    }
    return true;
}

bool PairingSession::videoUnavailable(Token token, std::int64_t nowMs)
{
    if (!accepts(token, nowMs)) return false;
    lastVideoAtMs_.reset(); // A decoder fault is not evidence that hardware was unpaired.
    return true;
}

void PairingSession::cancel()
{
    ++token_;
    active_ = false;
    outcome_ = PairingPhase::Cancelled;
    clearEvidence();
}

bool PairingSession::deviceDisconnected(Token token)
{
    if (!active_ || token != token_) return false;
    cancel();
    outcome_ = PairingPhase::Disconnected;
    return true;
}

} // namespace rm27
