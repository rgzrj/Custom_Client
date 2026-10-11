#include "protocol/rm2026/mqtt_observer.hpp"
#include "protocol/rm2026/state_mapping.hpp"

#include "client.pb.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <google/protobuf/util/json_util.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace rm27::protocol::rm2026 {
namespace {
constexpr std::array<const char*, 14> topics{
    "Buff", "CustomByteBlock", "DeployModeStatusSync", "Event", "GameStatus", "GlobalLogisticsStatus", "GlobalUnitStatus",
    "PenaltyInfo", "RobotDynamicStatus", "RobotModuleStatus", "RobotPerformanceSelectionSync",
    "RobotPosition", "RobotRespawnStatus", "RobotStaticStatus"};

std::unique_ptr<google::protobuf::Message> decode(const std::string& topic, const void* payload, const int length)
{
    if (length < 0 || length > 65536 || std::none_of(topics.begin(), topics.end(),
        [&](const char* candidate) { return topic == candidate; })) return {};
    const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()
        ->FindMessageTypeByName("rm2026.v2." + topic);
    const auto* prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
    if (!prototype) return {};
    std::unique_ptr<google::protobuf::Message> message(prototype->New());
    if (!message->ParseFromArray(payload, length)) return {};
    const auto* reflection = message->GetReflection();
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(*message, &fields);
    if (fields.empty()) return {}; // Empty and unknown-only are not live evidence.
    for (const auto* field : fields) {
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT &&
            !std::isfinite(reflection->GetFloat(*message, field))) return {};
        if (topic == "GlobalUnitStatus" && field->name() == "robot_health" &&
            reflection->FieldSize(*message, field) != 10) return {};
        if (topic == "CustomByteBlock" && field->name() == "data" &&
            reflection->GetString(*message, field).size() > 300) return {};
    }
    return message;
}

QJsonObject messageJson(const google::protobuf::Message& message)
{
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    if (!google::protobuf::util::MessageToJsonString(message, &json, options).ok()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

void check(const int result)
{
    if (result != MQTTASYNC_SUCCESS) throw std::runtime_error("Paho setup failed: " + std::to_string(result));
}
}  // namespace

MqttObserver::MqttObserver(const std::string& protocolVersion, const unsigned port,
                           const std::optional<unsigned> robotClientId)
{
    if (protocolVersion != version || port == 0 || port > 65535)
        throw std::invalid_argument("explicit rm2026-v2.0.0 and port 1..65535 required");
    if (robotClientId) {
        // Only the six documented player roles per team are selected in this probe.
        constexpr std::array<unsigned, 12> ids{1, 2, 3, 4, 6, 7, 101, 102, 103, 104, 106, 107};
        if (std::find(ids.begin(), ids.end(), *robotClientId) == ids.end())
            throw std::invalid_argument("robot ID must be 1/2/3/4/6/7 or 101/102/103/104/106/107");
    }
    ::rm2026::v2::GameStatus::descriptor(); // Ensure generated schema is linked/registered.
    const auto uri = "tcp://127.0.0.1:" + std::to_string(port);
    clientId_ = robotClientId ? std::to_string(*robotClientId) :
        "rm2026-local-observer-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
    check(MQTTAsync_create(&client_, uri.c_str(), clientId_.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr));
    const int callbacks = MQTTAsync_setCallbacks(client_, this, lost, arrived, nullptr);
    const int connection = MQTTAsync_setConnected(client_, this, connected);
    if (callbacks != MQTTASYNC_SUCCESS || connection != MQTTASYNC_SUCCESS) {
        MQTTAsync_destroy(&client_);
        check(callbacks != MQTTASYNC_SUCCESS ? callbacks : connection);
    }
}

MqttObserver::~MqttObserver()
{
    // Paho joins its callback activity; do not hold our mutex across destruction.
    MQTTAsync_destroy(&client_);
}

void MqttObserver::reset(const char* link)
{
    link_ = link;
    samples_.clear();
    recentEvents_.clear();
    eventHistoryEvicted_ = 0;
    robotId_.reset();
    nextTopic_ = 0;
    pending_ = 0;
}

void MqttObserver::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (link_ != "stopped") throw std::logic_error("observer already started");
    reset("connecting");
    MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
    options.MQTTVersion = MQTTVERSION_3_1_1;
    options.cleansession = 1;
    options.keepAliveInterval = 10;
    options.connectTimeout = 5;
    options.automaticReconnect = 1;
    options.minRetryInterval = 1;
    options.maxRetryInterval = 8;
    options.context = this;
    options.onFailure = connectionFailed;
    check(MQTTAsync_connect(client_, &options));
}

