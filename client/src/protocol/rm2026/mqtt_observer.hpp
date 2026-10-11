#pragma once

#include <MQTTAsync.h>
#include <QJsonObject>
#include "core/rm2026_state.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace google::protobuf { class Message; }

namespace rm27::protocol::rm2026 {

// Loopback-only historical developer adapter, no publisher or GUI binding.
class MqttObserver final {
public:
    static constexpr const char* version = "rm2026-v2.0.0";
    explicit MqttObserver(const std::string& protocolVersion, unsigned port,
                          std::optional<unsigned> robotClientId = std::nullopt);
    ~MqttObserver();
    MqttObserver(const MqttObserver&) = delete;
    MqttObserver& operator=(const MqttObserver&) = delete;
    void start();
    QJsonObject status();
    historical::rm2026::Snapshot snapshot();

private:
    using Clock = std::chrono::steady_clock;
    struct Sample { std::unique_ptr<google::protobuf::Message> message; Clock::time_point received; };
    static void connected(void* context, char* cause);
    static void lost(void* context, char* cause);
    static void connectionFailed(void* context, MQTTAsync_failureData* data);
    static void subscribed(void* context, MQTTAsync_successData* data);
    static void subscriptionFailed(void* context, MQTTAsync_failureData* data);
    static int arrived(void* context, char* topic, int length, MQTTAsync_message* message);
    void subscribeNext();
    void reset(const char* link);
    historical::rm2026::Snapshot snapshotAt(Clock::time_point now) const;

    MQTTAsync client_{nullptr};
    std::string clientId_;
    std::mutex mutex_;
    std::string link_{"stopped"};
    std::map<std::string, Sample> samples_;
    std::deque<historical::rm2026::EventEntry> recentEvents_;
    std::uint64_t eventSequence_{0}, eventHistoryEvicted_{0};
    std::optional<std::uint32_t> robotId_;
    std::size_t nextTopic_{0};
    MQTTAsync_token pending_{0};
    Clock::time_point handshake_{};
    unsigned sessions_{0};
    unsigned accepted_{0}, rejected_{0}, retained_{0}, duplicates_{0};
};

}  // namespace rm27::protocol::rm2026
