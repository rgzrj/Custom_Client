#pragma once

#include <cstdint>
#include <optional>

namespace rm27 {

enum class PairingRegion { Global, Japan };
enum class PairingPhase { Idle, ExternalRequired, VideoRequired, VideoReview,
    IdentityMismatch, TimedOut, Cancelled, Disconnected };

// Workflow only. Confirmation is an operator report, never device authentication.
// No USB/serial commands, MQTT login or control permission are produced here.
class PairingSession {
public:
    using Token = std::uint64_t;
    bool begin(PairingRegion region, std::uint32_t robotId,
               std::int64_t nowMs, int timeoutMs = 60000);
    bool confirmExternal(Token token, std::int64_t nowMs);
    bool observeVideo(Token token, std::int64_t nowMs);
    bool reportIdentity(Token token, std::optional<std::uint32_t> id, std::int64_t nowMs);
    bool videoUnavailable(Token token, std::int64_t nowMs);
    bool deviceDisconnected(Token token);
    void cancel();
    void tick(std::int64_t nowMs);
    [[nodiscard]] PairingPhase phase(std::int64_t nowMs) const;
    [[nodiscard]] bool videoFresh(std::int64_t nowMs) const;
    [[nodiscard]] Token token() const { return token_; }
    [[nodiscard]] PairingRegion region() const { return region_; }
    [[nodiscard]] std::optional<std::uint32_t> targetId() const { return targetId_; }
    [[nodiscard]] std::optional<std::uint32_t> reportedId() const { return reportedId_; }
    [[nodiscard]] bool operatorConfirmed() const { return operatorConfirmed_; }

private:
    bool accepts(Token token, std::int64_t nowMs);
    void clearEvidence();
    Token token_{0};
    PairingRegion region_{PairingRegion::Global};
    PairingPhase outcome_{PairingPhase::Idle};
    bool active_{false}, operatorConfirmed_{false}, reviewed_{false};
    std::int64_t startedAtMs_{0}, deadlineMs_{0};
    std::optional<std::int64_t> lastVideoAtMs_;
    std::optional<std::uint32_t> targetId_, reportedId_;
};

} // namespace rm27
