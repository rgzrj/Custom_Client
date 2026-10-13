#include "protocol/rm2026/upstream_commands.hpp"
#include "protocol/rm2026/upstream_backend.hpp"
#include "network/NetworkManager.h"
#include "network/MqttManager.h"
#include "protocol/rm2026/state_mapping.hpp"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <atomic>
#include <algorithm>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace rm27;
using namespace rm27::historical::rm2026;
using namespace rm27::protocol::rm2026;

namespace {
struct Fixture { Command command; const char *topic; const char *hex; };
std::vector<Fixture> fixtures() {
    MapClickCommand map;
    map.sendAll = 2; map.robotIds[0] = 3; map.markingType = 4; map.ascii = 'C';
    map.x = 1.25f; map.y = 2.5f;
    // Independent fixed wire expectations from official §2.2 optional fields.
    return {{KeyboardMouseCommand{}, "KeyboardMouseControl", "0800100018002000280030003800"},
        {CustomControlCommand{{0, 255}}, "CustomControl", "0a0200ff"},
        {map, "MapClickCmd", "080212070300000000000018042000284330013d0000a03f4500002040"},
        {AssemblyCommand{0, 4}, "AssemblyCommand", "08001004"},
        {PerformanceCommand{1, 2, 0}, "RobotPerformanceSelectionCommand", "080110021800"},
        {CommonCommand{1, 10}, "CommonCommand", "0801100a"},
        {DeployCommand{0}, "HeroDeployModeEventCommand", "0800"},
        {RuneCommand{1}, "RuneActivateCommand", "0801"},
        {DartCommand{5, false, true}, "DartCommand", "080510001801"},
        {SentryCommand{12}, "SentryCtrlCommand", "080c"},
        {AirSupportCommand{0}, "AirSupportCommand", "0800"}};
}
CommandContext permittedFixture() {
    CommandContext c;
    c.terminal.mode = TerminalMode::HeavyOperator;
    c.terminal.phase = MatchPhase::Match;
    c.terminal.referee = {true, true}; c.terminal.controller = {true, true};
    c.terminal.controlsArmed = true;
    c.requestedId = c.reportedId = 3; c.identityFresh = true;
    // Only this isolated test creates the policy permit. Ordinary UI never does.
    c.businessPolicyReady = c.keyboardSourceSelected = c.mapCalibrated = true;
    c.mapOperator = MapOperator::SemiAutomatic;
    return c;
}
QByteArray packet(unsigned char header, const QByteArray &body) {
    QByteArray out(1, char(header));
    unsigned size = body.size();
    do { unsigned byte = size % 128; size /= 128;
        out.append(char(byte | (size ? 128 : 0))); } while (size);
    return out + body;
}
// Independent minimal loopback peer. Never accesses any external broker/device.
class Peer {
public:
    struct Received { unsigned char header; QByteArray topic, payload; };
    std::atomic<bool> stopped{false}, acknowledge{true}, refresh{false}, paused{false};
    std::atomic<bool> dropEventAcks{false};
    std::atomic<unsigned> reportedId{3}, ackFault{0}, ackDelayMs{0};
    std::atomic<unsigned> alive{1}, stage{4}, runeCode{1}, gold{100}, canFree{1}, canPay{1};
    std::atomic<int> penaltyType{-1}; // Absent unless explicitly injected by this test peer.
    std::atomic<bool> remoteEligible{false};
    std::atomic<int> deploymentState{-1};
    quint16 port;
    std::mutex mutex;
    std::vector<Received> received;
    QString error;
    std::thread thread;
    Peer() {
        std::promise<quint16> ready;
        auto future = ready.get_future();
        thread = std::thread([this, &ready] {
            QTcpServer server;
            if (!server.listen(QHostAddress::LocalHost)) { ready.set_value(0); return; }
            ready.set_value(server.serverPort());
            if (!server.waitForNewConnection(5000)) return;
            std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
            const auto exact = [&](int size) {
                QByteArray out;
                while (out.size() < size) {
                    if (!socket->bytesAvailable() && !socket->waitForReadyRead(2000))
                        throw std::runtime_error("incomplete MQTT packet");
                    out += socket->read(size - out.size());
                }
                return out;
            };
            const auto send = [&](const QByteArray &bytes) {
                socket->write(bytes); socket->flush();
            };
            const auto report = [&] {
                if (penaltyType >= 0) {
                    const QByteArray topic("PenaltyInfo");
                    send(packet(0x30, QByteArray::fromHex("000b") + topic
                        + QByteArray(1, char(8)) + QByteArray(1, char(penaltyType.load()))
                        + QByteArray::fromHex("10001800")));
                }
                if (remoteEligible) {
                    const QByteArray topic("RobotDynamicStatus");
                    send(packet(0x30, QByteArray::fromHex("0012") + topic + QByteArray::fromHex("500160016801")));
                }
                for (const auto &sample : std::vector<std::pair<QByteArray, QByteArray>>{
                    {"RobotStaticStatus", QByteArray::fromHex("0801100018") + QByteArray(1, char(alive.load()))
                        + QByteArray(1, char(0x20)) + QByteArray(1, char(reportedId.load()))},
                    // Identity changes deliberately clear all earlier reports.
                    {"GameStatus", QByteArray(1, char(0x28)) + QByteArray(1, char(stage.load()))
                        + QByteArray::fromHex(paused ? "4001" : "4000")},
                    {"RuneStatusSync", QByteArray(1, char(8)) + QByteArray(1, char(runeCode.load()))},
                    {"RobotRespawnStatus", QByteArray::fromHex("080120") + QByteArray(1, char(canFree.load()))
                        + QByteArray::fromHex("285030") + QByteArray(1, char(canPay.load()))},
                    {"GlobalLogisticsStatus", QByteArray(1, char(8)) + QByteArray(1, char(gold.load()))}}) {
                    QByteArray body;
                    body.append(char(0)); body.append(char(sample.first.size()));
                    send(packet(0x30, body + sample.first + sample.second));
                }
                if (deploymentState >= 0) {
                    const QByteArray topic("DeployModeStatusSync");
                    send(packet(0x30, QByteArray::fromHex("0014") + topic
                        + QByteArray(1, char(8)) + QByteArray(1, char(deploymentState.load()))));
                }
            };
            int subscribed = 0;
            std::vector<QByteArray> pendingAcks;
            try {
                while (!stopped) {
                    if (refresh.exchange(false)) report();
                    if (acknowledge && !pendingAcks.empty()) {
                        for (const auto &ack : pendingAcks) send(packet(0x40, ack));
                        pendingAcks.clear();
                    }
                    if (!socket->bytesAvailable() && !socket->waitForReadyRead(10)) {
                        if (socket->state() == QAbstractSocket::UnconnectedState) break;
                        continue;
                    }
                    const auto header = static_cast<unsigned char>(exact(1)[0]);
                    unsigned length = 0, multiplier = 1, byte;
                    do { byte = static_cast<unsigned char>(exact(1)[0]);
                        length += (byte & 127) * multiplier; multiplier *= 128;
                        if (length > 65536) throw std::runtime_error("oversized packet");
                    } while (byte & 128);
                    const auto body = exact(int(length));
                    if (header == 0x10) send(packet(0x20, QByteArray(2, 0)));
                    else if (header == 0x82) {
                        send(packet(0x90, body.left(2) + QByteArray(1, 1)));
                        if (++subscribed == 24) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100)); report();
                        }
                    } else if ((header >> 4) == 3) {
                        if (body.size() < 4) throw std::runtime_error("short publish");
                        const int n = (quint8(body[0]) << 8) | quint8(body[1]);
                        { std::lock_guard lock(mutex);
                            received.push_back({header, body.mid(2, n), body.mid(4 + n)}); }
                        report();
                        if (dropEventAcks && body.mid(2, n) != "KeyboardMouseControl") continue;
                        if (acknowledge) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(ackDelayMs.load()));
                            auto ack = body.mid(2 + n, 2);
                            if (ackFault == 1) ack = QByteArray(2, 0);
                            if (ackFault == 2) ack = ack.left(1);
                            send(packet(ackFault == 3 ? 0x41 : 0x40, ack));
                        } else pendingAcks.push_back(body.mid(2 + n, 2));
                    } else if (header == 0xc0) send(packet(0xd0, {}));
                    else if (header == 0xe0) break;
                    else throw std::runtime_error("unexpected packet");
                }
            } catch (const std::exception &e) { std::lock_guard lock(mutex); error = e.what(); }
        });
        port = future.get();
    }
    ~Peer() { stopped = true; thread.join(); }
    std::size_t count() { std::lock_guard lock(mutex); return received.size(); }
};
}

