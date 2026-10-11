#include "sim/sim_referee_receiver.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkDatagram>

#include <cmath>

namespace rm27::sim {
namespace {

constexpr qint64 kMaximumDatagramSize = 4096;
constexpr int kStaleAfterMs = 600;

std::optional<int> boundedInteger(
    const QJsonObject& object, const char* key, const int minimum, const int maximum)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

std::optional<double> boundedNumber(
    const QJsonObject& object, const char* key, const double minimum, const double maximum)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum) {
        return std::nullopt;
    }
    return number;
}

std::optional<MatchPhase> parsePhase(const QString& phase)
{
    if (phase == QLatin1String("preparation")) {
        return MatchPhase::Preparation;
    }
    if (phase == QLatin1String("self_check")) {
        return MatchPhase::SelfCheck;
    }
    if (phase == QLatin1String("countdown")) {
        return MatchPhase::Countdown;
    }
    if (phase == QLatin1String("match")) {
        return MatchPhase::Match;
    }
    if (phase == QLatin1String("ended")) {
        return MatchPhase::Ended;
    }
    return std::nullopt;
}

std::optional<RefereeSample::Hud> parseHud(const QJsonObject& object)
{
    RefereeSample::Hud hud;
    hud.ownTeam = object.value(QStringLiteral("own_team")).toString();
    if (hud.ownTeam != QLatin1String("red") && hud.ownTeam != QLatin1String("blue")) {
        return std::nullopt;
    }
    const auto friendlyCoins = boundedInteger(object, "friendly_coins", 0, 1000000);
    const auto enemyCoins = boundedInteger(object, "enemy_coins", 0, 1000000);
    const auto restartCount = boundedInteger(object, "restart_count", 0, 1000);
    const auto pitch = boundedNumber(object, "pitch_deg", -45.0, 45.0);
    const auto speed = boundedNumber(object, "speed_mps", 0.0, 100.0);
    const auto shots = boundedInteger(object, "allowed_shots", 0, 1000000);
    if (!friendlyCoins || !enemyCoins || !restartCount || !pitch || !speed || !shots) {
        return std::nullopt;
    }
    hud.friendlyCoins = *friendlyCoins;
    hud.enemyCoins = *enemyCoins;
    hud.restartCount = *restartCount;
    hud.pitchDeg = *pitch;
    hud.speedMps = *speed;
    hud.allowedShots = *shots;

    if (object.contains(QStringLiteral("level"))) {
        hud.level = boundedInteger(object, "level", 1, 10);
        if (!hud.level) return std::nullopt;
    }

    // 偏航角同样是可选的：不带此字段的旧模拟包照常解析，
    // 带了但越界或不是数字的包按既有约定整包拒绝。
    if (object.contains(QStringLiteral("yaw_deg"))) {
        hud.yawDeg = boundedNumber(object, "yaw_deg", -180.0, 180.0);
        if (!hud.yawDeg) return std::nullopt;
    }

    // 本车下标：可选，0..5，对应队列表的六个位置；越界或类型错误按约定拒绝整包。
    if (object.contains(QStringLiteral("self_index"))) {
        hud.selfIndex = boundedInteger(object, "self_index", 0, 5);
        if (!hud.selfIndex) return std::nullopt;
    }
    const bool hasShield = object.contains(QStringLiteral("shield"));
    const bool hasMaxShield = object.contains(QStringLiteral("max_shield"));
    if (hasShield != hasMaxShield) return std::nullopt;
    if (hasShield) {
        hud.shield = boundedInteger(object, "shield", 0, 1000);
        hud.maxShield = boundedInteger(object, "max_shield", 1, 1000);
        if (!hud.shield || !hud.maxShield || *hud.shield > *hud.maxShield) {
            return std::nullopt;
        }
    }
    if (object.contains(QStringLiteral("actions"))) {
        const auto value = object.value(QStringLiteral("actions"));
        if (!value.isObject()) return std::nullopt;
        const auto actions = value.toObject();
        const auto climb = actions.value(QStringLiteral("climb"));
        const auto fire = actions.value(QStringLiteral("fire"));
        const auto speedUp = actions.value(QStringLiteral("speed_up"));
        if (!climb.isBool() || !fire.isBool() || !speedUp.isBool()) {
            return std::nullopt;
        }
        hud.actions = RefereeSample::Hud::Actions{
            climb.toBool(), fire.toBool(), speedUp.toBool()};
    }

    if (object.contains(QStringLiteral("structures"))) {
        const auto value = object.value(QStringLiteral("structures"));
        if (!value.isObject()) return std::nullopt;
        const auto structures = value.toObject();
        std::array<RefereeSample::Hud::Structure, 4> parsed;
        constexpr std::array names{"friendly_base", "enemy_base", "friendly_outpost", "enemy_outpost"};
        for (size_t index = 0; index < names.size(); ++index) {
            const auto item = structures.value(QLatin1String(names[index]));
            if (!item.isObject()) return std::nullopt;
            const auto hp = boundedInteger(item.toObject(), "hp", 0, 1000000);
            const auto maximum = boundedInteger(item.toObject(), "max_hp", 1, 1000000);
            if (!hp || !maximum || *hp > *maximum) return std::nullopt;
            parsed[index] = {*hp, *maximum};
        }
        hud.structures = parsed;
    }

    const auto friendlyHp = object.value(QStringLiteral("friendly_hp")).toArray();
    const auto enemyHp = object.value(QStringLiteral("enemy_hp")).toArray();
    const auto friendlyPositions = object.value(QStringLiteral("friendly_positions")).toArray();
    const auto enemyPositions = object.value(QStringLiteral("enemy_positions")).toArray();
    if (friendlyHp.size() != 6 || enemyHp.size() != 6
        || friendlyPositions.size() != 6 || enemyPositions.size() != 6) {
        return std::nullopt;
    }
    for (int index = 0; index < 6; ++index) {
        const QJsonObject hpObject{{QStringLiteral("value"), friendlyHp.at(index)}};
        const QJsonObject enemyHpObject{{QStringLiteral("value"), enemyHp.at(index)}};
        const auto friendlyValue = boundedInteger(hpObject, "value", 0, 1000000);
        const auto enemyValue = boundedInteger(enemyHpObject, "value", 0, 1000000);
        const auto friendlyPoint = friendlyPositions.at(index).toArray();
        const auto enemyPoint = enemyPositions.at(index).toArray();
        if (!friendlyValue || !enemyValue || friendlyPoint.size() != 2 || enemyPoint.size() != 2) {
            return std::nullopt;
        }
        hud.friendlyHp[index] = *friendlyValue;
        hud.enemyHp[index] = *enemyValue;
        for (int axis = 0; axis < 2; ++axis) {
            const QJsonObject friendlyCoordinate{{QStringLiteral("value"), friendlyPoint.at(axis)}};
            const QJsonObject enemyCoordinate{{QStringLiteral("value"), enemyPoint.at(axis)}};
            const auto friendlyNumber = boundedNumber(friendlyCoordinate, "value", 0.0, 1.0);
            const auto enemyNumber = boundedNumber(enemyCoordinate, "value", 0.0, 1.0);
            if (!friendlyNumber || !enemyNumber) {
                return std::nullopt;
            }
            hud.friendlyPositions[index][axis] = *friendlyNumber;
            hud.enemyPositions[index][axis] = *enemyNumber;
        }
    }
    return hud;
}