void MqttObserver::connected(void* context, char*)
{
    auto& self = *static_cast<MqttObserver*>(context);
    std::lock_guard<std::mutex> lock(self.mutex_);
    self.reset("awaiting-suback");
    ++self.sessions_;
    self.handshake_ = Clock::now();
    self.subscribeNext();
}

void MqttObserver::subscribeNext()
{
    MQTTAsync_responseOptions options = MQTTAsync_responseOptions_initializer;
    options.context = this;
    options.onSuccess = subscribed;
    options.onFailure = subscriptionFailed;
    // One topic per request: MQTT v3 batch SUBACK API has no qosList length.
    if (MQTTAsync_subscribe(client_, topics[nextTopic_], 1, &options) == MQTTASYNC_SUCCESS)
        pending_ = options.token;
    else reset("subscription-rejected");
}

void MqttObserver::subscribed(void* context, MQTTAsync_successData* data)
{
    auto& self = *static_cast<MqttObserver*>(context);
    std::lock_guard<std::mutex> lock(self.mutex_);
    if (self.link_ != "awaiting-suback" || !data || data->token != self.pending_) return;
    if (Clock::now() - self.handshake_ >= std::chrono::seconds(5)) {
        self.reset("subscription-timeout");
        return;
    }
    if (data->alt.qos != 0 && data->alt.qos != 1) {
        self.reset("subscription-rejected");
        return;
    }
    if (++self.nextTopic_ == topics.size()) self.link_ = "subscribed";
    else self.subscribeNext();
}

void MqttObserver::subscriptionFailed(void* context, MQTTAsync_failureData*)
{
    auto& self = *static_cast<MqttObserver*>(context);
    std::lock_guard<std::mutex> lock(self.mutex_);
    self.reset("subscription-rejected");
}

void MqttObserver::lost(void* context, char*)
{
    auto& self = *static_cast<MqttObserver*>(context);
    std::lock_guard<std::mutex> lock(self.mutex_);
    self.reset("offline");
}

void MqttObserver::connectionFailed(void* context, MQTTAsync_failureData*) { lost(context, nullptr); }

int MqttObserver::arrived(void* context, char* name, const int length, MQTTAsync_message* message)
{
    auto& self = *static_cast<MqttObserver*>(context);
    {
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (self.link_ != "subscribed" || message->qos < 0 || message->qos > 1) ++self.rejected_;
        else if (message->retained) ++self.retained_;
        else if (message->dup) ++self.duplicates_; // Fail closed even for unknown DUPs.
        else {
            const std::string topic(name, length > 0 ? length : std::strlen(name));
            // The pinned Paho packet factory bounds input before allocating its body.
            auto decoded = decode(topic, message->payload, message->payloadlen);
            if (!decoded) ++self.rejected_;
            else {
                if (topic == "RobotStaticStatus") {
                    const auto& config = static_cast<const ::rm2026::v2::RobotStaticStatus&>(*decoded);
                    const auto id = config.has_robot_id() ? std::optional<std::uint32_t>(config.robot_id()) : std::nullopt;
                    if (id != self.robotId_) {
                        self.samples_.clear();
                        self.recentEvents_.clear();
                        self.eventHistoryEvicted_ = 0;
                    }
                    self.robotId_ = id;
                }
                if (topic == "Event") {
                    auto event = mapEvent(static_cast<const ::rm2026::v2::Event&>(*decoded));
                    if (event.param && event.param->size() > 4096) {
                        size_t end = 4096;
                        while (end && (static_cast<unsigned char>((*event.param)[end]) & 0xc0) == 0x80) --end;
                        event.param->resize(end);
                        *event.param += "..."; // Display truncation, not a reconstructed gameplay parameter.
                    }
                    self.recentEvents_.push_back({++self.eventSequence_, std::move(event)});
                    if (self.recentEvents_.size() > 24) {
                        self.recentEvents_.pop_front();
                        ++self.eventHistoryEvicted_;
                    }
                }
                self.samples_[topic] = {std::move(decoded), Clock::now()};
                ++self.accepted_;
            }
        }
    }
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(name);
    return 1; // Receipt acknowledged; invalid data still does not become fresh.
}

