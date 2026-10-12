#include "core/GameData.h"
#include "protocol/rm2026/state_mapping.hpp"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/unknown_field_set.h>
#include <cmath>

namespace domain = rm27::historical::rm2026;
namespace mapping = rm27::protocol::rm2026;
namespace {
constexpr auto freshness = std::chrono::milliseconds(1500); // Project policy.
bool fresh(std::chrono::steady_clock::time_point at) {
    const auto now = std::chrono::steady_clock::now();
    return now >= at && now - at < freshness;
}
QVariant fieldValue(const google::protobuf::Message &message,
                    const google::protobuf::FieldDescriptor *field, int index = -1) {
    const auto *r = message.GetReflection();
    using F = google::protobuf::FieldDescriptor;
    const bool repeated = index >= 0;
    switch (field->cpp_type()) {
    case F::CPPTYPE_UINT32: return repeated ? r->GetRepeatedUInt32(message, field, index) : r->GetUInt32(message, field);
    case F::CPPTYPE_INT32: return repeated ? r->GetRepeatedInt32(message, field, index) : r->GetInt32(message, field);
    case F::CPPTYPE_UINT64: return QString::number(r->GetUInt64(message, field)); // Lossless diagnostic JSON.
    case F::CPPTYPE_BOOL: return r->GetBool(message, field);
    case F::CPPTYPE_FLOAT: return r->GetFloat(message, field);
    case F::CPPTYPE_STRING: {
        const auto value = r->GetString(message, field);
        if (field->type() == F::TYPE_BYTES)
            return QString::fromLatin1(QByteArray(value.data(), qsizetype(value.size())).toHex());
        return QString::fromUtf8(value.data(), qsizetype(value.size()));
    }
    default: return {};
    }
}
}

QStringList GameData::officialTopics() {
    return {"GameStatus", "GlobalUnitStatus", "GlobalLogisticsStatus", "GlobalSpecialMechanism",
        "Event", "RobotInjuryStat", "RobotRespawnStatus", "RobotStaticStatus", "RobotDynamicStatus",
        "RobotModuleStatus", "RobotPosition", "Buff", "PenaltyInfo", "RobotPathPlanInfo", "MapClickInfo",
        "CustomByteBlock", "TechCoreMotionStateSync", "RobotPerformanceSelectionSync", "DeployModeStatusSync",
        "RuneStatusSync", "SentryStatusSync", "DartSelectTargetStatusSync", "SentryCtrlResult", "AirSupportStatusSync"};
}

void GameData::resetOfficialReports() {
    m_officialReports.clear();
    m_officialEvents.clear();
    m_officialIdentity.reset();
    m_officialEventSequence = 0;
}

bool GameData::acceptOfficialReport(const QString &topic, const QByteArray &payload, qint64 receivedMonoMs) {
    const auto reject = [this] { ++m_officialRejected; return false; };
    if (!officialTopics().contains(topic) || payload.size() > 65536) return reject();
    const auto *descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
        "robomaster." + topic.toStdString());
    if (!descriptor) return reject();
    const auto *prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
    std::unique_ptr<google::protobuf::Message> message(prototype->New());
    if (!message->ParseFromArray(payload.constData(), int(payload.size()))) return reject();
    std::vector<const google::protobuf::FieldDescriptor *> fields;
    const auto *reflection = message->GetReflection();
    reflection->ListFields(*message, &fields);
    // This repeated-list topic can explicitly report no active mechanisms.
    // Unknown-only payloads and empty singular-field messages remain invalid.
    if (fields.empty() && (topic != "GlobalSpecialMechanism"
        || !reflection->GetUnknownFields(*message).empty())) return reject();
    const auto at = std::chrono::steady_clock::time_point(std::chrono::milliseconds(receivedMonoMs));
    if (!fresh(at)) return reject(); // Validate age before any identity/state mutation.
    for (const auto *field : fields) {
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT
            && !std::isfinite(reflection->GetFloat(*message, field))) return reject();
    }
    if (topic == "GlobalUnitStatus") {
        const auto &value = static_cast<const robomaster::GlobalUnitStatus &>(*message);
        if ((value.robot_health_size() != 0 && value.robot_health_size() != 10)
            || (value.robot_bullets_size() != 0 && value.robot_bullets_size() != 10)) return reject();
    } else if (topic == "CustomByteBlock") {
        if (static_cast<const robomaster::CustomByteBlock &>(*message).data().size() > 300) return reject();
    } else if (topic == "GlobalSpecialMechanism") {
        const auto &value = static_cast<const robomaster::GlobalSpecialMechanism &>(*message);
        if (value.mechanism_id_size() != value.mechanism_time_sec_size()) return reject();
    } else if (topic == "RobotPathPlanInfo") {
        const auto &value = static_cast<const robomaster::RobotPathPlanInfo &>(*message);
        if (value.offset_x_size() != value.offset_y_size() || value.offset_x_size() > 49) return reject();
        for (auto offset : value.offset_x()) if (offset < -128 || offset > 127) return reject();
        for (auto offset : value.offset_y()) if (offset < -128 || offset > 127) return reject();
    } else if (topic == "MapClickInfo") {
        const auto &value = static_cast<const robomaster::MapClickInfo &>(*message);
        if (value.has_robot_id() && value.robot_id().size() != 7) return reject();
    } else if (topic == "RobotStaticStatus") {
        const auto &value = static_cast<const robomaster::RobotStaticStatus &>(*message);
        if (value.has_robot_id()) {
            if (m_officialIdentity && *m_officialIdentity != value.robot_id()) resetOfficialReports();
            m_officialIdentity = value.robot_id();
        }
    }
    if (topic == "PenaltyInfo") {
        const auto kind = mapping::mapPenalty(static_cast<const robomaster::PenaltyInfo &>(*message)).kind;
        if (kind == domain::PenaltyKind::YellowCard || kind == domain::PenaltyKind::BothYellowCards
            || kind == domain::PenaltyKind::RedCard || kind == domain::PenaltyKind::Unknown)
            m_penaltyReleaseUnverified = true;
    }
    if (topic == "Event") {
        auto &event = static_cast<robomaster::Event &>(*message);
        if (event.param().size() > 4096) event.mutable_param()->resize(4096);
        m_officialEvents.push_back({++m_officialEventSequence, mapping::mapEvent(event)});
        if (m_officialEvents.size() > 24) m_officialEvents.erase(m_officialEvents.begin());
    }
    m_officialReports[topic] = {std::move(message), at};
    ++m_officialAccepted;
    return true;
}