std::optional<RefereeSample::Event> parseEvent(const QJsonObject& object)
{
    RefereeSample::Event event{
        object.value(QStringLiteral("id")).toString(),
        object.value(QStringLiteral("category")).toString(),
        object.value(QStringLiteral("text")).toString(),
    };
    if (event.id.isEmpty() || event.id.size() > 48
        || (event.category != QLatin1String("system")
            && event.category != QLatin1String("robot"))
        || event.text.isEmpty() || event.text.size() > 120) {
        return std::nullopt;
    }
    for (const QChar character : event.id) {
        const ushort code = character.unicode();
        if (!((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')
              || (code >= '0' && code <= '9') || code == '-' || code == '_')) {
            return std::nullopt;
        }
    }
    for (const auto character : event.text.toUcs4()) {
        if (!QChar::isPrint(character)) {
            return std::nullopt;
        }
    }
    return event;
}

}  // namespace

std::optional<RefereeSample> parseRefereeDatagram(const QByteArray& payload)
{
    if (payload.isEmpty() || payload.size() > kMaximumDatagramSize) {
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("channel")).toString()
        != QLatin1String("rm27.sim.v1.referee")) {
        return std::nullopt;
    }

    const auto sequenceValue = root.value(QStringLiteral("sequence"));
    const auto timestampValue = root.value(QStringLiteral("sent_at_unix_ms"));
    const auto phase = parsePhase(root.value(QStringLiteral("phase")).toString());
    const auto telemetryValue = root.value(QStringLiteral("telemetry"));
    if (!sequenceValue.isDouble() || !timestampValue.isDouble() || !phase
        || !telemetryValue.isObject()) {
        return std::nullopt;
    }

    const double sequenceNumber = sequenceValue.toDouble();
    const double timestampNumber = timestampValue.toDouble();
    if (!std::isfinite(sequenceNumber) || std::floor(sequenceNumber) != sequenceNumber
        || sequenceNumber < 0.0 || sequenceNumber > 9007199254740991.0
        || !std::isfinite(timestampNumber) || std::floor(timestampNumber) != timestampNumber
        || timestampNumber <= 0.0 || timestampNumber > 9007199254740991.0) {
        return std::nullopt;
    }

    const auto telemetry = telemetryValue.toObject();
    const auto hp = boundedInteger(telemetry, "hp", 0, 1000000);
    const auto maxHp = boundedInteger(telemetry, "max_hp", 1, 1000000);
    const auto power = boundedNumber(telemetry, "chassis_power_w", 0.0, 1000000.0);
    const auto energy = boundedNumber(telemetry, "buffer_energy_j", 0.0, 1000000.0);
    const auto heat = boundedInteger(telemetry, "barrel_heat", 0, 1000000);
    const auto ammo = boundedInteger(telemetry, "ammo", 0, 1000000);
    const auto latency = boundedInteger(telemetry, "latency_ms", 0, 60000);
    const auto packetLoss = boundedNumber(telemetry, "packet_loss_pct", 0.0, 100.0);
    if (!hp || !maxHp || *hp > *maxHp || !power || !energy || !heat || !ammo
        || !latency || !packetLoss) {
        return std::nullopt;
    }

    std::optional<RefereeSample::Hud> hud;
    const auto hudValue = root.value(QStringLiteral("hud"));
    if (!hudValue.isUndefined()) {
        if (!hudValue.isObject()) {
            return std::nullopt;
        }
        hud = parseHud(hudValue.toObject());
        if (!hud) {
            return std::nullopt;
        }
    }

    std::optional<RefereeSample::Event> event;
    const auto eventValue = root.value(QStringLiteral("event"));
    if (!eventValue.isUndefined()) {
        if (!eventValue.isObject()) {
            return std::nullopt;
        }
        event = parseEvent(eventValue.toObject());
        if (!event) {
            return std::nullopt;
        }
    }

    return RefereeSample{
        static_cast<quint64>(sequenceNumber),
        static_cast<qint64>(timestampNumber),
        *phase,
        *hp,
        *maxHp,
        *power,
        *energy,
        *heat,
        *ammo,
        *latency,
        *packetLoss,
        hud,
        event,
    };
}