class UplinkTest : public QObject {
    Q_OBJECT
private slots:
    void encodingAndRate() {
        for (const auto &f : fixtures()) {
            const auto encoded = encodeCommand(f.command, MapOperator::SemiAutomatic);
            QVERIFY2(encoded.error.isEmpty(), qPrintable(encoded.error));
            QCOMPARE(encoded.topic, QString::fromLatin1(f.topic));
            QCOMPARE(encoded.payload.toHex(), QByteArray(f.hex));
            CommandRateLimiter limiter;
            const auto at = CommandRateLimiter::Clock::time_point{};
            QVERIFY(limiter.acquire(f.command.index(), encoded.interval, at));
            QVERIFY(!limiter.acquire(f.command.index(), encoded.interval, at + encoded.interval - std::chrono::nanoseconds(1)));
            QVERIFY(!limiter.acquire(f.command.index(), encoded.interval, at - std::chrono::nanoseconds(1)));
            QVERIFY(limiter.acquire(f.command.index(), encoded.interval, at + encoded.interval));
        }
        const auto keyboardInterval = encodeCommand(KeyboardMouseCommand{}).interval;
        QCOMPARE(keyboardInterval.count(), 13'333'334LL);
        CommandRateLimiter limiter;
        int admitted = 0;
        const auto at = CommandRateLimiter::Clock::time_point{};
        for (int microsecond = 0; microsecond < 1'000'000; ++microsecond)
            admitted += limiter.acquire(0, keyboardInterval, at + std::chrono::microseconds(microsecond));
        QCOMPARE(admitted, 75);
        const auto partial = encodeCommand(PerformanceCommand{{}, {}, 0});
        QCOMPARE(partial.payload.toHex(), QByteArray("1800")); // absent != explicit zero.
        QVERIFY(!encodeCommand(CommonCommand{1, 11}).error.isEmpty());
        QVERIFY(!encodeCommand(CustomControlCommand{std::vector<std::uint8_t>(31)}).error.isEmpty());
        QVERIFY(!encodeCommand(KeyboardMouseCommand{0, 0, 0, false, false, false, 65536}).error.isEmpty());
        QVERIFY(!encodeCommand(SentryCommand{2}).error.isEmpty());
        QVERIFY(!encodeCommand(DartCommand{0}).error.isEmpty());
        QVERIFY(!encodeCommand(DeployCommand{2}).error.isEmpty());
        QVERIFY(!encodeCommand(RuneCommand{0}).error.isEmpty());
        QVERIFY(!encodeCommand(AirSupportCommand{4}).error.isEmpty());
        QVERIFY(!encodeCommand(AssemblyCommand{3}).error.isEmpty());
        QVERIFY(!encodeCommand(PerformanceCommand{0, 2, 0}).error.isEmpty());
        QVERIFY(!encodeCommand(MapClickCommand{}).error.isEmpty());
        auto map = std::get<MapClickCommand>(fixtures()[2].command);
        map.ascii = 'M'; QVERIFY(!encodeCommand(map, MapOperator::Gimbal).error.isEmpty());
        map.ascii = 'C'; map.x = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!encodeCommand(map, MapOperator::Gimbal).error.isEmpty());
        const auto custom30 = encodeCommand(CustomControlCommand{std::vector<std::uint8_t>(30)});
        QVERIFY(custom30.error.isEmpty()); QCOMPARE(custom30.payload.size(), 32);
        QCOMPARE(encodeCommand(CustomControlCommand{}).payload.toHex(), QByteArray("0a00"));
        const auto signedMouse = encodeCommand(KeyboardMouseCommand{-1, 2, -3, true, false, true, 0x8001});
        QCOMPARE(signedMouse.payload.toHex(), QByteArray("08ffffffffffffffffff01100218fdffffffffffffffff0120012800308180023801"));
    }
    void gates() {
        auto c = permittedFixture();
        const Command motion = KeyboardMouseCommand{};
        QVERIFY(commandBlock(motion, c).isEmpty());
        for (auto mode : {TerminalMode::Simulation, TerminalMode::Replay}) {
            c.terminal.mode = mode; QVERIFY(!commandBlock(motion, c).isEmpty());
        }
        c = permittedFixture(); c.terminal.uiOccupied = true; QVERIFY(!commandBlock(motion, c).isEmpty());
        c = permittedFixture(); c.terminal.referee.fresh = false; QVERIFY(!commandBlock(motion, c).isEmpty());
        c = permittedFixture(); c.terminal.controlsArmed = false; QVERIFY(!commandBlock(motion, c).isEmpty());
        c = permittedFixture(); c.terminal.phase = MatchPhase::Countdown; QVERIFY(!commandBlock(RuneCommand{1}, c).isEmpty());
        c = permittedFixture(); c.reportedId = 103; QCOMPARE(commandBlock(motion, c), QString("identity_mismatch"));
        c = permittedFixture(); c.identityFresh = false; QCOMPARE(commandBlock(motion, c), QString("identity_unknown"));
        c = permittedFixture(); c.businessPolicyReady = false; QCOMPARE(commandBlock(motion, c), QString("business_policy_unavailable"));
        c = permittedFixture(); c.keyboardSourceSelected = false; QCOMPARE(commandBlock(motion, c), QString("keyboard_source_unconfirmed"));
        c = permittedFixture(); c.mapCalibrated = false; QCOMPARE(commandBlock(MapClickCommand{}, c), QString("map_uncalibrated"));
    }
    void businessRules() {
        const auto now = std::chrono::steady_clock::now();
        auto c = permittedFixture();
        Snapshot s;
        SelfConfig robot; robot.robotId = 3; robot.connectionState = 1;
        robot.fieldState = 0; robot.aliveState = 1; robot.level = 1;
        s.selfConfig = {robot, now};
        Game game; game.phase = Phase::Playing; game.paused = false; game.countdownSec = 420;
        s.game = {game, now}; s.rune = {RuneStatus{1}, now};
        const auto reason = [&](const Command &command) {
            return QString::fromLatin1(businessCommandBlock(command, s, c, now));
        };
        const auto role = [&](unsigned id) {
            s.selfConfig->value.robotId = id; c.requestedId = c.reportedId = id;
        };
        QVERIFY(reason(RuneCommand{}).isEmpty());
        s.selfConfig->value.connectionState.reset();
        QCOMPARE(reason(RuneCommand{}), QString("robot_connection_unconfirmed"));
        s.selfConfig->value.connectionState = 1;
        s.selfConfig->value.fieldState = 1;
        QCOMPARE(reason(RuneCommand{}), QString("robot_not_on_field"));
        s.selfConfig->value.fieldState = 0;
        s.rune->at = now - std::chrono::milliseconds(1500);
        QCOMPARE(reason(RuneCommand{}), QString("rune_not_inactive"));
        s.rune->at = now + std::chrono::milliseconds(1);
        QCOMPARE(reason(RuneCommand{}), QString("rune_not_inactive"));
        s.rune->at = now; s.rune->value.code = 0;
        QCOMPARE(reason(RuneCommand{}), QString("rune_not_inactive"));
        s.rune->value.code = 1;
        s.penalty = {Penalty{}, now};
        QCOMPARE(reason(RuneCommand{}), QString("penalty_release_unverified"));
        s.penalty->value.effectSec = 0; s.penalty->at = now - std::chrono::seconds(2);
        QCOMPARE(reason(RuneCommand{}), QString("penalty_release_unverified"));
        s.penalty.reset(); // Test setup only; not a live penalty-clear implementation.
        MapClickCommand map; map.robotIds[0] = 103;
        QCOMPARE(reason(map), QString("map_recipient_wrong_team"));
        map.robotIds[0] = map.robotIds[1] = 3;
        QCOMPARE(reason(map), QString("map_duplicate_recipient"));
        map.robotIds = {}; QCOMPARE(reason(map), QString("map_recipient_missing"));
        map.sendAll = 1; map.markingMode = 2; map.enemyId = 3;
        QCOMPARE(reason(map), QString("map_target_not_enemy"));
        map.enemyId = 103; QVERIFY(reason(map).isEmpty());
        QVERIFY(reason(KeyboardMouseCommand{}).isEmpty());
        QVERIFY(reason(CustomControlCommand{}).isEmpty());
        s.economy = {Economy{9}, now};
        QCOMPARE(reason(CommonCommand{1, 10}), QString("insufficient_gold"));
        s.economy->value.remaining = 10;
        QCOMPARE(reason(CommonCommand{1, 10}), QString("exchange_area_and_team_quota_unverified"));
        role(1); s.economy->value.remaining = std::numeric_limits<std::uint32_t>::max();
        QCOMPARE(reason(CommonCommand{2, std::numeric_limits<std::uint32_t>::max()}), QString("ammo_quantity_exceeds_team_limit"));
        QCOMPARE(reason(CommonCommand{2, 101}), QString("ammo_quantity_exceeds_team_limit"));
        s.economy->value.remaining = 1000;
        QCOMPARE(reason(CommonCommand{2, 100}), QString("exchange_area_and_team_quota_unverified"));
        s.economy->value.remaining = 999;
        QCOMPARE(reason(CommonCommand{2, 100}), QString("insufficient_gold"));
        role(3); s.selfDynamic = {SelfDynamic{}, now};
        s.economy->value.remaining = 1000;
        QCOMPARE(reason(CommonCommand{1, 1000}), QString("exchange_area_and_team_quota_unverified"));
        QCOMPARE(reason(CommonCommand{1, 1010}), QString("ammo_quantity_exceeds_team_limit"));
        QCOMPARE(reason(CommonCommand{6, 100}), QString("out_of_combat_unconfirmed"));
        s.selfDynamic->value.outOfCombat = true; s.selfDynamic->value.canRemoteHeal = false;
        QCOMPARE(reason(CommonCommand{6, 100}), QString("remote_exchange_not_allowed"));
        s.selfDynamic->value.canRemoteHeal = true;
        QCOMPARE(reason(CommonCommand{6, 100}), QString("remote_exchange_parameter_unverified"));
        // Sufficient funds are necessary, never proof of parameter semantics or quota.
        s.selfDynamic->value.canRemoteAmmo = true;
        s.economy->value.remaining = 149;
        QCOMPARE(reason(CommonCommand{5, 100}), QString("insufficient_gold"));
        s.economy->value.remaining = 150;
        QCOMPARE(reason(CommonCommand{5, 100}), QString("remote_exchange_parameter_unverified"));
        for (const auto &[remaining, cost] : std::array<std::pair<int, unsigned>, 8>{{
            {420, 50}, {419, 51}, {417, 51}, {416, 52}, {360, 70}, {359, 71}, {1, 190}, {0, 190}}}) {
            s.game->value.countdownSec = remaining; s.economy->value.remaining = cost - 1;
            QCOMPARE(reason(CommonCommand{6, 60}), QString("insufficient_gold"));
            s.economy->value.remaining = cost;
            QCOMPARE(reason(CommonCommand{6, 60}), QString("remote_exchange_parameter_unverified"));
        }
        for (auto invalidTime : {std::optional<std::int32_t>{}, std::optional<std::int32_t>{-1}, std::optional<std::int32_t>{421}}) {
            s.game->value.countdownSec = invalidTime;
            QCOMPARE(reason(CommonCommand{6, 60}), QString("remote_heal_cost_unknown"));
        }
        s.game->value.countdownSec = 420;
        s.economy->at = now - std::chrono::milliseconds(1500);
        QCOMPARE(reason(CommonCommand{5, 100}), QString("economy_unknown"));
        QCOMPARE(reason(CommonCommand{6, 60}), QString("economy_unknown"));
        s.economy->at = now;
        s.selfConfig->value.aliveState = 2; s.respawn = {Respawn{}, now};
        s.respawn->value.pending = true; s.respawn->value.currentProgress = 100;
        s.respawn->value.requiredProgress = 100;
        QCOMPARE(reason(CommonCommand{3, 0}), QString("free_respawn_not_allowed")); // Progress is not permission.
        s.respawn->value.reportedCanFree = true;
        QVERIFY(reason(CommonCommand{3, 0}).isEmpty());
        s.respawn->value.reportedCanPay = true; s.respawn->value.goldCost = 0;
        QCOMPARE(reason(CommonCommand{4, 0}), QString("respawn_cost_unconfirmed"));
        s.respawn->value.goldCost = 80; s.economy->value.remaining = 79;
        QCOMPARE(reason(CommonCommand{4, 0}), QString("insufficient_gold"));
        s.economy->value.remaining = 80; QVERIFY(reason(CommonCommand{4, 0}).isEmpty());
        s.respawn->at = now - std::chrono::milliseconds(1500);
        QCOMPARE(reason(CommonCommand{3, 0}), QString("respawn_pending_unconfirmed"));
        s.game->value.phase = Phase::Preparation;
        QVERIFY(reason(PerformanceCommand{1, 2, {}}).isEmpty()); // No life/on-field requirement for selection.
        QCOMPARE(reason(PerformanceCommand{3, {}, {}}), QString("infantry_performance_out_of_range"));
        QCOMPARE(reason(PerformanceCommand{{}, {}, 0}), QString("sentry_operator_unverified"));
        role(1);
        QVERIFY(reason(PerformanceCommand{4, 4, {}}).isEmpty());
        QCOMPARE(reason(PerformanceCommand{3, 4, {}}), QString("hero_requires_whole_robot_performance"));
        s.game->value.phase = Phase::SelfCheck; QVERIFY(reason(PerformanceCommand{3, 3, {}}).isEmpty());
        s.game->value.phase = Phase::Countdown;
        QCOMPARE(reason(PerformanceCommand{3, 3, {}}), QString("current_game_unavailable_or_paused"));
        s.game->value.phase = Phase::Playing;
        s.deployment = {Deployment{1, DeployState::Deployed}, now};
        QVERIFY(reason(DeployCommand{0}).isEmpty()); // Dead deployed hero can request exit.
        s.selfConfig->value.aliveState = 1; s.selfConfig->value.shooter = s.selfConfig->value.chassis = 4;
        QCOMPARE(reason(DeployCommand{1}), QString("deployment_not_confirmed_inactive"));
        s.deployment->value = Deployment{};
        QCOMPARE(reason(DeployCommand{1}), QString("deployment_not_confirmed_inactive"));
        s.deployment->value = Deployment{0, DeployState::NotDeployed};
        QCOMPARE(reason(DeployCommand{1}), QString("deployment_area_or_cooldown_unverified"));
        role(2); s.techCore = {TechCore{4, 1, 0, 0, 0}, now};
        QVERIFY(reason(AssemblyCommand{0, 4}).isEmpty());
        QCOMPARE(reason(AssemblyCommand{0, 5}), QString("assembly_difficulty_unavailable"));
        s.techCore->value.basicState = 3; s.techCore->value.putIn = 1;
        QCOMPARE(reason(AssemblyCommand{1, 3}), QString("assembly_steps_incomplete"));
        s.techCore->value.move = s.techCore->value.rotate = 1;
        QVERIFY(reason(AssemblyCommand{1, 3}).isEmpty());
        s.selfConfig->value.aliveState = 2; QVERIFY(reason(AssemblyCommand{2, 0}).isEmpty());
        QCOMPARE(reason(AssemblyCommand{1, 3}), QString("robot_life_unconfirmed"));
        role(6); s.selfConfig->value.aliveState = 1; c.mapOperator = MapOperator::Gimbal;
        s.airSupport = {AirSupport{0, 1, 0, 0, 2}, now};
        QVERIFY(reason(AirSupportCommand{1}).isEmpty());
        s.airSupport->value.freeTime = 0; s.economy->value.remaining = 0;
        QCOMPARE(reason(AirSupportCommand{1}), QString("air_support_free_time_exhausted"));
        QCOMPARE(reason(AirSupportCommand{2}), QString("insufficient_gold"));
        s.economy->value.remaining = 1; QVERIFY(reason(AirSupportCommand{2}).isEmpty());
        for (const auto &[remaining, cost] : std::array<std::pair<int, unsigned>, 4>{{{420, 40}, {419, 200}, {360, 200}, {359, 360}}}) {
            s.game->value.countdownSec = remaining; s.economy->value.remaining = cost - 1;
            QCOMPARE(reason(AirSupportCommand{3}), QString("insufficient_gold"));
            s.economy->value.remaining = cost; QVERIFY(reason(AirSupportCommand{3}).isEmpty());
        }
        s.airSupport->value.shooterStatus = 1;
        QCOMPARE(reason(AirSupportCommand{3}), QString("air_counter_not_releasable"));
        s.airSupport->value.status = 1; s.selfConfig->value.aliveState = 2;
        QVERIFY(reason(AirSupportCommand{0}).isEmpty());
        s.selfConfig->value.aliveState = 1;
        QCOMPARE(reason(SentryCommand{1}), QString("sentry_operator_unverified"));
        QCOMPARE(reason(DartCommand{1}), QString("dart_operator_unverified"));
    }
    void reportMapping() {
        GameData data;
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        for (const auto &[topic, hex] : std::array<std::pair<const char *, const char *>, 15>{{
            {"RobotStaticStatus", "080110001801200330003800280050005d00000000600068007000"},
            {"RobotDynamicStatus", "500060006800200028003000380040005800"},
            {"GlobalUnitStatus", "620a0000000000000000000068007000"},
            {"GlobalLogisticsStatus", "0800100018002000"},
            {"TechCoreMotionStateSync", "08041001180020002800300038004000"},
            {"RuneStatusSync", "080010001d00000000"},
            {"SentryStatusSync", "080010001800"}, {"DartSelectTargetStatusSync", "08001000"},
            {"AirSupportStatusSync", "08001000180020002800"},
            {"SentryCtrlResult", "080c1000"},
            {"GlobalSpecialMechanism", "0a0101120100"},
            {"RobotInjuryStat", "0800100018002000280030003800400048005000"},
            {"RobotPathPlanInfo", "0800100018003000"},
            {"MapClickInfo", "080012070000000000000018002000280030003d000000004500000000"},
            {"CustomByteBlock", "0a00"}}})
            QVERIFY(data.acceptOfficialReport(topic, QByteArray::fromHex(hex), ms));
        auto snapshot = data.officialSnapshot();
        QVERIFY(snapshot.selfConfig && snapshot.selfDynamic && snapshot.techCore
            && snapshot.rune && snapshot.sentryStatus && snapshot.dartStatus
            && snapshot.airSupport && snapshot.sentryResult && snapshot.specialMechanisms && snapshot.injury
            && snapshot.pathPlan && snapshot.mapClick && snapshot.customBytes);
        QCOMPARE(snapshot.selfConfig->value.fieldState, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.selfDynamic->value.canRemoteHeal, std::optional<bool>(false));
        QCOMPARE(snapshot.selfConfig->value.heatCooldownRate, std::optional<float>(0));
        QCOMPARE(snapshot.selfDynamic->value.experienceForUpgrade, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.units->value.robotBullets, std::vector<std::int32_t>(10, 0));
        QCOMPARE(snapshot.economy->value.totalObtained, std::optional<std::uint64_t>(0));
        QCOMPARE(snapshot.techCore->value.putIn, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.techCore->value.enemyCoreStatus, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.techCore->value.remainTimeAll, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.techCore->value.remainTimeStep, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.rune->value.code, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.rune->value.activatedArms, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.rune->value.averageRings, std::optional<float>(0));
        QCOMPARE(snapshot.sentryStatus->value.posture, SentryPosture::Unknown);
        QCOMPARE(snapshot.sentryStatus->value.weakened, std::optional<bool>(false));
        QCOMPARE(snapshot.sentryStatus->value.powered, std::optional<bool>(false));
        QCOMPARE(snapshot.dartStatus->value.targetId, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.dartStatus->value.target, DartTarget::Unknown);
        QCOMPARE(snapshot.dartStatus->value.gate, DartGate::Closed);
        QCOMPARE(snapshot.airSupport->value.freeTime, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.sentryResult->value.resultCode, std::optional<std::uint32_t>(0));
        auto json = snapshotJson(snapshot);
        QCOMPARE(json["economy"].toObject()["total_obtained"], QJsonValue("0"));
        QVERIFY(json["self_dynamic"].toObject()["can_remote_heal"].isBool());
        QVERIFY(json["sentry_status"].toObject()["powered"].isBool());
        QCOMPARE(json["dart_status"].toObject()["gate_code"].toInt(-1), 0);
        QCOMPARE(json["sentry_result_report"].toObject()["result_code"].toInt(-1), 0);
        QCOMPARE(json["tech_core"].toObject()["enemy_core_status"].toInt(-1), 0);
        QCOMPARE(json["tech_core"].toObject()["remain_time_all"].toInt(-1), 0);
        QCOMPARE(json["tech_core"].toObject()["remain_time_step"].toInt(-1), 0);
        QCOMPARE(snapshot.specialMechanisms->value.entries.front().timeSec, 0);
        const auto injury = json["injury"].toObject();
        QCOMPARE(injury.size(), 10);
        for (auto i = injury.begin(); i != injury.end(); ++i) QCOMPARE(i.value(), QJsonValue(0));
        QCOMPARE(snapshot.pathPlan->value.startXdm, std::optional<std::uint32_t>(0));
        QCOMPARE(snapshot.pathPlan->value.senderId, std::optional<std::uint32_t>(0));
        QVERIFY(snapshot.pathPlan->value.offsets.empty());
        QVERIFY(snapshot.mapClick->value.robotIds);
        for (const auto id : *snapshot.mapClick->value.robotIds) QCOMPARE(id, std::uint8_t(0));
        QCOMPARE(json["map_click_report"].toObject()["x"], QJsonValue(0));
        QCOMPARE(json["map_click_report"].toObject()["y"], QJsonValue(0));
        QVERIFY(snapshot.customBytes->value.data && snapshot.customBytes->value.data->empty());
        QCOMPARE(json["custom_bytes"].toObject(), QJsonObject({{"data_hex", ""}}));
        QCOMPARE(QJsonObject::fromVariantMap(data.officialReports())["CustomByteBlock"].toObject()["fields"].toObject()["data"],
            QJsonValue(QString("")));
        QVERIFY(data.acceptOfficialReport("SentryCtrlResult", QByteArray::fromHex("0801"), ms));
        QVERIFY(!data.officialSnapshot().sentryResult->value.resultCode);
        QVERIFY(!snapshotJson(data.officialSnapshot())["sentry_result_report"].toObject().contains("result_code"));
        QVERIFY(!data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms + 2000));
        QVERIFY(!data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms - 1500));
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("2067"), ms));
        snapshot = data.officialSnapshot();
        QVERIFY(!snapshot.techCore && !snapshot.rune && !snapshot.sentryStatus && !snapshot.dartStatus
            && !snapshot.airSupport && !snapshot.sentryResult && !snapshot.specialMechanisms && !snapshot.injury
            && !snapshot.pathPlan && !snapshot.mapClick && !snapshot.customBytes
            && !snapshot.selfDynamic && !snapshot.units && !snapshot.economy);
        QVERIFY(!snapshot.selfConfig->value.connectionState); // Absent is not connected.
    }
    void completeCoreReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex(
            "200328ff0150ffffffff0f5d0000a03f60ffffffff0f680070ffffffff0f"), ms()));
        auto json = snapshotJson(data.officialSnapshot());
        QCOMPARE(json["self_config"].toObject(), QJsonObject({{"team", "red"}, {"robot_id", 3},
            {"robot_type", 255}, {"max_heat", 4294967295.0}, {"heat_cooldown_rate", 1.25},
            {"max_power", 4294967295.0}, {"max_buffer_energy", 0}, {"max_chassis_energy", 4294967295.0}}));
        for (const char *hex : {"20675d0000c07f", "5d0000807f", "5d000080ff", "5d00"}) {
            QVERIFY(!data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex(hex), ms()));
            QCOMPARE(snapshotJson(data.officialSnapshot())["self_config"], json["self_config"]);
        }
        QVERIFY(data.acceptOfficialReport("RobotDynamicStatus", QByteArray::fromHex(
            "20ffffffff0f280030ffffffff0f380040ffffffff0f58ffffffff0f"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["self_dynamic"].toObject(), QJsonObject({
            {"chassis_energy", 4294967295.0}, {"buffer_energy", 0}, {"experience", 4294967295.0},
            {"experience_for_upgrade", 0}, {"total_projectiles_fired", 4294967295.0},
            {"out_of_combat_countdown", 4294967295.0}}));
        QVERIFY(!data.officialSnapshot().selfDynamic->value.outOfCombat);
        const auto unit = QByteArray::fromHex(
            "622080808080f8ffffffff01ffffffffffffffffff0100ffffffff0701020304050668ffffffff0f7000");
        QVERIFY(data.acceptOfficialReport("GlobalUnitStatus", unit, ms()));
        QCOMPARE(data.officialSnapshot().units->value.robotBullets,
            (std::vector<std::int32_t>{std::numeric_limits<std::int32_t>::min(), -1, 0,
                std::numeric_limits<std::int32_t>::max(), 1,2,3,4,5,6}));
        QCOMPARE(snapshotJson(data.officialSnapshot())["units"].toObject()["total_damage_ally"].toDouble(), 4294967295.0);
        QCOMPARE(data.officialSnapshot().units->value.totalDamageEnemy, std::optional<std::uint32_t>(0));
        for (const auto size : {9, 11}) {
            const auto bad = QByteArray(1, char(0x62)) + QByteArray(1, char(size)) + QByteArray(size, char(0));
            QVERIFY(!data.acceptOfficialReport("GlobalUnitStatus", bad, ms()));
            QCOMPARE(data.officialSnapshot().units->value.robotBullets.size(), size_t(10));
        }
        for (const auto& side : {data.officialSnapshot().units->value.ally, data.officialSnapshot().units->value.enemy})
            for (const auto& robot : side.robots) QVERIFY(!robot.health); // No ammo-to-health/role inference.
        for (const auto &[hex, expected] : std::array<std::pair<const char*, const char*>, 3>{{
                {"1000", "0"}, {"108180808080808010", "9007199254740993"},
                {"10ffffffffffffffffff01", "18446744073709551615"}}}) {
            QVERIFY(data.acceptOfficialReport("GlobalLogisticsStatus", QByteArray::fromHex(hex)
                + QByteArray::fromHex("18ffffffff0f2000"), ms()));
            const auto economy = snapshotJson(data.officialSnapshot())["economy"].toObject();
            QCOMPARE(economy, QJsonObject({{"total_obtained", expected}, {"tech_level", 4294967295.0}, {"encryption_level", 0}}));
        }
        QCOMPARE(data.officialSnapshot().economy->value.totalObtained,
            std::optional<std::uint64_t>(std::numeric_limits<std::uint64_t>::max()));
        json = snapshotJson(data.officialSnapshot());
        for (const auto& [topic, hex] : std::array<std::pair<const char*, const char*>, 4>{{
                {"RobotStaticStatus", "5000"}, {"RobotDynamicStatus", "2800"},
                {"GlobalUnitStatus", "7000"}, {"GlobalLogisticsStatus", "2000"}}}) {
            for (const auto offset : {-1600, 2000})
                QVERIFY(!data.acceptOfficialReport(topic, QByteArray::fromHex(hex), ms() + offset));
            QVERIFY(!data.acceptOfficialReport(topic, QByteArray::fromHex("80"), ms()));
        }
        QCOMPARE(snapshotJson(data.officialSnapshot()), json);
        QVERIFY(data.officialSnapshot().recentEvents.empty());
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("5000"), ms() - 1400));
        QVERIFY(data.acceptOfficialReport("RobotDynamicStatus", QByteArray::fromHex("5800"), ms() - 1400));
        QVERIFY(data.acceptOfficialReport("GlobalUnitStatus", QByteArray::fromHex("7000"), ms() - 1400));
        QVERIFY(data.acceptOfficialReport("GlobalLogisticsStatus", QByteArray::fromHex("2000"), ms() - 1400));
        json = snapshotJson(data.officialSnapshot());
        QCOMPARE(json["self_config"].toObject(), QJsonObject({{"team", "unknown"}, {"max_heat", 0}}));
        QCOMPARE(json["self_dynamic"].toObject(), QJsonObject({{"out_of_combat_countdown", 0}}));
        QVERIFY(!json["units"].toObject().contains("robot_bullets"));
        QVERIFY(!json["units"].toObject().contains("total_damage_ally"));
        QCOMPARE(json["economy"].toObject(), QJsonObject({{"encryption_level", 0}}));
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms()));
        QTest::qWait(180);
        const auto expired = data.officialSnapshot();
        QVERIFY(!expired.selfConfig && !expired.selfDynamic && !expired.units && !expired.economy && expired.rune);
    }
    void customByteReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("2003"), ms()));
        QVERIFY(data.acceptOfficialReport("RobotDynamicStatus", QByteArray::fromHex("0864"), ms()));
        const auto bytes = QByteArray::fromHex("0aac020000000165ff") + QByteArray(291, char(0));
        QVERIFY(data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0aac02") + bytes, ms()));
        const auto payload = *data.officialSnapshot().customBytes->value.data;
        QCOMPARE(payload.size(), size_t(300));
        QCOMPARE(payload[0], std::uint8_t(10)); QCOMPARE(payload[1], std::uint8_t(172));
        QCOMPARE(payload[3], std::uint8_t(0)); QCOMPARE(payload[8], std::uint8_t(255));
        // Embedded protobuf/video-looking prefixes are application bytes, never stripped or decoded.
        const QJsonValue previous = snapshotJson(data.officialSnapshot())["custom_bytes"];
        QCOMPARE(previous.toObject()["data_hex"].toString(), QString::fromLatin1(bytes.toHex()));
        for (const auto invalid : {QByteArray(), QByteArray::fromHex("980601"), QByteArray::fromHex("0a"),
                QByteArray::fromHex("0a0200"), QByteArray::fromHex("0000000165"),
                QByteArray::fromHex("0aad02") + QByteArray(301, char(0)),
                QByteArray::fromHex("0aaf02") + QByteArray::fromHex("0aac02") + QByteArray(300, char(0))}) {
            QVERIFY(!data.acceptOfficialReport("CustomByteBlock", invalid, ms()));
            QCOMPARE(snapshotJson(data.officialSnapshot())["custom_bytes"], previous);
        }
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0a00"), ms() + offset));
        QCOMPARE(snapshotJson(data.officialSnapshot())["custom_bytes"], previous);
        QVERIFY(data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0a1c") + QByteArray(28, char(255)), ms()));
        QCOMPARE(data.officialSnapshot().selfConfig->value.robotId, std::optional<std::uint32_t>(3));
        QCOMPARE(data.officialSnapshot().selfDynamic->value.health, std::optional<std::uint32_t>(100));
        QVERIFY(data.officialSnapshot().recentEvents.empty());
        QVERIFY(data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0a0200ff"), ms()));
        QCOMPARE(*data.officialSnapshot().customBytes->value.data, (std::vector<std::uint8_t>{0,255}));
        QVERIFY(data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0a00"), ms() - 1400));
        QCOMPARE(snapshotJson(data.officialSnapshot())["custom_bytes"].toObject(), QJsonObject({{"data_hex", ""}}));
        QVERIFY(data.officialSnapshot().customBytes->value.data);
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms()));
        QTest::qWait(180);
        QVERIFY(!data.officialSnapshot().customBytes && data.officialSnapshot().rune);
        QVERIFY(data.acceptOfficialReport("CustomByteBlock", QByteArray::fromHex("0a00"), ms()));
        data.resetOfficialReports();
        QVERIFY(!data.officialSnapshot().customBytes);
    }
    void pathMapReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        // Raw decimeters and signed offsets must survive without guessing normalized coordinates.
        const auto path = QByteArray::fromHex(
            "08ff01100118ffffffff0f220b80ffffffffffffffff017f2a0b7f80ffffffffffffffff0130ffffffff0f");
        QVERIFY(data.acceptOfficialReport("RobotPathPlanInfo", path, ms()));
        const auto value = data.officialSnapshot().pathPlan->value;
        QCOMPARE(value.intention, std::optional<std::uint32_t>(255));
        QCOMPARE(value.startXdm, std::optional<std::uint32_t>(1));
        QCOMPARE(value.startYdm, std::optional<std::uint32_t>(4294967295u));
        QCOMPARE(value.senderId, std::optional<std::uint32_t>(4294967295u));
        QCOMPARE(value.offsets.size(), size_t(2));
        QCOMPARE(value.offsets[0].x, -128); QCOMPARE(value.offsets[0].y, 127);
        QCOMPARE(value.offsets[1].x, 127); QCOMPARE(value.offsets[1].y, -128);
        const QJsonValue previous = snapshotJson(data.officialSnapshot())["path_plan"];
        QCOMPARE(previous.toObject()["start_y_dm"].toDouble(), 4294967295.0);
        for (const char *hex : {"", "22", "220100", "2a0100", "220280012a0100",
                "220afffeffffffffffffff012a0100", "980601"}) {
            QVERIFY(!data.acceptOfficialReport("RobotPathPlanInfo", QByteArray::fromHex(hex), ms()));
            QCOMPARE(snapshotJson(data.officialSnapshot())["path_plan"], previous);
        }
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("RobotPathPlanInfo", QByteArray::fromHex("0800"), ms() + offset));
        QCOMPARE(snapshotJson(data.officialSnapshot())["path_plan"], previous);
        const auto fullPath = QByteArray::fromHex("2231") + QByteArray(49, char(0))
            + QByteArray::fromHex("2a31") + QByteArray(49, char(0));
        QVERIFY(data.acceptOfficialReport("RobotPathPlanInfo", fullPath, ms()));
        QCOMPARE(data.officialSnapshot().pathPlan->value.offsets.size(), size_t(49));
        QVERIFY(!data.officialSnapshot().pathPlan->value.startXdm);
        const auto tooLong = QByteArray::fromHex("2232") + QByteArray(50, char(0))
            + QByteArray::fromHex("2a32") + QByteArray(50, char(0));
        QVERIFY(!data.acceptOfficialReport("RobotPathPlanInfo", tooLong, ms()));
        QCOMPARE(data.officialSnapshot().pathPlan->value.offsets.size(), size_t(49));
        QVERIFY(data.acceptOfficialReport("RobotPathPlanInfo", QByteArray::fromHex("0800"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["path_plan"].toObject(),
            QJsonObject({{"intention", 0}, {"offsets", QJsonArray()}}));
        // Field 3 is marking type, field 6 marking mode; IDs are recipients, not a sender/self ID.
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("2003"), ms()));
        const auto map = QByteArray::fromHex("08021207656603ff000000180420ff01285a30013d0000a03f45000020c0");
        QVERIFY(data.acceptOfficialReport("MapClickInfo", map, ms()));
        const auto expected = QJsonObject({{"send_all", 2}, {"robot_ids", QJsonArray{101,102,3,255,0,0,0}},
            {"marking_type", 4}, {"enemy_id", 255}, {"ascii", 90}, {"marking_mode", 1}, {"x", 1.25}, {"y", -2.5}});
        QCOMPARE(snapshotJson(data.officialSnapshot())["map_click_report"].toObject(), expected);
        QCOMPARE(data.officialSnapshot().selfConfig->value.robotId, std::optional<std::uint32_t>(3));
        QVERIFY(data.officialSnapshot().recentEvents.empty());
        for (const char *hex : {"", "1200", "1206000000000000", "12080000000000000000", "3d0000c07f",
                "450000807f", "3d000080ff", "45", "980601"}) {
            QVERIFY(!data.acceptOfficialReport("MapClickInfo", QByteArray::fromHex(hex), ms()));
            QCOMPARE(snapshotJson(data.officialSnapshot())["map_click_report"].toObject(), expected);
        }
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("MapClickInfo", QByteArray::fromHex("0800"), ms() + offset));
        QCOMPARE(snapshotJson(data.officialSnapshot())["map_click_report"].toObject(), expected);
        QVERIFY(data.acceptOfficialReport("MapClickInfo", QByteArray::fromHex("08ffffffff0f18ffffffff0f30ffffffff0f"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["map_click_report"].toObject(), QJsonObject({
            {"send_all", 4294967295.0}, {"marking_type", 4294967295.0}, {"marking_mode", 4294967295.0}}));
        QVERIFY(data.acceptOfficialReport("MapClickInfo", QByteArray::fromHex("3d00000000"), ms() - 1400));
        QCOMPARE(snapshotJson(data.officialSnapshot())["map_click_report"].toObject(), QJsonObject({{"x", 0}}));
        QVERIFY(!data.officialSnapshot().mapClick->value.robotIds);
        QVERIFY(data.acceptOfficialReport("RobotPathPlanInfo", QByteArray::fromHex("3000"), ms() - 1400));
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms()));
        QTest::qWait(180);
        QVERIFY(!data.officialSnapshot().pathPlan && !data.officialSnapshot().mapClick && data.officialSnapshot().rune);
        QVERIFY(data.acceptOfficialReport("RobotPathPlanInfo", path, ms()));
        QVERIFY(data.acceptOfficialReport("MapClickInfo", map, ms()));
        data.resetOfficialReports();
        QVERIFY(!data.officialSnapshot().pathPlan && !data.officialSnapshot().mapClick);
    }
    void mechanismInjuryReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        // Independent wire sample: paired arrays preserve signed bounds, order, duplicates and unknown IDs.
        const auto mechanisms = QByteArray::fromHex(
            "0a0b0102ff010100ffffffff0f121cffffffffffffffffff010080808080f8ffffffff01ffffffff070001");
        QVERIFY(data.acceptOfficialReport("GlobalSpecialMechanism", mechanisms, ms()));
        const auto entries = data.officialSnapshot().specialMechanisms->value.entries;
        QCOMPARE(entries.size(), size_t(6));
        QCOMPARE(entries[0].kind, MechanismKind::AllyFortressOccupied);
        QCOMPARE(entries[1].kind, MechanismKind::EnemyFortressOccupied);
        QCOMPARE(entries[0].timeSec, -1);
        QCOMPARE(entries[1].timeSec, 0);
        QCOMPARE(entries[2].timeSec, std::numeric_limits<std::int32_t>::min());
        QCOMPARE(entries[3].timeSec, std::numeric_limits<std::int32_t>::max());
        QCOMPARE(entries[3].id, 1u);
        QCOMPARE(entries[4].id, 0u);
        QCOMPARE(entries[5].id, 4294967295u);
        for (const auto index : {2, 4, 5}) QCOMPARE(entries[index].kind, MechanismKind::Unknown);
        const QJsonValue previous = snapshotJson(data.officialSnapshot())["special_mechanisms"];
        for (const char *hex : {"0a0101", "120100", "0a", "980601"}) {
            QVERIFY(!data.acceptOfficialReport("GlobalSpecialMechanism", QByteArray::fromHex(hex), ms()));
            QCOMPARE(snapshotJson(data.officialSnapshot())["special_mechanisms"], previous);
        }
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("GlobalSpecialMechanism", {}, ms() + offset));
        QCOMPARE(snapshotJson(data.officialSnapshot())["special_mechanisms"], previous);
        for (const auto empty : {QByteArray(), QByteArray::fromHex("0a001200")}) {
            QVERIFY(data.acceptOfficialReport("GlobalSpecialMechanism", empty, ms()));
            QVERIFY(data.officialSnapshot().specialMechanisms->value.entries.empty());
            QCOMPARE(snapshotJson(data.officialSnapshot())["special_mechanisms"].toObject()["entries"].toArray().size(), 0);
        }
        QVERIFY(!data.acceptOfficialReport("RobotInjuryStat", {}, ms()));
        QVERIFY(!data.officialSnapshot().injury);
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("2003"), ms()));
        QVERIFY(data.acceptOfficialReport("RobotDynamicStatus", QByteArray::fromHex("0864"), ms()));
        QVERIFY(data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("080110021803200428053006380740084809500a"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["injury"].toObject(), QJsonObject({
            {"total_damage", 1}, {"collision_damage", 2}, {"small_projectile_damage", 3},
            {"large_projectile_damage", 4}, {"dart_splash_damage", 5}, {"module_offline_damage", 6},
            {"offline_damage", 7}, {"penalty_damage", 8}, {"server_kill_damage", 9}, {"killer_id", 10}}));
        QCOMPARE(data.officialSnapshot().selfConfig->value.robotId, std::optional<std::uint32_t>(3));
        QCOMPARE(data.officialSnapshot().selfDynamic->value.health, std::optional<std::uint32_t>(100));
        QVERIFY(data.officialSnapshot().recentEvents.empty());
        QVERIFY(data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("08ffffffff0f50ffffffff0f"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["injury"].toObject(),
            QJsonObject({{"total_damage", 4294967295.0}, {"killer_id", 4294967295.0}}));
        QVERIFY(data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("5000"), ms()));
        const auto zeroKiller = QJsonObject({{"killer_id", 0}});
        QCOMPARE(snapshotJson(data.officialSnapshot())["injury"].toObject(), zeroKiller);
        for (const char *hex : {"", "08", "980601"})
            QVERIFY(!data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex(hex), ms()));
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("0800"), ms() + offset));
        QCOMPARE(snapshotJson(data.officialSnapshot())["injury"].toObject(), zeroKiller);
        QVERIFY(data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("0800"), ms() - 1400));
        QCOMPARE(snapshotJson(data.officialSnapshot())["injury"].toObject(), QJsonObject({{"total_damage", 0}}));
        QVERIFY(data.acceptOfficialReport("GlobalSpecialMechanism", {}, ms() - 1400));
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms()));
        QTest::qWait(180);
        QVERIFY(!data.officialSnapshot().injury && !data.officialSnapshot().specialMechanisms && data.officialSnapshot().rune);
        QVERIFY(data.acceptOfficialReport("RobotInjuryStat", QByteArray::fromHex("5000"), ms()));
        QVERIFY(data.acceptOfficialReport("GlobalSpecialMechanism", mechanisms, ms()));
        data.resetOfficialReports();
        QVERIFY(!data.officialSnapshot().injury && !data.officialSnapshot().specialMechanisms);
    }
    void techCoreReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        for (unsigned code : {0u, 1u, 2u, 255u}) {
            const auto enemy = code == 255 ? QByteArray::fromHex("30ff01")
                : QByteArray::fromHex("30") + QByteArray(1, char(code));
            const auto sample = QByteArray::fromHex("08041003180120012801") + enemy
                + QByteArray::fromHex("38ffffffff0f4000");
            QVERIFY(data.acceptOfficialReport("TechCoreMotionStateSync", sample, ms()));
            const auto s = data.officialSnapshot().techCore->value;
            QCOMPARE(s.enemyCoreStatus, std::optional<std::uint32_t>(code));
            QCOMPARE(s.remainTimeAll, std::optional<std::uint32_t>(4294967295u));
            QCOMPARE(s.remainTimeStep, std::optional<std::uint32_t>(0));
            QCOMPARE(snapshotJson(data.officialSnapshot())["tech_core"].toObject()["remain_time_all"].toDouble(),
                4294967295.0);
        }
        // A report with only one field replaces all previous fields, not just that field.
        QVERIFY(data.acceptOfficialReport("TechCoreMotionStateSync", QByteArray::fromHex("4000"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["tech_core"].toObject(),
            QJsonObject({{"remain_time_step", 0}}));
        for (const auto offset : {-1600, 2000})
            QVERIFY(!data.acceptOfficialReport("TechCoreMotionStateSync", QByteArray::fromHex("3801"), ms() + offset));
        QVERIFY(!data.acceptOfficialReport("TechCoreMotionStateSync", QByteArray::fromHex("38"), ms()));
        QCOMPARE(snapshotJson(data.officialSnapshot())["tech_core"].toObject(),
            QJsonObject({{"remain_time_step", 0}}));
        // Availability alone is not proof of own level-4 assembly or timer completion.
        QVERIFY(data.acceptOfficialReport("TechCoreMotionStateSync", QByteArray::fromHex("08013002383c400a"), ms() - 1400));
        QCOMPARE(data.officialSnapshot().techCore->value.remainTimeAll, std::optional<std::uint32_t>(60));
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("0801"), ms()));
        QTest::qWait(180);
        QVERIFY(!data.officialSnapshot().techCore && data.officialSnapshot().rune);
        QVERIFY(data.acceptOfficialReport("TechCoreMotionStateSync", QByteArray::fromHex("38004000"), ms()));
        data.resetOfficialReports();
        QVERIFY(!data.officialSnapshot().techCore);
    }
    void tacticalReportMapping() {
        GameData data;
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        // Independent official wire samples cover all known codes; unknown codes are not clamped.
        for (unsigned code : {1u, 2u, 3u, 255u}) {
            const auto sample = code == 255 ? QByteArray::fromHex("08ff0110011801")
                : QByteArray::fromHex("08") + QByteArray(1, char(code)) + QByteArray::fromHex("10011801");
            QVERIFY(data.acceptOfficialReport("SentryStatusSync", sample, ms()));
            const auto s = data.officialSnapshot().sentryStatus->value;
            QCOMPARE(s.postureId, std::optional<std::uint32_t>(code));
            QCOMPARE(s.posture, code <= 3 ? static_cast<SentryPosture>(code) : SentryPosture::Unknown);
            QCOMPARE(s.weakened, std::optional<bool>(true));
            QCOMPARE(s.powered, std::optional<bool>(true));
        }
        for (unsigned code : {1u, 2u, 3u, 4u, 5u, 255u}) {
            const auto sample = code == 255 ? QByteArray::fromHex("08ff01")
                : QByteArray::fromHex("08") + QByteArray(1, char(code));
            QVERIFY(data.acceptOfficialReport("DartSelectTargetStatusSync", sample, ms()));
            const auto s = data.officialSnapshot().dartStatus->value;
            QCOMPARE(s.targetId, std::optional<std::uint32_t>(code));
            QCOMPARE(s.target, code <= 5 ? static_cast<DartTarget>(code) : DartTarget::Unknown);
            QVERIFY(!s.gateCode);
            QCOMPARE(s.gate, DartGate::Unknown);
        }
        for (unsigned code : {0u, 1u, 2u, 255u}) {
            const auto sample = code == 255 ? QByteArray::fromHex("10ff01")
                : QByteArray::fromHex("10") + QByteArray(1, char(code));
            QVERIFY(data.acceptOfficialReport("DartSelectTargetStatusSync", sample, ms()));
            const auto s = data.officialSnapshot().dartStatus->value;
            QVERIFY(!s.targetId); // A replacement does not retain the previous target.
            QCOMPARE(s.target, DartTarget::Unknown);
            QCOMPARE(s.gateCode, std::optional<std::uint32_t>(code));
            QCOMPARE(s.gate, code <= 2 ? static_cast<DartGate>(code) : DartGate::Unknown);
        }
        QVERIFY(data.acceptOfficialReport("SentryStatusSync", QByteArray::fromHex("1000"), ms()));
        auto json = snapshotJson(data.officialSnapshot());
        QCOMPARE(json["sentry_status"].toObject()["posture"].toString(), QString("unknown"));
        QCOMPARE(json["sentry_status"].toObject()["weakened"], QJsonValue(false));
        QVERIFY(!json["sentry_status"].toObject().contains("powered"));
        QVERIFY(!json["sentry_status"].toObject().contains("posture_id"));
        QCOMPARE(json["dart_status"].toObject()["gate"].toString(), QString("unknown"));
        QCOMPARE(json["dart_status"].toObject()["gate_code"].toInt(), 255);
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("080210031d0000f040"), ms()));
        QCOMPARE(data.officialSnapshot().rune->value.averageRings, std::optional<float>(7.5f));
        QCOMPARE(data.officialSnapshot().rune->value.activatedArms, std::optional<std::uint32_t>(3));
        for (const char* hex : {"1d0000c07f", "1d0000807f", "1d000080ff", "1d00"}) {
            QVERIFY(!data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex(hex), ms()));
            QCOMPARE(data.officialSnapshot().rune->value.averageRings, std::optional<float>(7.5f));
        }
        QVERIFY(data.acceptOfficialReport("RuneStatusSync", QByteArray::fromHex("1000"), ms() - 1400));
        QVERIFY(!data.officialSnapshot().rune->value.code);
        QVERIFY(!snapshotJson(data.officialSnapshot())["rune"].toObject().contains("average_rings"));
        QVERIFY(data.acceptOfficialReport("SentryStatusSync", QByteArray::fromHex("0801"), ms() - 1400));
        QTest::qWait(180);
        const auto expired = data.officialSnapshot();
        QVERIFY(!expired.rune && !expired.sentryStatus && expired.dartStatus);
        data.resetOfficialReports();
        QVERIFY(!data.officialSnapshot().dartStatus);
    }
    void penaltyConstraint() {
        const auto ms = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(); };
        for (int code : {0, 1, 2, 3, 4, 5, 6, 255, -1}) {
            GameData data;
            QVERIFY(!data.acceptOfficialReport("PenaltyInfo", QByteArray::fromHex("08"), ms()));
            QVERIFY(!data.acceptOfficialReport("PenaltyInfo", QByteArray::fromHex("0801"), ms() + 2000));
            QVERIFY(!data.acceptOfficialReport("PenaltyInfo", QByteArray::fromHex("0801"), ms() - 1600));
            QVERIFY(!data.officialSnapshot().penaltyReleaseUnverified);
            const auto sample = code < 0 ? QByteArray::fromHex("1000")
                : QByteArray(1, char(8)) + (code == 255 ? QByteArray::fromHex("ff01") : QByteArray(1, char(code)));
            QVERIFY(data.acceptOfficialReport("PenaltyInfo", sample, ms()));
            const bool restricted = code < 4 || code > 6;
            QCOMPARE(data.officialSnapshot().penaltyReleaseUnverified, restricted);
            // Neither explicit zeros nor a later non-card report proves release.
            QVERIFY(data.acceptOfficialReport("PenaltyInfo", QByteArray::fromHex("080410001800"), ms()));
            QCOMPARE(data.officialSnapshot().penaltyReleaseUnverified, restricted);
            data.resetOfficialReports();
            QVERIFY(!data.officialSnapshot().penalty);
            QCOMPARE(data.officialSnapshot().penaltyReleaseUnverified, restricted);
        }
        GameData data;
        QVERIFY(data.acceptOfficialReport("PenaltyInfo", QByteArray::fromHex("080110001800"), ms()));
        QTest::qWait(1600);
        QVERIFY(!data.officialSnapshot().penalty); // Fresh report expires normally.
        QVERIFY(data.officialSnapshot().penaltyReleaseUnverified);
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("0801100018012003"), ms()));
        QVERIFY(data.acceptOfficialReport("RobotStaticStatus", QByteArray::fromHex("0801100018012004"), ms()));
        QVERIFY(data.acceptOfficialReport("GameStatus", QByteArray::fromHex("28044000"), ms()));
        auto c = permittedFixture(); c.requestedId = c.reportedId = 4;
        QCOMPARE(QString::fromLatin1(businessCommandBlock(KeyboardMouseCommand{}, data.officialSnapshot(),
            c, std::chrono::steady_clock::now())), QString("penalty_release_unverified"));
        QVERIFY(snapshotJson(data.officialSnapshot())["control_constraints"].toObject()
            ["penalty_release_unverified"].toBool());
    }
    void penaltyBackendRestart() {
        Peer first; UpstreamBackend backend;
        backend.start("127.0.0.1", first.port, 3);
        QTRY_VERIFY(backend.view().snapshot.rune && backend.view().snapshot.game);
        const auto c = permittedFixture();
        auto noSource = c; noSource.keyboardSourceSelected = false;
        backend.updateInput(KeyboardMouseCommand{1, 0, 0, false, false, false, 1}, noSource);
        QTest::qWait(80); QCOMPARE(first.count(), std::size_t(0));
        QVERIFY(backend.view().subscribed); // Unknown source stays closed without fabricating a report.
        first.penaltyType = 1; first.refresh = true;
        QTRY_VERIFY(backend.view().snapshot.penaltyReleaseUnverified);
        QCOMPARE(backend.submitCommand(RuneCommand{1}, c).reason, QString("penalty_release_unverified"));
        backend.updateInput(KeyboardMouseCommand{1, 0, 0, false, false, false, 1}, c);
        QTest::qWait(80); // Send-time check rejects input; no active transport needs cancellation.
        QCOMPARE(first.count(), std::size_t(0));
        backend.stop();
        Peer second; // Different endpoint, same identity, no penalty report after reconnect.
        backend.start("127.0.0.1", second.port, 3);
        QTRY_VERIFY(backend.view().snapshot.rune && backend.view().snapshot.game);
        QVERIFY(!backend.view().snapshot.penalty);
        QVERIFY(backend.view().snapshot.penaltyReleaseUnverified);
        QCOMPARE(backend.submitCommand(RuneCommand{1}, c).reason, QString("penalty_release_unverified"));
        backend.stop();
        Peer third; third.reportedId = 4; third.penaltyType = 4;
        backend.start("127.0.0.1", third.port, 4);
        QTRY_VERIFY(backend.view().snapshot.rune && backend.view().snapshot.penalty);
        QVERIFY(backend.view().snapshot.penaltyReleaseUnverified);
        auto changed = c; changed.requestedId = changed.reportedId = 4;
        QCOMPARE(backend.submitCommand(RuneCommand{1}, changed).reason, QString("penalty_release_unverified"));
        backend.updateInput(KeyboardMouseCommand{1, 0, 0, false, false, false, 1}, changed);
        QTest::qWait(80);
        QCOMPARE(second.count(), std::size_t(0)); QCOMPARE(third.count(), std::size_t(0));
        backend.stop();
    }
    void wireEncoding() {
        // Raw transport conformance only; this test grants no robot business eligibility.
        Peer peer; QVERIFY(peer.port);
        MqttManager mqtt;
        mqtt.connectToBrokerAsync(QString("tcp://127.0.0.1:%1").arg(peer.port), "3");
        QTRY_VERIFY(mqtt.isConnected());
        QSignalSpy delivery(&mqtt, &MqttManager::commandDelivery);
        for (const auto &f : fixtures()) {
            delivery.clear();
            const auto encoded = encodeCommand(f.command, MapOperator::SemiAutomatic);
            const auto result = mqtt.publishCommand(encoded.topic, encoded.payload);
            QVERIFY(result.status == MqttManager::PublishResult::Queued);
            QTRY_COMPARE(delivery.size(), 1);
            QCOMPARE(delivery[0][0].toInt(), result.token);
            QVERIFY(delivery[0][1].toBool());
        }
        std::lock_guard lock(peer.mutex);
        QVERIFY2(peer.error.isEmpty(), qPrintable(peer.error));
        QCOMPARE(peer.received.size(), std::size_t(11));
        const auto expected = fixtures();
        for (std::size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(peer.received[i].header, static_cast<unsigned char>(0x32));
            QCOMPARE(peer.received[i].topic, QByteArray(expected[i].topic));
            QCOMPARE(peer.received[i].payload.toHex(), QByteArray(expected[i].hex));
        }
    }
    void sendTimeBusinessChecks() {
        Peer peer; QVERIFY(peer.port);
        GameData data; RM::NetworkManager network(&data);
        QVERIFY(network.sendCommand(RuneCommand{1}, {}).status == CommandSendStatus::Blocked);
        QVERIFY(network.sendCommand(RuneCommand{1}, permittedFixture()).status == CommandSendStatus::Disconnected);
        network.startMqtt(QString("tcp://127.0.0.1:%1").arg(peer.port), "3");
        QVERIFY(network.topicsSubscribed());
        QTRY_VERIFY(data.officialSnapshot().rune && data.officialSnapshot().respawn && data.officialSnapshot().economy);
        auto c = permittedFixture();
        auto changed = c; changed.requestedId = changed.reportedId = 103;
        QCOMPARE(network.sendCommand(RuneCommand{1}, changed).reason, QString("configured_identity_or_session_changed"));
        QVERIFY(network.sendCommand(CommonCommand{1, 11}, c).status == CommandSendStatus::Invalid);
        const auto block = [&](const Command &command, const char *reason) {
            const auto before = peer.count();
            const auto result = network.sendCommand(command, c);
            QCOMPARE(result.status, CommandSendStatus::Blocked);
            QCOMPARE(result.reason, QString::fromLatin1(reason));
            QCOMPARE(peer.count(), before);
        };
        block(CommonCommand{3, 0}, "robot_not_reported_dead");
        block(CommonCommand{1, 10}, "exchange_area_and_team_quota_unverified");
        block(CommonCommand{1, 1010}, "ammo_quantity_exceeds_team_limit");
        peer.remoteEligible = true; peer.refresh = true;
        QTRY_VERIFY(data.officialSnapshot().selfDynamic && data.officialSnapshot().selfDynamic->value.canRemoteAmmo);
        block(CommonCommand{5, 100}, "insufficient_gold");
        block(CommonCommand{6, 60}, "remote_heal_cost_unknown"); // This peer has no authoritative countdown.
        block(AssemblyCommand{0, 1}, "assembly_role_mismatch");
        block(SentryCommand{12}, "sentry_operator_unverified");
        peer.alive = 2; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().selfConfig->value.aliveState, std::optional<std::uint32_t>(2));
        block(RuneCommand{1}, "robot_life_unconfirmed");
        QSignalSpy delivery(&network, &RM::NetworkManager::commandDelivery);
        QVERIFY(network.sendCommand(CommonCommand{3, 0}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1); QVERIFY(delivery[0][1].toBool());
        // Cached caller permit cannot override a newly revoked report or fee.
        peer.canFree = 0; peer.gold = 79; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().economy->value.remaining, std::optional<std::uint32_t>(79));
        block(CommonCommand{3, 0}, "free_respawn_not_allowed");
        block(CommonCommand{4, 0}, "insufficient_gold");
        peer.gold = 80; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().economy->value.remaining, std::optional<std::uint32_t>(80));
        delivery.clear();
        QVERIFY(network.sendCommand(CommonCommand{4, 0}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1); QVERIFY(delivery[0][1].toBool());
        peer.stage = 1; peer.alive = 1; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().game->value.phase, Phase::Preparation);
        c.terminal.phase = MatchPhase::Preparation;
        delivery.clear();
        const PerformanceCommand performance{1, 2, {}};
        QVERIFY(network.sendCommand(performance, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1); QVERIFY(delivery[0][1].toBool());
        peer.stage = 4; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().game->value.phase, Phase::Playing);
        block(performance, "current_game_unavailable_or_paused"); // Forged cached phase fails.
        c.terminal.phase = MatchPhase::Match;
        QTest::qWait(1600);
        block(RuneCommand{1}, "current_identity_unknown_or_changed");
        peer.reportedId = 103; peer.refresh = true;
        QTRY_VERIFY(data.officialSnapshot().selfConfig
            && data.officialSnapshot().selfConfig->value.robotId == std::optional<std::uint32_t>(103));
        block(RuneCommand{1}, "current_identity_unknown_or_changed");
        peer.reportedId = 3; peer.paused = true; peer.refresh = true;
        QTRY_VERIFY(data.officialSnapshot().game && data.officialSnapshot().game->value.paused == std::optional<bool>(true));
        block(RuneCommand{1}, "current_game_unavailable_or_paused");
        peer.paused = false; peer.refresh = true;
        QTRY_VERIFY(data.officialSnapshot().rune && data.officialSnapshot().game->value.paused == std::optional<bool>(false));
        peer.acknowledge = false; delivery.clear();
        const auto unknown = network.sendCommand(RuneCommand{1}, c);
        QVERIFY2(unknown.status == CommandSendStatus::Queued, qPrintable(unknown.reason));
        QTRY_COMPARE(delivery.size(), 1);
        QCOMPARE(delivery[0][0].toInt(), unknown.token); QVERIFY(!delivery[0][1].toBool());
        QVERIFY(!network.isMqttConnected()); QVERIFY(!network.topicsSubscribed());
        QVERIFY(network.sendCommand(RuneCommand{1}, c).status == CommandSendStatus::Disconnected);
        const auto finalCount = peer.count(); QTest::qWait(1200);
        QCOMPARE(peer.count(), finalCount); // No replay after missing PUBACK.
    }
    void deploymentSendChecks() {
        Peer peer; peer.reportedId = 1; peer.deploymentState = 1;
        GameData data; RM::NetworkManager network(&data);
        network.startMqtt(QString("tcp://127.0.0.1:%1").arg(peer.port), "1");
        QTRY_VERIFY(data.officialSnapshot().deployment && data.officialSnapshot().game);
        auto c = permittedFixture(); c.requestedId = c.reportedId = 1;
        QCOMPARE(network.sendCommand(DeployCommand{1}, c).reason, QString("deployment_not_confirmed_inactive"));
        QCOMPARE(peer.count(), std::size_t(0));
        peer.deploymentState = 2; peer.refresh = true;
        QTRY_COMPARE(data.officialSnapshot().deployment->value.code, std::optional<std::uint32_t>(2));
        QCOMPARE(network.sendCommand(DeployCommand{1}, c).reason, QString("deployment_not_confirmed_inactive"));
        QCOMPARE(network.sendCommand(DeployCommand{0}, c).reason, QString("robot_not_deployed"));
        QCOMPARE(peer.count(), std::size_t(0));
        peer.deploymentState = 1; peer.alive = 2; peer.refresh = true;
        QTRY_VERIFY(data.officialSnapshot().deployment->value.state == DeployState::Deployed
            && data.officialSnapshot().selfConfig->value.aliveState == std::optional<std::uint32_t>(2));
        QSignalSpy delivery(&network, &RM::NetworkManager::commandDelivery);
        QCOMPARE(network.sendCommand(DeployCommand{0}, c).status, CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1); QVERIFY(delivery[0][1].toBool());
        std::lock_guard lock(peer.mutex);
        QCOMPARE(peer.received.size(), std::size_t(1));
        QCOMPARE(peer.received[0].topic, QByteArray("HeroDeployModeEventCommand"));
        QCOMPARE(peer.received[0].payload.toHex(), QByteArray("0800"));
    }
    void malformedPuback_data() {
        QTest::addColumn<unsigned>("fault");
        QTest::newRow("zero-packet-id") << 1u;
        QTest::newRow("short-body") << 2u;
        QTest::newRow("reserved-flags") << 3u;
    }
    void malformedPuback() {
        QFETCH(unsigned, fault);
        Peer peer; QVERIFY(peer.port); peer.ackFault = fault;
        GameData data; RM::NetworkManager network(&data);
        network.startMqtt(QString("tcp://127.0.0.1:%1").arg(peer.port), "3");
        QTRY_VERIFY(data.officialSnapshot().selfConfig && data.officialSnapshot().game && data.officialSnapshot().rune);
        QSignalSpy delivery(&network, &RM::NetworkManager::commandDelivery);
        const auto result = network.sendCommand(RuneCommand{1}, permittedFixture());
        QVERIFY(result.status == CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1);
        QVERIFY(!delivery[0][1].toBool());
        QTRY_VERIFY(!network.isMqttConnected());
        QVERIFY(!data.officialSnapshot().selfConfig);
        std::lock_guard lock(peer.mutex);
        QCOMPARE(peer.received.size(), std::size_t(1));
    }
    void asynchronousAckAndCancellation() {
        Peer peer; peer.ackDelayMs = 250;
        GameData data; RM::NetworkManager network(&data);
        network.startMqtt(QString("tcp://127.0.0.1:%1").arg(peer.port), "3");
        QTRY_VERIFY(data.officialSnapshot().selfConfig && data.officialSnapshot().game && data.officialSnapshot().rune);
        QSignalSpy delivery(&network, &RM::NetworkManager::commandDelivery);
        int ticks = 0;
        QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; }); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start();
        const auto queued = network.sendCommand(RuneCommand{1}, permittedFixture());
        QVERIFY(queued.status == CommandSendStatus::Queued);
        QVERIFY(elapsed.elapsed() < 150); // Admission returns before the delayed ACK.
        QTRY_COMPARE(delivery.size(), 1);
        QVERIFY(delivery[0][1].toBool()); QVERIFY(ticks >= 10);

        delivery.clear();
        const auto cancelled = network.sendCommand(KeyboardMouseCommand{0, 0, 0, false, false, false, 1}, permittedFixture());
        QVERIFY(cancelled.status == CommandSendStatus::Queued);
        QTRY_COMPARE(peer.count(), std::size_t(2));
        network.cancelCommands();
        QCOMPARE(network.commandsInFlight(), 0);
        QCOMPARE(delivery.size(), 1); QVERIFY(!delivery[0][1].toBool());
        QTest::qWait(350);
        QCOMPARE(delivery.size(), 1); // Late callback cannot turn cancellation into success.
        Peer next;
        network.startMqtt(QString("tcp://127.0.0.1:%1").arg(next.port), "3");
        QTRY_VERIFY(data.officialSnapshot().selfConfig && data.officialSnapshot().game && data.officialSnapshot().rune);
        delivery.clear();
        const auto fresh = network.sendCommand(RuneCommand{1}, permittedFixture());
        QVERIFY(fresh.status == CommandSendStatus::Queued);
        QTRY_COMPARE(delivery.size(), 1); QVERIFY(delivery[0][1].toBool());
        QCOMPARE(next.count(), std::size_t(1)); // No old key/event replay on a new handle.
    }
    void inFlightBound() {
        Peer peer; peer.acknowledge = false;
        MqttManager mqtt;
        mqtt.connectToBrokerAsync(QString("tcp://127.0.0.1:%1").arg(peer.port), "3");
        QTRY_VERIFY(mqtt.isConnected());
        QSignalSpy delivery(&mqtt, &MqttManager::commandDelivery);
        const auto samples = fixtures();
        for (int i = 0; i < 8; ++i) {
            const auto encoded = encodeCommand(samples[i].command, MapOperator::SemiAutomatic);
            QVERIFY(mqtt.publishCommand(encoded.topic, encoded.payload).status == MqttManager::PublishResult::Queued);
        }
        QCOMPARE(mqtt.commandsInFlight(), 8);
        const auto ninth = encodeCommand(samples[8].command);
        QVERIFY(mqtt.publishCommand(ninth.topic, ninth.payload).status == MqttManager::PublishResult::InFlightLimit);
        QTRY_COMPARE(peer.count(), std::size_t(8));
        QTRY_COMPARE(delivery.size(), 8);
        for (const auto &row : delivery) QVERIFY(!row[1].toBool());
        QCOMPARE(mqtt.commandsInFlight(), 0); QVERIFY(!mqtt.isConnected());
        QTest::qWait(200); QCOMPARE(peer.count(), std::size_t(8));
    }
    void respawnEventHandoff() {
        Peer peer; UpstreamBackend backend;
        backend.start("127.0.0.1", peer.port, 3);
        QTRY_VERIFY(backend.view().snapshot.respawn && backend.view().snapshot.economy && backend.view().snapshot.rune);
        const auto c = permittedFixture(); // Isolated fixture only, never ordinary UI authority.
        QCOMPARE(backend.submitCommand(CommonCommand{3, 0}, c).reason, QString("robot_not_reported_dead"));
        QCOMPARE(backend.view().status["commands"].toMap()["event_depth"].toInt(), 0);
        // An ineligible event must not occupy the slot needed by a valid event.
        QCOMPARE(backend.submitCommand(SentryCommand{12}, c).reason, QString("sentry_operator_unverified"));
        QVERIFY(backend.submitCommand(RuneCommand{1}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(peer.count(), std::size_t(1));
        QTRY_COMPARE(backend.view().status["commands"].toMap()["event_status"].toString(), QString("broker_acknowledged"));
        peer.alive = 2; peer.refresh = true;
        QTRY_VERIFY(backend.view().snapshot.selfConfig
            && backend.view().snapshot.selfConfig->value.aliveState == std::optional<std::uint32_t>(2));
        QVERIFY(backend.submitCommand(CommonCommand{3, 0}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(peer.count(), std::size_t(2));
        QVERIFY(backend.submitCommand(CommonCommand{4, 0}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(peer.count(), std::size_t(3));
        {
            std::lock_guard lock(peer.mutex);
            QCOMPARE(peer.received[1].topic, QByteArray("CommonCommand"));
            QCOMPARE(peer.received[1].payload.toHex(), QByteArray("08031000"));
            QCOMPARE(peer.received[2].payload.toHex(), QByteArray("08041000"));
        }
        peer.canFree = 0; peer.gold = 79; peer.refresh = true;
        QTRY_VERIFY(backend.view().snapshot.economy
            && backend.view().snapshot.economy->value.remaining == std::optional<std::uint32_t>(79));
        QCOMPARE(backend.submitCommand(CommonCommand{3, 0}, c).reason, QString("free_respawn_not_allowed"));
        QCOMPARE(backend.submitCommand(CommonCommand{4, 0}, c).reason, QString("insufficient_gold"));
        QTest::qWait(150); QCOMPARE(peer.count(), std::size_t(3));
        backend.stop();
    }
    void eventDeliveryIsSeparateFromInput() {
        Peer peer; UpstreamBackend backend;
        peer.dropEventAcks = true;
        backend.start("127.0.0.1", peer.port, 3);
        QTRY_VERIFY(backend.view().snapshot.selfConfig && backend.view().snapshot.game && backend.view().snapshot.rune);
        const auto c = permittedFixture();
        QVERIFY(backend.submitCommand(RuneCommand{1}, c).status == CommandSendStatus::Queued);
        QTRY_COMPARE(peer.count(), std::size_t(1));
        QTRY_COMPARE(backend.view().status["commands"].toMap()["event_status"].toString(), QString("awaiting_puback"));
        QTimer producer;
        connect(&producer, &QTimer::timeout, this, [&] { backend.updateInput(KeyboardMouseCommand{}, c); });
        producer.start(30);
        QTRY_VERIFY(backend.view().status["commands"].toMap()["broker_acknowledged"].toUInt() > 0);
        // ACKs for subsequent keyboard frames must not confirm the unacknowledged event.
        QCOMPARE(backend.view().status["commands"].toMap()["event_status"].toString(), QString("awaiting_puback"));
        QTRY_COMPARE(backend.view().status["commands"].toMap()["event_status"].toString(), QString("delivery_unknown"));
        producer.stop();
        backend.stop();
        QVERIFY(backend.view().status.isEmpty());
    }
    void latestInputAndRelease() {
        Peer peer; UpstreamBackend backend;
        backend.start("127.0.0.1", peer.port, 3);
        QTRY_VERIFY(backend.view().snapshot.selfConfig && backend.view().snapshot.game && backend.view().snapshot.rune);
        const auto c = permittedFixture();
        const KeyboardMouseCommand held{0, 0, 0, false, false, false, 1};
        backend.updateInput(held, c);
        QTRY_COMPARE(peer.count(), std::size_t(1));
        backend.updateInput(KeyboardMouseCommand{}, c);
        backend.updateInput(held, c); // Repress cannot replace the pending release.
        QTRY_COMPARE(peer.count(), std::size_t(3));
        {
            std::lock_guard lock(peer.mutex);
            QCOMPARE(peer.received[0].payload, encodeCommand(held).payload);
            QCOMPARE(peer.received[1].payload, encodeCommand(KeyboardMouseCommand{}).payload);
            QCOMPARE(peer.received[2].payload, encodeCommand(held).payload);
        }
        // A UI gate closes the session, never publishes a special bypass frame.
        auto occupied = c; occupied.terminal.uiOccupied = true;
        backend.updateInput(KeyboardMouseCommand{}, occupied);
        QTRY_VERIFY(!backend.view().subscribed);
        QTest::qWait(150); QCOMPARE(peer.count(), std::size_t(3));
        Peer next;
        backend.start("127.0.0.1", next.port, 3);
        QTRY_VERIFY(backend.view().snapshot.selfConfig && backend.view().snapshot.game && backend.view().snapshot.rune);
        QTest::qWait(120); QCOMPARE(next.count(), std::size_t(0));
        backend.updateInput(KeyboardMouseCommand{5, -2, 120, false, false, false, 1}, c);
        QTRY_COMPARE(next.count(), std::size_t(1));
        QTest::qWait(60); QCOMPARE(next.count(), std::size_t(1)); // Deltas consumed once.
        QTRY_VERIFY(!backend.view().subscribed); // Missing fresh GUI input cancels capture.
        backend.stop();
        QVERIFY(backend.submitCommand(RuneCommand{1}, {}).status == CommandSendStatus::Blocked);
    }
    void queuedEventIsolation_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("event_eligibility_changed") << 0;
        QTest::newRow("event_expired") << 1;
        QTest::newRow("game_paused") << 2;
        QTest::newRow("identity_changed") << 3;
        QTest::newRow("robot_dead") << 4;
        QTest::newRow("penalty_received") << 5;
    }
    void queuedEventIsolation() {
        QFETCH(int, change);
        Peer peer; peer.acknowledge = false;
        UpstreamBackend backend;
        backend.start("127.0.0.1", peer.port, 3);
        QTRY_VERIFY(backend.view().snapshot.rune && backend.view().snapshot.game);
        const auto c = permittedFixture();
        QTimer producer; producer.setTimerType(Qt::PreciseTimer); producer.setInterval(14);
        connect(&producer, &QTimer::timeout, this, [&] {
            backend.updateInput(KeyboardMouseCommand{1, 0, 0, false, false, false, 1}, c);
        });
        producer.start();
        QTRY_COMPARE(backend.view().status.value("commands").toMap().value("in_flight").toInt(), 8);
        QCOMPARE(backend.submitCommand(RuneCommand{1}, c).status, CommandSendStatus::Queued);
        // No event can publish until the eight input ACKs are released.
        if (change == 0) peer.runeCode = 2;
        if (change == 2) peer.paused = true;
        if (change == 3) peer.reportedId = 4;
        if (change == 4) peer.alive = 2;
        if (change == 5) peer.penaltyType = 1;
        peer.refresh = true;
        if (change <= 1) {
            QTRY_COMPARE_WITH_TIMEOUT(backend.view().status.value("commands").toMap()
                .value("events_rejected").toULongLong(), 1ULL, 500);
            QCOMPARE(backend.view().status.value("commands").toMap().value("event_status").toString(),
                QString(change == 0 ? "rune_not_inactive" : "event_expired"));
            QVERIFY(backend.view().subscribed);
            const auto before = peer.count();
            peer.acknowledge = true;
            QTRY_VERIFY_WITH_TIMEOUT(peer.count() > before, 500); // Input survived the discarded event.
        } else {
            QTRY_VERIFY(!backend.view().subscribed); // A shared gate still invalidates control.
        }
        producer.stop(); backend.stop();
        std::lock_guard lock(peer.mutex);
        QCOMPARE(std::count_if(peer.received.begin(), peer.received.end(),
            [](const auto &sample) { return sample.topic == "RuneActivateCommand"; }), 0);
    }
    void continuousInputAndEvent() {
        Peer peer; UpstreamBackend backend;
        backend.start("127.0.0.1", peer.port, 3);
        QTRY_VERIFY(backend.view().snapshot.selfConfig && backend.view().snapshot.game && backend.view().snapshot.rune);
        const auto c = permittedFixture();
        for (int i = 1; i <= 500; ++i)
            backend.updateInput(KeyboardMouseCommand{i, 0, 0, false, false, false, 1}, c);
        const auto latestPayload = encodeCommand(KeyboardMouseCommand{500, 0, 0, false, false, false, 1}).payload;
        const auto receivedLatest = [&] {
            std::lock_guard lock(peer.mutex);
            return std::any_of(peer.received.begin(), peer.received.end(),
                [&](const auto &sample) { return sample.payload == latestPayload; });
        };
        QTRY_VERIFY(receivedLatest());
        QVERIFY(peer.count() < 5); // The 500 GUI updates were not queued for transmission.
        QTimer producer; producer.setTimerType(Qt::PreciseTimer); producer.setInterval(14);
        connect(&producer, &QTimer::timeout, this, [&] {
            backend.updateInput(KeyboardMouseCommand{1, 0, 0, false, false, false, 1}, c);
        });
        producer.start();
        QVERIFY(backend.submitCommand(RuneCommand{1}, c).status == CommandSendStatus::Queued);
        QTest::qWait(700);
        {
            std::lock_guard lock(peer.mutex);
            const auto count = std::count_if(peer.received.begin(), peer.received.end(),
                [](const auto &sample) { return sample.topic == "KeyboardMouseControl"; });
            QVERIFY(count >= 20 && count <= 60); // Bounded ongoing transport, no 500-packet backlog.
            QCOMPARE(std::count_if(peer.received.begin(), peer.received.end(),
                [](const auto &sample) { return sample.topic == "RuneActivateCommand"; }), 1);
        }
        const auto beforeRejectedEvent = peer.count();
        QCOMPARE(backend.submitCommand(SentryCommand{12}, c).reason, QString("sentry_operator_unverified"));
        QTest::qWait(70);
        QVERIFY(backend.view().subscribed);
        QVERIFY(peer.count() > beforeRejectedEvent); // Valid ongoing input survived event rejection.
        const auto commands = backend.view().status.value("commands").toMap();
        QVERIFY(commands.value("latest_input_depth").toInt() <= 1);
        QVERIFY(commands.value("release_depth").toInt() <= 1);
        QVERIFY(commands.value("in_flight").toInt() <= 8);
        QVERIFY(commands.value("replaced").toULongLong() > 400);
        producer.stop(); backend.stop();
        QTest::qWait(150); // A packet already admitted before stop cannot be retracted.
        const auto stoppedAt = peer.count();
        QTest::qWait(100); QCOMPARE(peer.count(), stoppedAt);
    }
};
QTEST_GUILESS_MAIN(UplinkTest)
#include "test_rm26_uplink.moc"
