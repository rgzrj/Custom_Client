#pragma once
#include "core/rm2026_commands.hpp"
#include <QByteArray>
#include <QString>

namespace rm27::protocol::rm2026 {
struct EncodedCommand {
    QString topic;
    QByteArray payload;
    std::chrono::nanoseconds interval{};
    QString error;
};
EncodedCommand encodeCommand(const historical::rm2026::Command &command,
    historical::rm2026::MapOperator mapOperator = historical::rm2026::MapOperator::Unknown);
// Additional authority gates supplement the existing common terminal gate.
QString commandBlock(const historical::rm2026::Command &command,
                     const historical::rm2026::CommandContext &context);
enum class CommandSendStatus { Blocked, Invalid, Disconnected, RateLimited,
    TransportRejected, DeliveryUnknown, BrokerAcknowledged, Queued, InFlightLimit };
struct CommandSendResult {
    CommandSendStatus status{CommandSendStatus::Blocked};
    QString reason;
    int token = 0;
    // BrokerAcknowledged is a MQTT PUBACK, never an event-engine/robot receipt.
};
}
