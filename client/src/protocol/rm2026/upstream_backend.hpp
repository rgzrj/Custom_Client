#pragma once

#include "core/rm2026_state.hpp"
#include "protocol/rm2026/upstream_commands.hpp"
#include <QVariantMap>
#include <memory>

namespace rm27::protocol::rm2026 {
struct BackendView {
    historical::rm2026::Snapshot snapshot;
    QVariantMap status;
    bool subscribed = false;
};

// Owns the imported backend on a worker thread. GUI polls one latest view.
class UpstreamBackend final {
public:
    UpstreamBackend();
    ~UpstreamBackend();
    void start(const QString &host, quint16 port, unsigned robotId);
    void stop();
    BackendView view() const;
    // Bounded domain handoff, never a queued GUI event for every input frame.
    void updateInput(std::optional<historical::rm2026::KeyboardMouseCommand> input,
                     const historical::rm2026::CommandContext &context = {});
    // Single pending event. Missing UI actions can use this typed boundary later.
    CommandSendResult submitCommand(const historical::rm2026::Command &command,
                                   const historical::rm2026::CommandContext &context);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