QJsonObject MqttObserver::status()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (link_ == "awaiting-suback" && Clock::now() - handshake_ >= std::chrono::seconds(5))
        reset("subscription-timeout"); // Late callbacks cannot reopen a timed-out session.
    QJsonObject fresh;
    QJsonArray stale;
    const auto now = Clock::now();
    for (const auto& [topic, sample] : samples_) {
        if (now - sample.received >= std::chrono::milliseconds(1500))
            stale.append(QString::fromStdString(topic));
        else fresh.insert(QString::fromStdString(topic), messageJson(*sample.message));
    }
    return {{"protocol", version}, {"local_only", true}, {"link", QString::fromStdString(link_)},
            {"client_id", QString::fromStdString(clientId_)},
            {"sessions", int(sessions_)}, {"fresh", fresh}, {"stale", stale},
            {"state", snapshotJson(snapshotAt(now))},
            {"event_history_size", int(recentEvents_.size())},
            {"event_history_evicted", QString::number(eventHistoryEvicted_)},
            {"counts", QJsonObject{{"accepted", int(accepted_)}, {"rejected", int(rejected_)},
                {"retained", int(retained_)}, {"duplicates", int(duplicates_)}}}};
}

historical::rm2026::Snapshot MqttObserver::snapshotAt(const Clock::time_point now) const
{
    historical::rm2026::Snapshot state;
    state.recentEvents.assign(recentEvents_.begin(), recentEvents_.end());
    state.eventHistoryEvicted = eventHistoryEvicted_;
    for (const auto& [topic, sample] : samples_) {
        if (now - sample.received >= std::chrono::milliseconds(1500)) continue;
        if (topic == "RobotStaticStatus") state.selfConfig = {
            mapConfig(static_cast<const ::rm2026::v2::RobotStaticStatus&>(*sample.message)), sample.received};
        if (topic == "RobotDynamicStatus") state.selfDynamic = {
            mapDynamic(static_cast<const ::rm2026::v2::RobotDynamicStatus&>(*sample.message)), sample.received};
        if (topic == "GlobalUnitStatus") state.units = {
            mapUnits(static_cast<const ::rm2026::v2::GlobalUnitStatus&>(*sample.message)), sample.received};
        if (topic == "GlobalLogisticsStatus") state.economy = {
            mapEconomy(static_cast<const ::rm2026::v2::GlobalLogisticsStatus&>(*sample.message)), sample.received};
        if (topic == "GameStatus") state.game = {
            mapGame(static_cast<const ::rm2026::v2::GameStatus&>(*sample.message)), sample.received};
        if (topic == "PenaltyInfo") state.penalty = {
            mapPenalty(static_cast<const ::rm2026::v2::PenaltyInfo&>(*sample.message)), sample.received};
        if (topic == "RobotModuleStatus") state.modules = {
            mapModules(static_cast<const ::rm2026::v2::RobotModuleStatus&>(*sample.message)), sample.received};
        if (topic == "RobotRespawnStatus") state.respawn = {
            mapRespawn(static_cast<const ::rm2026::v2::RobotRespawnStatus&>(*sample.message)), sample.received};
        if (topic == "Event") state.latestEvent = {
            mapEvent(static_cast<const ::rm2026::v2::Event&>(*sample.message)), sample.received};
        if (topic == "RobotPosition") state.position = {
            mapPosition(static_cast<const ::rm2026::v2::RobotPosition&>(*sample.message)), sample.received};
        if (topic == "Buff") state.latestBuff = {
            mapBuff(static_cast<const ::rm2026::v2::Buff&>(*sample.message)), sample.received};
        if (topic == "RobotPerformanceSelectionSync") state.performance = {
            mapPerformance(static_cast<const ::rm2026::v2::RobotPerformanceSelectionSync&>(*sample.message)), sample.received};
        if (topic == "DeployModeStatusSync") state.deployment = {
            mapDeployment(static_cast<const ::rm2026::v2::DeployModeStatusSync&>(*sample.message)), sample.received};
    }
    return state;
}

historical::rm2026::Snapshot MqttObserver::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotAt(Clock::now());
}
}  // namespace rm27::protocol::rm2026