std::optional<RadarSample> parseRadarDatagram(const QByteArray& payload)
{
    if (payload.isEmpty() || payload.size() > kMaximumDatagramSize) return std::nullopt;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto root = document.object();
    if (root.value("channel").toString() != QLatin1String("rm27.sim.v1.radar")) return std::nullopt;
    const auto sequence = boundedNumber(root, "sequence", 0, 9007199254740991.0);
    const auto sentAt = boundedNumber(root, "sent_at_unix_ms", 1, 9007199254740991.0);
    if (!sequence || std::floor(*sequence) != *sequence
        || !sentAt || std::floor(*sentAt) != *sentAt) return std::nullopt;
    RadarSample sample;
    sample.sequence = static_cast<quint64>(*sequence);
    constexpr std::array names{"red", "blue"};
    for (size_t team = 0; team < names.size(); ++team) {
        const auto value = root.value(QLatin1String(names[team]));
        if (!value.isArray() || value.toArray().size() > 6) return std::nullopt;
        std::array<bool, 6> seen{};
        for (const auto point : value.toArray()) {
            if (!point.isObject()) return std::nullopt;
            const auto object = point.toObject();
            const auto id = boundedInteger(object, "id", 0, 5);
            const auto x = boundedNumber(object, "x", 0, 1);
            const auto y = boundedNumber(object, "y", 0, 1);
            if (!id || !x || !y || seen[*id]) return std::nullopt;
            seen[*id] = true;
            sample.teams[team].push_back({*id, *x, *y});
        }
    }
    return sample;
}

