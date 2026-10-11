#include "video/historical_video_receiver.hpp"
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>
#include <stdexcept>

class HistoricalVideoTest : public QObject {
    Q_OBJECT
    static QByteArray packet(quint16 frame, quint16 slice, quint32 total, const QByteArray& data)
    {
        QByteArray bytes(8, '\0');
        auto* header = reinterpret_cast<uchar*>(bytes.data());
        qToBigEndian(frame, header);
        qToBigEndian(slice, header + 2);
        qToBigEndian(total, header + 4);
        return bytes + data;
    }
    static QByteArray nal(unsigned type, int size = 7)
    {
        QByteArray data = QByteArray::fromHex("0000000100012a");
        data[4] = char(type << 1);
        if (size > data.size()) data.append(QByteArray(size - data.size(), 'x'));
        return data;
    }
    static QByteArray parameters() { return nal(32) + nal(33) + nal(34); }
    static QByteArray keyframe(int size = 28) { return parameters() + nal(19, size - 21); }
private slots:
    void invalidFragmentBase()
    {
        QVERIFY_EXCEPTION_THROWN(rm27::video::HistoricalVideoReceiver(2), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(rm27::video::HistoricalVideoReceiver(65536), std::invalid_argument);
    }

    void oneBasedFragments()
    {
        QUdpSocket output, reservation, sender;
        QVERIFY(output.bind(QHostAddress::LocalHost, 0));
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const quint16 inputPort = reservation.localPort();
        reservation.close();
        rm27::video::HistoricalVideoReceiver receiver(1);
        QVERIFY(receiver.start(inputPort, output.localPort()));
        sender.writeDatagram(packet(0, 0, 3, "bad"), QHostAddress::LocalHost, inputPort);
        QTest::qWait(40);
        QVERIFY(!output.hasPendingDatagrams());
        const auto key = keyframe();
        sender.writeDatagram(packet(0, 1, key.size(), key), QHostAddress::LocalHost, inputPort);
        QTRY_VERIFY(output.hasPendingDatagrams());
        QCOMPARE(output.receiveDatagram().data(), parameters() + key);
    }

    void transport()
    {
        QUdpSocket output, reservation, sender, otherSender;
        QVERIFY(output.bind(QHostAddress::LocalHost, 0));
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const quint16 inputPort = reservation.localPort();
        reservation.close();
        rm27::video::HistoricalVideoReceiver receiver(0);
        QSignalSpy stats(&receiver, &rm27::video::HistoricalVideoReceiver::statsChanged);
        QVERIFY(!receiver.start(0, output.localPort()));
        QVERIFY(!receiver.start(output.localPort(), output.localPort()));
        QVERIFY(!receiver.start(inputPort, output.localPort(), QHostAddress::AnyIPv4, QHostAddress::LocalHost));
        QVERIFY(!receiver.start(inputPort, output.localPort(), QHostAddress::LocalHost, QHostAddress("239.1.2.3")));
        QVERIFY(!receiver.start(inputPort, output.localPort(), QHostAddress::LocalHost, QHostAddress("192.168.1.2")));
        QVERIFY(receiver.start(inputPort, output.localPort()));
        const QByteArray data = keyframe(3000);
        const auto send = [&](quint16 frame, quint16 slice, const QByteArray& bytes) {
            const auto p = packet(frame, slice, data.size(), bytes);
            QCOMPARE(sender.writeDatagram(p, QHostAddress::LocalHost, inputPort), p.size());
        };
        send(100, 2, data.mid(2784)); // out of order and duplicate
        send(100, 0, data.left(1392));
        send(100, 0, data.left(1392));
        QTest::qWait(25);
        QVERIFY(!output.hasPendingDatagrams());
        send(100, 1, data.mid(1392, 1392));
        QTRY_VERIFY(output.hasPendingDatagrams());
        QByteArray assembled;
        QTRY_VERIFY(output.bytesAvailable() > 0);
        QTest::qWait(30);
        while (output.hasPendingDatagrams()) assembled += output.receiveDatagram().data();
        QCOMPARE(assembled, parameters() + data);
        send(99, 0, data.left(1392)); // old frame
        send(101, 0, data.left(1392)); // incomplete frame expires
        const auto bad = packet(102, 0, 0xffffffffu, QByteArray(1, 'x'));
        QCOMPARE(sender.writeDatagram(bad, QHostAddress::LocalHost, inputPort), bad.size());
        QCOMPARE(sender.writeDatagram(QByteArray(60000, 'x'), QHostAddress::LocalHost, inputPort), 60000);
        const auto foreign = packet(102, 0, 3, "abc");
        QCOMPARE(otherSender.writeDatagram(foreign, QHostAddress::LocalHost, inputPort), foreign.size());
        QTest::qWait(300);
        QVERIFY(!output.hasPendingDatagrams());
        QVERIFY(!stats.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(stats.last().first().toMap().value("bufferedBytes").toULongLong(), 0u, 1000);
        auto report = stats.last().first().toMap();
        QCOMPARE(report.value("assembledFrames").toULongLong(), 1u);
        QCOMPARE(report.value("bufferedBytes").toULongLong(), 0u);
        QVERIFY(report.value("rejectedPackets").toULongLong() >= 4);
        QVERIFY(report.value("duplicates").toULongLong() >= 1);
        QVERIFY(report.value("droppedFrames").toULongLong() >= 1);
        QVERIFY(report.value("waitingForKeyframe").toBool());
        const auto dependent = nal(1);
        sender.writeDatagram(packet(102, 0, dependent.size(), dependent), QHostAddress::LocalHost, inputPort);
        QTest::qWait(50);
        QVERIFY(!output.hasPendingDatagrams());
        const auto key = keyframe();
        sender.writeDatagram(packet(103, 0, key.size(), key), QHostAddress::LocalHost, inputPort);
        QTRY_VERIFY(output.hasPendingDatagrams());
        QCOMPARE(output.receiveDatagram().data(), parameters() + key);
        // A complete lost sequence also requires a new keyframe, not just a timeout.
        sender.writeDatagram(packet(105, 0, dependent.size(), dependent), QHostAddress::LocalHost, inputPort);
        QTest::qWait(50);
        QVERIFY(!output.hasPendingDatagrams());
        QTest::qWait(750); // new sender/session after silence
        const auto restarted = packet(0, 0, key.size(), key);
        QCOMPARE(otherSender.writeDatagram(restarted, QHostAddress::LocalHost, inputPort), restarted.size());
        QTRY_VERIFY(output.hasPendingDatagrams());
        QCOMPARE(output.receiveDatagram().data(), parameters() + key);
        receiver.stop();
        QCOMPARE(sender.writeDatagram(packet(1, 0, 3, "xyz"), QHostAddress::LocalHost, inputPort), 11);
        QTest::qWait(100);
        QVERIFY(!output.hasPendingDatagrams());
    }

    void sourceAddressFilter()
    {
        QUdpSocket output, reservation, sender, foreign;
        QVERIFY(output.bind(QHostAddress::LocalHost, 0));
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        QVERIFY(sender.bind(QHostAddress::LocalHost, 0));
        QVERIFY(foreign.bind(QHostAddress("127.0.0.2"), 0));
        const auto port = reservation.localPort();
        reservation.close();
        rm27::video::HistoricalVideoReceiver receiver(0);
        QVERIFY(receiver.start(port, output.localPort(), QHostAddress::LocalHost, QHostAddress::LocalHost));
        const auto key = keyframe();
        const auto bytes = packet(0, 0, key.size(), key);
        QCOMPARE(foreign.writeDatagram(bytes, QHostAddress::LocalHost, port), bytes.size());
        QTest::qWait(50);
        QVERIFY(!output.hasPendingDatagrams());
        QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, port), bytes.size());
        QTRY_VERIFY(output.hasPendingDatagrams());
        QCOMPARE(output.receiveDatagram().data(), parameters() + key);
    }
};

QTEST_GUILESS_MAIN(HistoricalVideoTest)
#include "test_historical_video_receiver.moc"