domain::Snapshot GameData::officialSnapshot() const {
    domain::Snapshot result;
    result.penaltyReleaseUnverified = m_penaltyReleaseUnverified;
    for (const auto &[topic, report] : m_officialReports) {
        if (!fresh(report.at)) continue;
#define MAP_TOPIC(Name, member, mapper) \
        if (topic == #Name) result.member = {mapping::mapper(static_cast<const robomaster::Name &>(*report.value)), report.at};
        MAP_TOPIC(RobotStaticStatus, selfConfig, mapConfig)
        MAP_TOPIC(RobotDynamicStatus, selfDynamic, mapDynamic)
        MAP_TOPIC(GlobalUnitStatus, units, mapUnits)
        MAP_TOPIC(GlobalLogisticsStatus, economy, mapEconomy)
        MAP_TOPIC(GlobalSpecialMechanism, specialMechanisms, mapSpecialMechanisms)
        MAP_TOPIC(RobotInjuryStat, injury, mapInjury)
        MAP_TOPIC(RobotPathPlanInfo, pathPlan, mapPathPlan)
        MAP_TOPIC(MapClickInfo, mapClick, mapMapClick)
        MAP_TOPIC(CustomByteBlock, customBytes, mapCustomBytes)
        MAP_TOPIC(GameStatus, game, mapGame)
        MAP_TOPIC(PenaltyInfo, penalty, mapPenalty)
        MAP_TOPIC(RobotModuleStatus, modules, mapModules)
        MAP_TOPIC(RobotRespawnStatus, respawn, mapRespawn)
        MAP_TOPIC(Event, latestEvent, mapEvent)
        MAP_TOPIC(RobotPosition, position, mapPosition)
        MAP_TOPIC(Buff, latestBuff, mapBuff)
        MAP_TOPIC(RobotPerformanceSelectionSync, performance, mapPerformance)
        MAP_TOPIC(DeployModeStatusSync, deployment, mapDeployment)
        MAP_TOPIC(TechCoreMotionStateSync, techCore, mapTechCore)
        MAP_TOPIC(RuneStatusSync, rune, mapRune)
        MAP_TOPIC(SentryStatusSync, sentryStatus, mapSentryStatus)
        MAP_TOPIC(DartSelectTargetStatusSync, dartStatus, mapDartStatus)
        MAP_TOPIC(AirSupportStatusSync, airSupport, mapAirSupport)
        MAP_TOPIC(SentryCtrlResult, sentryResult, mapSentryResult)
#undef MAP_TOPIC
    }
    result.recentEvents = m_officialEvents;
    return result;
}

QVariantMap GameData::officialReports() const {
    QVariantMap result;
    for (const auto &[topic, report] : m_officialReports) {
        if (!fresh(report.at)) continue;
        QVariantMap values;
        const auto &message = *report.value;
        const auto *reflection = message.GetReflection();
        std::vector<const google::protobuf::FieldDescriptor *> fields;
        reflection->ListFields(message, &fields);
        for (const auto *field : fields) {
            const auto name = QString::fromStdString(field->name());
            if (field->is_repeated()) {
                QVariantList entries;
                for (int i = 0; i < reflection->FieldSize(message, field); ++i) entries.append(fieldValue(message, field, i));
                values.insert(name, entries);
            } else values.insert(name, fieldValue(message, field));
        }
        result.insert(topic, QVariantMap{{"fields", values}, {"age_ms", qlonglong(std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() - report.at).count())}});
    }
    return result;
}