RefereeReceiver::RefereeReceiver(QObject* parent)
    : QObject(parent)
{
    staleTimer_.setSingleShot(true);
    staleTimer_.setInterval(kStaleAfterMs);
    connect(&socket_, &QUdpSocket::readyRead, this, &RefereeReceiver::readPendingDatagrams);
    connect(&staleTimer_, &QTimer::timeout, this, [this] {
        receivedAny_ = false;
        lastSequence_ = 0;
        emit dataBecameStale();
    });
    radarStaleTimer_.setSingleShot(true);
    radarStaleTimer_.setInterval(kStaleAfterMs);
    connect(&radarStaleTimer_, &QTimer::timeout, this, [this] {
        radarReceivedAny_ = false;
        lastRadarSequence_ = 0;
        emit radarBecameStale();
    });
}

bool RefereeReceiver::start(const quint16 port)
{
    // 启动面板允许改端口：重新绑定前先释放，避免二次 bind 失败。
    socket_.close();
    const bool hadReferee = receivedAny_;
    const bool hadRadar = radarReceivedAny_;
    staleTimer_.stop();
    radarStaleTimer_.stop();
    receivedAny_ = radarReceivedAny_ = false;
    lastSequence_ = lastRadarSequence_ = 0;
    if (hadReferee) emit dataBecameStale();
    if (hadRadar) emit radarBecameStale();
    return socket_.bind(QHostAddress::LocalHost, port);
}

void RefereeReceiver::readPendingDatagrams()
{
    while (socket_.hasPendingDatagrams()) {
        const qint64 pendingSize = socket_.pendingDatagramSize();
        const auto datagram = socket_.receiveDatagram(kMaximumDatagramSize);
        if (pendingSize <= 0 || pendingSize > kMaximumDatagramSize
            || !datagram.senderAddress().isLoopback()) {
            continue;
        }

        acceptPayload(datagram.data());
    }
}

void RefereeReceiver::acceptPayload(const QByteArray& payload)
{
    if (const auto radar = parseRadarDatagram(payload)) {
        if (radarReceivedAny_ && radar->sequence <= lastRadarSequence_) return;
        radarReceivedAny_ = true;
        lastRadarSequence_ = radar->sequence;
        radarStaleTimer_.start();
        emit radarReceived(*radar);
        return;
    }
    const auto sample = parseRefereeDatagram(payload);
    if (!sample || (receivedAny_ && sample->sequence <= lastSequence_)) return;
    receivedAny_ = true;
    lastSequence_ = sample->sequence;
    staleTimer_.start();
    emit sampleReceived(*sample);
}

}  // namespace rm27::sim
