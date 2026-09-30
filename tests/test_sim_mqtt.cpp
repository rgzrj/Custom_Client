#include "sim/sim_mqtt_receiver.hpp"
#include "sim/sim_referee_receiver.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {
const QByteArray kTopic = QByteArrayLiteral("rm27/sim/v1/referee");
const QByteArray kSample = R"json({"channel":"rm27.sim.v1.referee","sequence":1,
    "sent_at_unix_ms":1789999999000,"phase":"match","telemetry":{
    "hp":850,"max_hp":1000,"chassis_power_w":80,"buffer_energy_j":45,
    "barrel_heat":30,"ammo":90,"latency_ms":12,"packet_loss_pct":0.1}})json";

QByteArray packet(quint8 header, const QByteArray& body)
{
    QByteArray result(1, char(header));
    int length = body.size();
    do {
        quint8 digit = length % 128;
        length /= 128;
        if (length) digit |= 0x80;
        result.append(char(digit));
    } while (length);
    return result + body;
}

QByteArray publish(const QByteArray& topic, const QByteArray& payload)
{
    QByteArray body;
    body.append(char(topic.size() >> 8));
    body.append(char(topic.size()));
    return packet(0x30, body + topic + payload);
}
}

class SimMqttTest : public QObject {
    Q_OBJECT
private slots:
    void localSubscriptionAndRecovery()
    {
        QTcpServer broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost, 0));
        rm27::sim::RefereeReceiver parser;
        rm27::sim::MqttReceiver receiver(nullptr, broker.serverPort());
        int received = 0;
        int payloads = 0;
        int stale = 0;
        bool subscribed = false;
        QVERIFY(rm27::sim::parseRefereeDatagram(kSample));
        connect(&receiver, &rm27::sim::MqttReceiver::payloadReceived,
                &receiver, [&](const QByteArray&) { ++payloads; });
        connect(&receiver, &rm27::sim::MqttReceiver::linkChanged,
                &receiver, [&](bool active) { subscribed = active; });
        connect(&receiver, &rm27::sim::MqttReceiver::payloadReceived,
                &parser, &rm27::sim::RefereeReceiver::acceptPayload);
        connect(&parser, &rm27::sim::RefereeReceiver::sampleReceived,
                &parser, [&](const rm27::sim::RefereeSample& sample) {
                    QCOMPARE(sample.hp, 850);
                    ++received;
                });
        connect(&parser, &rm27::sim::RefereeReceiver::dataBecameStale,
                &parser, [&] { ++stale; });
        receiver.start();
        QTRY_VERIFY_WITH_TIMEOUT(broker.hasPendingConnections(), 1500);
        auto* peer = broker.nextPendingConnection();
        QVERIFY(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= 31, 1000);
        const QByteArray connectPacket = peer->readAll();
        QVERIFY(connectPacket.startsWith(QByteArray("\x10\x1d\x00\x04MQTT\x04\x02\x00\x00", 12)));
        peer->write(QByteArray("\x20\x02\x00\x00", 4));
        QByteArray subscription("\x00\x01", 2);
        subscription.append(char(kTopic.size() >> 8));
        subscription.append(char(kTopic.size()));
        subscription.append(kTopic);
        subscription.append(char(0));
        const QByteArray expected = packet(0x82, subscription);
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= expected.size(), 1000);
        QCOMPARE(peer->readAll(), expected);
        peer->write(packet(0x90, QByteArray("\x00\x01\x00", 3)));
        QTRY_VERIFY_WITH_TIMEOUT(subscribed, 1000);
        const QByteArray valid = publish(kTopic, kSample);
        peer->write(valid.left(5));
        QTest::qWait(20);
        QCOMPARE(received, 0);
        peer->write(valid.mid(5));
        QTRY_COMPARE_WITH_TIMEOUT(payloads, 1, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(received, 1, 1000);
        peer->write(valid); // Duplicate sequence must not refresh data.
        peer->write(publish(QByteArrayLiteral("rm27/sim/v1/not-here"), kSample));
        QTest::qWait(50);
        QCOMPARE(received, 1);
        QCOMPARE(peer->bytesAvailable(), 0); // The observer never publishes.
        QTRY_COMPARE_WITH_TIMEOUT(stale, 1, 1500);
        QByteArray restarted = kSample;
        restarted.replace("\"sequence\":1", "\"sequence\":0");
        peer->write(publish(kTopic, restarted));
        QTRY_COMPARE_WITH_TIMEOUT(received, 2, 1000);
        peer->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(broker.hasPendingConnections(), 2500);
        auto* reconnected = broker.nextPendingConnection();
        QVERIFY(reconnected);
        QTRY_VERIFY_WITH_TIMEOUT(reconnected->bytesAvailable() >= 31, 1000);
        QVERIFY(reconnected->readAll().startsWith("\x10"));
        reconnected->write(QByteArray("\x20\x02\x00\x00", 4));
        QTRY_VERIFY_WITH_TIMEOUT(reconnected->bytesAvailable() >= expected.size(), 1000);
        QCOMPARE(reconnected->readAll(), expected);
        reconnected->write(packet(0x90, QByteArray("\x00\x01\x00", 3)));
        reconnected->write(publish(kTopic, kSample));
        QTRY_COMPARE_WITH_TIMEOUT(received, 3, 2000);
    }
};

QTEST_GUILESS_MAIN(SimMqttTest)
#include "test_sim_mqtt.moc"
