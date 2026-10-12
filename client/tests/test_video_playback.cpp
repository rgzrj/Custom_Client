#include <QGuiApplication>
#include <QAudioOutput>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCamera>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QProcess>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QKeySequence>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QUrl>
#include <QVideoSink>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QThread>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtEndian>
#include <cmath>
#include "app/terminal_controller.hpp"
#include "app/runtime_log.hpp"
#include "video/udp_video_ingress.hpp"

quint16 testRefereePort = 0;
quint16 testVideoProbePort = 0;
quint16 testVideoDecodePort = 0;

class PlaybackTest : public QObject {
    Q_OBJECT

    // HUD 与回放用例可直接关闭启动面板；图传用例必须走实际进入路径，
    // 避免遗漏应用链路参数对播放器和帧过期检测的影响。
    static void dismissStartPanel(QQuickWindow* window)
    {
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTest::qWait(60);
    }

    // 删掉隔离配置文件，回到默认档（非哨兵、非正式地址），
    // 避免上一个用例保存的哨兵段把后续用例带进模拟模式。
    static void resetIsolatedSettings()
    {
        const QString path = QString::fromUtf8(qgetenv("RM27_SETTINGS_FILE"));
        QVERIFY(!path.isEmpty());
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::remove(path + QStringLiteral(".tmp"));
        // 同时申请三个空闲端口，防止本测试误接应用默认会话。
        QUdpSocket refereeReservation, probeReservation, decodeReservation;
        QVERIFY(refereeReservation.bind(QHostAddress::LocalHost, 0));
        QVERIFY(probeReservation.bind(QHostAddress::LocalHost, 0));
        QVERIFY(decodeReservation.bind(QHostAddress::LocalHost, 0));
        testRefereePort = refereeReservation.localPort();
        testVideoProbePort = probeReservation.localPort();
        testVideoDecodePort = decodeReservation.localPort();
        rm27::app::ConnectionSettings settings;
        QVERIFY(settings.setProperty("refereePort", testRefereePort));
        QVERIFY(settings.setProperty("videoProbePort", testVideoProbePort));
        QVERIFY(settings.setProperty("simulationVideoPort", testVideoDecodePort));
        QVERIFY(settings.save());
    }

    // Repeater 生成的委托只能沿可视树找到（QObject::findChild 不覆盖它们），
    // 因此队伍栏里的格子按 childItems() 递归查找。
    static QQuickItem* findVisual(QQuickItem* root, const QString& name)
    {
        if (root == nullptr) {
            return nullptr;
        }
        if (root->objectName() == name) {
            return root;
        }
        const QList<QQuickItem*> children = root->childItems();
        for (QQuickItem* child : children) {
            if (QQuickItem* found = findVisual(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

private slots:
    void operatorInputLifecycle()
    {
        rm27::app::OperatorInput input;
        QWindow window;
        input.observe(&window);
        QEvent activate(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&window, &activate);
        input.setEnabled(true);
        QVERIFY(input.enabled());
        const auto key = [](QWindow *target, QEvent::Type type, int code, bool repeat = false) {
            QKeyEvent event(type, code, Qt::NoModifier, QString(), repeat);
            QCoreApplication::sendEvent(target, &event);
        };
        key(&window, QEvent::KeyPress, Qt::Key_W);
        QCOMPARE(input.frame().keys, 1u);
        input.setEnabled(false); // UI occupied / control gate closed.
        QCOMPARE(input.frame().keys, 0u);
        input.setEnabled(true);
        key(&window, QEvent::KeyPress, Qt::Key_W, true);
        QCOMPARE(input.frame().keys, 0u); // A held key cannot rearm after the gate opens.
        key(&window, QEvent::KeyRelease, Qt::Key_W);
        key(&window, QEvent::KeyPress, Qt::Key_W);
        QCOMPARE(input.frame().keys, 1u);
        QSignalSpy lost(&input, &rm27::app::OperatorInput::captureLost);
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&window, &deactivate);
        QVERIFY(!input.enabled()); QCOMPARE(input.frame().keys, 0u);
        QCOMPARE(lost.size(), 1);
        input.setEnabled(true); QVERIFY(!input.enabled());
        QCoreApplication::sendEvent(&window, &activate);
        input.setEnabled(true);
        QCOMPARE(input.frame().keys, 0u);
        key(&window, QEvent::KeyRelease, Qt::Key_W);

        std::vector<rm27::historical::rm2026::KeyboardMouseCommand> frames;
        connect(&input, &rm27::app::OperatorInput::frameReady, this,
                [&] { frames.push_back(input.frame()); });
        const auto move = [&](QPointF pos) {
            QMouseEvent event(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &event);
        };
        move({10, 10}); move({15, 13});
        QWheelEvent wheel({15, 13}, {15, 13}, {}, {0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(&window, &wheel);
        QTRY_VERIFY(!frames.empty());
        QCOMPARE(frames.front().x, 5); QCOMPARE(frames.front().y, -3);
        QCOMPARE(frames.front().wheel, 120);
        QTRY_VERIFY(frames.size() >= 2);
        QCOMPARE(frames.back().x, 0); QCOMPARE(frames.back().y, 0);
        QCOMPARE(frames.back().wheel, 0);
        QMouseEvent press(QEvent::MouseButtonPress, {15, 13}, {15, 13}, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press); QVERIFY(input.frame().left);
        input.setEnabled(false); input.setEnabled(true);
        QVERIFY(!input.frame().left);
        QMouseEvent release(QEvent::MouseButtonRelease, {15, 13}, {15, 13}, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
        QCoreApplication::sendEvent(&window, &press); QVERIFY(input.frame().left);
        QCoreApplication::sendEvent(&window, &release);

        key(&window, QEvent::KeyPress, Qt::Key_S);
        auto other = std::make_unique<QWindow>();
        input.observe(other.get());
        QCoreApplication::sendEvent(other.get(), &activate); input.setEnabled(true);
        key(other.get(), QEvent::KeyPress, Qt::Key_S); QCOMPARE(input.frame().keys, 0u);
        key(other.get(), QEvent::KeyRelease, Qt::Key_S);
        key(other.get(), QEvent::KeyPress, Qt::Key_S); QCOMPARE(input.frame().keys, 2u);
        key(&window, QEvent::KeyPress, Qt::Key_W); QCOMPARE(input.frame().keys, 2u);
        other.reset(); QVERIFY(!input.enabled()); QCOMPARE(input.frame().keys, 0u);
        input.observe(nullptr); input.setEnabled(true); QVERIFY(!input.enabled());
    }
    void officialActionPanel()
    {
        // UI-only identity fixture; it owns no transport or control permission.
        QQmlApplicationEngine engine;
        engine.loadData("import QtQuick\nimport QtQuick.Controls\nimport \".\"\n"
            "ApplicationWindow { width: 1180; height: 680; visible: true; color: \"#071019\";"
            "QtObject { id: source; objectName: \"actionIdentityFixture\";"
            "property int officialActionRobotId: 3; signal stateChanged();"
            "onOfficialActionRobotIdChanged: stateChanged() }"
            "OfficialActions { objectName: \"testOfficialActions\"; backend: source } }",
            QUrl::fromLocalFile(QFileInfo(QStringLiteral(RM27_QML_SOURCE)).absolutePath()
            + QStringLiteral("/ActionHarness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto* popup = window->findChild<QObject*>("testOfficialActions");
        auto* source = window->findChild<QObject*>("actionIdentityFixture");
        QVERIFY(popup && source);
        QSignalSpy submitted(popup, SIGNAL(submitRequested(QString,QVariant,int)));
        QVERIFY(submitted.isValid());
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("visible").toBool());
        auto* choice = findVisual(window->contentItem(), "officialActionChoice");
        QVERIFY(choice);
        QVERIFY(!popup->property("complete").toBool());
        choice->setProperty("currentIndex", 0);
        const auto select = [&](const char* name, int index) {
            auto* field = findVisual(window->contentItem(), QString::fromLatin1(name));
            if (!field) return false;
            field->setProperty("currentIndex", index);
            return QMetaObject::invokeMethod(field, "activated", Q_ARG(int, index));
        };
        QVERIFY(select("officialParameter_shooter", 0));
        QVERIFY(select("officialParameter_chassis", 0));
        QVERIFY(select("officialParameter_sentry", 0)); // Explicitly unchanged, not wire zero.
        QVERIFY(!popup->property("complete").toBool());
        QVERIFY(select("officialParameter_sentry", 1)); // Actual wire zero must remain present.
        QVERIFY(popup->property("complete").toBool());
        QCOMPARE(popup->property("draft").toMap().value("sentry").toInt(), 0);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("official-actions-1180x680.png")));
        QVERIFY(QMetaObject::invokeMethod(popup, "submitDraft"));
        QCOMPARE(submitted.size(), 1);
        QCOMPARE(submitted[0][0].toString(), QStringLiteral("performance"));
        QCOMPARE(submitted[0][1].toMap(), (QVariantMap{{"shooter",0},{"chassis",0},{"sentry",0}}));
        QCOMPARE(submitted[0][2].toInt(), 3);
        QVERIFY(!popup->property("visible").toBool());
        QVERIFY(popup->property("draft").toMap().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        choice->setProperty("currentIndex", 3); // Numeric quantity control.
        QTRY_COMPARE(popup->property("draft").toMap().value("quantity").toInt(), 10);
        QVERIFY(popup->property("complete").toBool());
        source->setProperty("officialActionRobotId", 103);
        QVERIFY(!popup->property("visible").toBool());
        QVERIFY(popup->property("draft").toMap().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QCOMPARE(popup->property("targetRobotId").toInt(), 103);
        source->setProperty("officialActionRobotId", 0); // Disconnect / expiry.
        QVERIFY(!popup->property("visible").toBool());
        QCOMPARE(submitted.size(), 1); // No replay on identity changes.

        TerminalController terminal(false, nullptr, 0, false, 0, false, true);
        QCOMPARE(terminal.officialActionRobotId(), 0);
        QVERIFY(!terminal.requestPerformance(0, 0, -1, 3));
#ifdef RM27_UPSTREAM_BACKEND
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("参数无效")));
#endif
        QVERIFY(!terminal.requestPerformance(1, 2, 0, 3));
#ifdef RM27_UPSTREAM_BACKEND
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("身份未知")));
#endif
        QVERIFY(!terminal.requestAssembly(2, -1, 3));
        QVERIFY(!terminal.requestCommon(1, 11, 3));
        QVERIFY(!terminal.requestDeployment(1, 3));
        QVERIFY(!terminal.requestRune(3));
        QVERIFY(!terminal.requestDart(6, true, true, 3));
        QVERIFY(!terminal.requestSentry(2, 3));
        QVERIFY(!terminal.requestAirSupport(4, 3));
        QVERIFY(!terminal.controlsArmed());

        QQmlApplicationEngine mainEngine;
        mainEngine.rootContext()->setContextProperty("terminal", &terminal);
        mainEngine.rootContext()->setContextProperty("initialVideo", QUrl());
        mainEngine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        mainEngine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!mainEngine.rootObjects().isEmpty());
        auto* mainWindow = qobject_cast<QQuickWindow*>(mainEngine.rootObjects().first());
        QVERIFY(mainWindow && QTest::qWaitForWindowExposed(mainWindow));
        auto* start = mainWindow->findChild<QObject*>("startPanel");
        QVERIFY(start && QMetaObject::invokeMethod(start, "requestOfficialActions"));
        auto* realPopup = mainWindow->findChild<QObject*>("officialActionsPopup");
        QVERIFY(realPopup);
        QTRY_VERIFY(realPopup->property("visible").toBool());
        QVERIFY(terminal.uiOccupied());
        QVERIFY(!mainWindow->property("startPanelVisible").toBool());
        auto* submit = findVisual(mainWindow->contentItem(), "officialActionSubmit");
        QVERIFY(submit && !submit->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(realPopup, "close"));
        QVERIFY(!terminal.uiOccupied());
        QVERIFY(!terminal.controlsArmed());
    }
#ifdef RM27_UPSTREAM_BACKEND
    void upstreamHevcVideo()
    {
        QFile file(QString::fromUtf8(qgetenv("RM27_TEST_HEVC")));
        if (file.fileName().isEmpty()) QSKIP("No generated HEVC test pattern selected");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto stream = file.readAll();
        QList<qsizetype> starts;
        for (qsizetype i = 0; i + 5 < stream.size(); ++i) {
            const int prefix = stream.mid(i, 4) == QByteArray::fromHex("00000001") ? 4
                : stream.mid(i, 3) == QByteArray::fromHex("000001") ? 3 : 0;
            if (!prefix) continue;
            if ((static_cast<unsigned char>(stream[i + prefix]) >> 1) == 35) starts.append(i);
            i += prefix - 1;
        }
        QVERIFY(starts.size() >= 30 && starts.first() == 0);
        starts.append(stream.size());
        QList<QByteArray> frames;
        for (int i = 0; i + 1 < starts.size(); ++i) frames.append(stream.mid(starts[i], starts[i + 1] - starts[i]));
        QUdpSocket reservation, sender, wrongSource;
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const auto port = reservation.localPort();
        reservation.close();
        QVERIFY(wrongSource.bind(QHostAddress("127.0.0.2"), 0));
        TerminalController terminal(false, nullptr, 0, false, 0, false, true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* output = window->findChild<QObject*>("localVideoOutput");
        QVERIFY(output);
        auto* sink = output->property("videoSink").value<QVideoSink*>();
        QVERIFY(sink);
        QVERIFY(terminal.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, true));
        // Same bind must fail; never share another receiver's endpoint.
        rm27::video::UpstreamVideo contender;
        QVERIFY(!contender.start(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0));
        quint16 sequence = 0;
        int index = 0;
        unsigned base = 0;
        const auto sendFrame = [&](const QByteArray& frame, bool drop = false) {
            const int slices = int((frame.size() + 1391) / 1392);
            for (int slice = slices - 1; slice >= 0; --slice) {
                if (drop && slice == 0) continue;
                QByteArray packet(8, '\0');
                qToBigEndian(sequence, packet.data());
                qToBigEndian(quint16(slice + base), packet.data() + 2);
                qToBigEndian(quint32(frame.size()), packet.data() + 4);
                packet.append(frame.mid(slice * 1392, 1392));
                sender.writeDatagram(packet, QHostAddress::LocalHost, port);
                if (slice == slices - 1) sender.writeDatagram(packet, QHostAddress::LocalHost, port);
            }
            ++sequence;
        };
        // A non-keyframe at startup cannot create a current image.
        sendFrame(frames[1]);
        QByteArray forbidden(9, '\0');
        qToBigEndian(quint32(1), forbidden.data() + 4);
        wrongSource.writeDatagram(forbidden, QHostAddress::LocalHost, port);
        sender.writeDatagram(QByteArray(1401, 'x'), QHostAddress::LocalHost, port);
        QTest::qWait(200);
        QCOMPARE(terminal.decodedFrames(), 0);
        QTimer transmit;
        connect(&transmit, &QTimer::timeout, &sender, [&] { sendFrame(frames[index++ % frames.size()]); });
        transmit.start(33);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 20, 6000);
        QVERIFY(terminal.videoConnected() && !terminal.refereeFresh() && !terminal.controlsArmed());
        QVERIFY(terminal.videoDiagnosticsLabel().contains(QStringLiteral("HEVC 收包")));
        QVERIFY(!terminal.videoDiagnosticsLabel().contains(QStringLiteral("UDP 探针")));
        const auto picture = sink->videoFrame().toImage();
        QCOMPARE(picture.size(), QSize(320, 180));
        QSet<QRgb> colors;
        for (int y = 15; y < picture.height(); y += 30)
            for (int x = 15; x < picture.width(); x += 30) colors.insert(picture.pixel(x, y));
        QVERIFY(colors.size() > 4);
        QVERIFY(output->property("visible").toBool());
        QImage rendered;
        const auto renderedPattern = [&] {
            rendered = window->grabWindow();
            if (rendered.isNull()) return false;
            int brightPixels = 0;
            for (int y = 300; y < 600; y += 40)
                for (int x = 400; x < 1200; x += 40) {
                    const auto color = rendered.pixelColor(x, y);
                    if (color.saturation() > 180 && color.value() > 180) ++brightPixels;
                }
            return brightPixels > 10;
        };
        QTRY_VERIFY_WITH_TIMEOUT(renderedPattern(), 3000);
        QVERIFY(rendered.save(rm27::app::previewPath("rm26-upstream-hevc.png")));
        const auto transport = terminal.refereeDiagnostics().value("video").toMap().value("transport").toMap();
        QVERIFY(transport.value("rejectedPackets").toULongLong() >= 2);
        QVERIFY(transport.value("duplicates").toULongLong() > 0);
        QVERIFY(transport.value("imageQueueDepth").toInt() <= 1);
        transmit.stop();
        sendFrame(frames[0], true); // Incomplete AU must not be decoded.
        QTest::qWait(250);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.videoStale(), 1800);
        QVERIFY(!sink->videoFrame().isValid());
        const auto before = terminal.decodedFrames();
        sequence = 0; index = 0;
        transmit.start(33);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > before + 4, 4000);
        QVERIFY(terminal.videoConnected());
        transmit.stop();
        // Restart a new session with explicit one-based fragments.
        QVERIFY(terminal.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 1, false));
        QVERIFY(!sink->videoFrame().isValid());
        // Exercise pairing with actual decoded artificial HEVC, never hardware evidence.
        QVERIFY(terminal.beginPairing("global", 3));
        QVERIFY(terminal.confirmExternalPairing(terminal.pairingDiagnostics().value("session").toULongLong()));
        QVERIFY(!terminal.pairingDiagnostics().value("videoFresh").toBool());
        sequence = 0; index = 0; base = 1;
        transmit.start(33);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 4, 4000);
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("video_review"));
        QVERIFY(!terminal.pairingDiagnostics().value("hardwareVerified").toBool());
        rm27::historical::rm2026::Snapshot identity;
        identity.selfConfig = rm27::historical::rm2026::Received<rm27::historical::rm2026::SelfConfig>{
            {}, std::chrono::steady_clock::now()};
        identity.selfConfig->value.robotId = 3;
        terminal.applyHistoricalSnapshot(identity, true);
        QCOMPARE(terminal.pairingDiagnostics().value("identityStatus").toString(), QStringLiteral("reported_match"));
        identity.selfConfig->value.robotId = 103;
        terminal.applyHistoricalSnapshot(identity, true);
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("identity_mismatch"));
        QVERIFY(!terminal.upstreamVideoActive() && !sink->videoFrame().isValid());
        terminal.enterOfflineDrill();
        QVERIFY(!terminal.upstreamVideoActive() && !sink->videoFrame().isValid());
        const auto offlineFrames = terminal.decodedFrames();
        QTest::qWait(250);
        QCOMPARE(terminal.decodedFrames(), offlineFrames);
        transmit.stop();
    }
#endif
#ifdef RM27_UPSTREAM_BACKEND
    void upstreamIdentityProfile()
    {
        resetIsolatedSettings();
        QTcpServer broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost, 0));
        TerminalController terminal(false, nullptr, 0, false, 0, false, true);
        QVERIFY(!terminal.configureRm2026("localhost", broker.serverPort(), 3, true));
        QVERIFY(!terminal.configureRm2026("127.0.0.1", broker.serverPort(), 5, true));
        QVERIFY(terminal.configureRm2026("127.0.0.1", broker.serverPort(), 3, true));
        QCOMPARE(terminal.refereeDiagnostics().value("client_id").toString(), QString("3"));
        QCOMPARE(terminal.ownRobotPreference(), QString("infantry3"));
        QCOMPARE(terminal.ownTeamPreference(), QString("red"));
        QVERIFY(terminal.hudSnapshot().isEmpty()); // Preference is not a received report.
        QTRY_VERIFY(broker.hasPendingConnections());
        terminal.connection()->setOwnTeam("blue");
        QVERIFY(terminal.applyConnectionProfile());
        QCOMPARE(terminal.refereeDiagnostics().value("client_id").toString(), QString("103"));
        // The same profile leaves the current connection generation intact.
        QVERIFY(terminal.applyConnectionProfile());
        QCOMPARE(terminal.refereeDiagnostics().value("client_id").toString(), QString("103"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* selfLabel = window->findChild<QQuickItem*>("selfRobotLabel");
        QVERIFY(selfLabel);
        QCOMPARE(selfLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));
        auto* startPanel = window->findChild<QQuickItem*>("startPanel");
        QVERIFY(startPanel);
        QVERIFY(QMetaObject::invokeMethod(startPanel, "enterOffline"));
        QVERIFY(terminal.offlineDrill());
        QVERIFY(!window->property("startPanelVisible").toBool());
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(terminal.linkBannerText().contains(QStringLiteral("离线演练")));
        QTest::qWait(250);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        terminal.enterLocalPlayback();
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        QVERIFY(terminal.linkBannerText().contains(QStringLiteral("本地视频回放")));
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(!terminal.refereeFresh());
        QVERIFY(terminal.hudSnapshot().isEmpty());
        // Returning from replay explicitly starts a new data session.
        QVERIFY(terminal.applyConnectionProfile());
        QCOMPARE(terminal.refereeDiagnostics().value("client_id").toString(), QString("103"));
    }
#endif
    void init()
    {
        resetIsolatedSettings();
    }

    void unsupportedFormalProfile()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        auto* settings = terminal.connection();
        QSignalSpy applied(&terminal, &TerminalController::connectionProfileApplied);
        for (const QString& endpoint : {QStringLiteral("refereeHost"),
                 QStringLiteral("videoBindAddress"), QStringLiteral("mqttHost")}) {
            settings->setRefereeHost(QStringLiteral("127.0.0.1"));
            settings->setVideoBindAddress(QStringLiteral("127.0.0.1"));
            settings->setMqttHost(QStringLiteral("127.0.0.1"));
            settings->setUseMqtt(true);
            QVERIFY(settings->setProperty(endpoint.toUtf8().constData(), QStringLiteral("192.168.1.10")));
            QCOMPARE(rm27::validateProfile(settings->profile()), rm27::ProfileIssue::Ok);
            QVERIFY(!settings->formalLinkReady());
            QVERIFY(!settings->entryAllowed());
            QVERIFY(settings->validationMessage().contains(QStringLiteral("未接入")));
            QVERIFY(!terminal.applyConnectionProfile());
            QVERIFY(applied.isEmpty());
            QVERIFY(!terminal.controlsArmed());
        }
        settings->setRefereeHost(QStringLiteral("203.0.113.7"));
        QVERIFY(!settings->entryAllowed()); // A sentinel cannot hide an external MQTT endpoint.
        settings->setMqttHost(QStringLiteral("127.0.0.1"));
        settings->setUseMqtt(false);
        QVERIFY(settings->entryAllowed());
        QVERIFY(terminal.applyConnectionProfile());
        QVERIFY(terminal.offlineDrill());
        settings->setRefereeHost(QStringLiteral("127.0.0.1"));
        QVERIFY(settings->entryAllowed());
        QVERIFY(terminal.applyConnectionProfile());
        QVERIFY(!terminal.offlineDrill());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));

        // Loading a saved external profile must not silently start loopback receivers either.
        QUdpSocket refereeReservation, probeReservation;
        QVERIFY(refereeReservation.bind(QHostAddress::LocalHost, 0));
        QVERIFY(probeReservation.bind(QHostAddress::LocalHost, 0));
        settings->setRefereeHost(QStringLiteral("192.168.1.10"));
        settings->setRefereePort(refereeReservation.localPort());
        settings->setVideoProbePort(probeReservation.localPort());
        refereeReservation.close();
        probeReservation.close();
        QVERIFY(settings->save());
        TerminalController restored(false);
        QVERIFY(refereeReservation.bind(QHostAddress::LocalHost, settings->refereePort(), QUdpSocket::DontShareAddress));
        QVERIFY(probeReservation.bind(QHostAddress::LocalHost, settings->videoProbePort(), QUdpSocket::DontShareAddress));
        QVERIFY(!restored.refereeConnected());
        QVERIFY(!restored.videoConnected());
        QVERIFY(!restored.controlsArmed());
    }

    void guiHeartbeatLateTick()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        QSignalSpy ticks(&terminal, &TerminalController::performanceChanged);
        QTRY_VERIFY_WITH_TIMEOUT(!ticks.isEmpty(), 500);
        const int stalls = terminal.guiStallCount();
        // Right after a 200 ms tick, blocking 400 ms makes the next tick ~200 ms late.
        QThread::msleep(400);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.guiStallCount() > stalls, 1000);
        QVERIFY(terminal.guiPeakJitterMs() > 150);
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.guiThreadStalled(), 1000);
    }

    void videoDiagnosticSources()
    {
#ifdef RM27_UPSTREAM_BACKEND
        TerminalController hevc(false, nullptr, 0, false, 0, false, true);
        QUdpSocket reservation;
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const auto port = reservation.localPort();
        reservation.close();
        QVERIFY(hevc.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, true));
        QVERIFY(hevc.videoDiagnostics().value("stage").toString().contains("HEVC"));
        QVERIFY(!hevc.videoDiagnostics().value("packets").isValid());
        QVERIFY(!hevc.videoDiagnosticsLabel().contains(QStringLiteral("UDP 探针")));
        QVideoSink sink;
        hevc.observeVideoSink(&sink);
        sink.setVideoFrame(QVideoFrame(QImage(8, 8, QImage::Format_RGB32)));
        QCOMPARE(hevc.videoDiagnostics().value("stage").toString(), QStringLiteral("HEVC 已出帧"));
        QVERIFY(!hevc.videoDiagnostics().value("endToEndMeasured").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(hevc.videoDiagnostics().value("stage").toString()
            .contains(QStringLiteral("过期")), 1500);
#endif
        TerminalController local(false, nullptr, testRefereePort);
        local.enterLocalPlayback();
        QVERIFY(!local.videoDiagnostics().value("packets").isValid());
        QVERIFY(local.videoDiagnosticsLabel().contains(QStringLiteral("本地文件回放")));
        local.enterSimulationVideo();
        QVERIFY(!local.videoDiagnostics().value("packets").isValid());
        QVERIFY(local.videoDiagnosticsLabel().contains(QStringLiteral("MPEG-TS")));
    }

    void pairingWorkflow()
    {
        // Artificial pixels verify the backend wiring, not physical pairing or camera identity.
        TerminalController terminal(false, nullptr, 0, false, 0, false, true);
#ifdef RM27_UPSTREAM_BACKEND
        QUdpSocket reservation;
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const auto port = reservation.localPort();
        reservation.close();
        const auto configureVideo = [&] {
            return terminal.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, false);
        };
        QVERIFY(!terminal.beginPairing("global", 3)); // No active native source.
        QVERIFY(configureVideo());
        QVideoSink sink, replacement;
        terminal.observeVideoSink(&sink);
        QVERIFY(!terminal.beginPairing("invalid-region", 3));
        QVERIFY(terminal.beginPairing("global", 3));
        auto session = terminal.pairingDiagnostics().value("session").toULongLong();
        sink.setVideoFrame(QVideoFrame(QImage(8, 8, QImage::Format_RGB32)));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("external_required"));
        QVERIFY(!terminal.pairingDiagnostics().value("operatorConfirmed").toBool());
        QVERIFY(terminal.confirmExternalPairing(session));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("video_review"));
        QVERIFY(!terminal.pairingDiagnostics().value("hardwareVerified").toBool());
        QVERIFY(!terminal.pairingDiagnostics().value("controlAllowed").toBool());
        QVERIFY(!terminal.controlsArmed());
        sink.setVideoFrame({});
        QVERIFY(!terminal.pairingDiagnostics().value("videoFresh").toBool());
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("video_required"));
        sink.setVideoFrame(QVideoFrame(QImage(8, 8, QImage::Format_RGB32)));
        QVERIFY(!terminal.pairingDiagnostics().value("reportedRobotId").isValid());
        QCOMPARE(terminal.pairingDiagnostics().value("identityStatus").toString(), QStringLiteral("unknown"));
        terminal.cancelPairing();
        QVERIFY(!terminal.confirmExternalPairing(session));
        QVERIFY(terminal.beginPairing("japan", 103));
        QVERIFY(!terminal.reportPairingIdentity(session, 3));
        session = terminal.pairingDiagnostics().value("session").toULongLong();
        QVERIFY(!terminal.pairingDiagnostics().value("videoFresh").toBool());
        QVERIFY(terminal.reportPairingIdentity(session, 3));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("identity_mismatch"));
        QVERIFY(!terminal.confirmExternalPairing(session));
        QVERIFY(terminal.reportPairingIdentity(session, 103));
        QVERIFY(terminal.confirmExternalPairing(session));
        terminal.observeVideoSink(&replacement);
        sink.setVideoFrame(QVideoFrame(QImage(9, 9, QImage::Format_RGB32)));
        QVERIFY(!terminal.pairingDiagnostics().value("videoFresh").toBool());
        replacement.setVideoFrame(QVideoFrame(QImage(10, 10, QImage::Format_RGB32)));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("video_review"));
        terminal.observeVideoSink(&sink);
        QVERIFY(!terminal.pairingDiagnostics().value("videoFresh").toBool());
        const auto frameCount = terminal.decodedFrames();
        replacement.setVideoFrame(QVideoFrame(QImage(10, 10, QImage::Format_RGB32)));
        QCOMPARE(terminal.decodedFrames(), frameCount);
        sink.setVideoFrame(QVideoFrame(QImage(10, 10, QImage::Format_RGB32)));
        QCOMPARE(terminal.decodedFrames(), frameCount + 1);
        QVERIFY(terminal.pairingDeviceDisconnected(session));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("disconnected"));
        QVERIFY(!terminal.confirmExternalPairing(session));
        QVERIFY(!terminal.upstreamVideoActive());
        QVERIFY(configureVideo());
        QVERIFY(terminal.beginPairing("global", 3, 20));
        session = terminal.pairingDiagnostics().value("session").toULongLong();
        QTest::qWait(30);
        QVERIFY(!terminal.confirmExternalPairing(session));
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("timed_out"));
        QVERIFY(terminal.beginPairing("global", 3));
        terminal.enterLocalPlayback();
        QCOMPARE(terminal.pairingDiagnostics().value("phase").toString(), QStringLiteral("cancelled"));
        QVERIFY(!terminal.beginPairing("global", 3));
        TerminalController synthetic(false, nullptr, 0, false, 0, true);
        QVERIFY(synthetic.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, true));
        QVERIFY(!synthetic.beginPairing("global", 3));
        TerminalController normal(false, nullptr, testRefereePort);
        QVERIFY(!normal.beginPairing("global", 3));
        synthetic.enterLocalPlayback();
        TerminalController testSource(false, nullptr, 0, false, 0, false, true);
        QVERIFY(testSource.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, true));
        QVERIFY(!testSource.beginPairing("global", 3));
        testSource.enterLocalPlayback();
        // The real snapshot path, not the requested client ID, supplies identity.
        TerminalController reports(false, nullptr, 0, false, 0, false, true);
        QVERIFY(reports.configureRm2026Video(QHostAddress::LocalHost, port, QHostAddress::LocalHost, 0, false));
        QVERIFY(reports.beginPairing("global", 3));
        using namespace rm27::historical::rm2026;
        Snapshot snapshot;
        snapshot.selfConfig = Received<SelfConfig>{{3, 1000, 1}, std::chrono::steady_clock::now()};
        reports.applyHistoricalSnapshot(snapshot, true);
        QCOMPARE(reports.pairingDiagnostics().value("identityStatus").toString(), QStringLiteral("reported_match"));
        snapshot.selfConfig->at -= std::chrono::seconds(2);
        reports.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(!reports.pairingDiagnostics().value("reportedRobotId").isValid());
        QVERIFY(reports.upstreamVideoActive());
        snapshot.selfConfig->at = std::chrono::steady_clock::now();
        reports.applyHistoricalSnapshot(snapshot, false);
        QVERIFY(!reports.pairingDiagnostics().value("reportedRobotId").isValid());
        QVERIFY(reports.upstreamVideoActive());
        snapshot.selfConfig->value.robotId = 103;
        reports.applyHistoricalSnapshot(snapshot, true);
        QCOMPARE(reports.pairingDiagnostics().value("phase").toString(), QStringLiteral("identity_mismatch"));
        QVERIFY(!reports.upstreamVideoActive());
        QVERIFY(!reports.controlsArmed());
#else
        QSKIP("Native HEVC backend not built");
#endif
    }


    void historicalGamePresentation()
    {
        using namespace rm27::historical::rm2026;
        TerminalController terminal(false, nullptr, 0, false, 0, true);
        Snapshot snapshot;
        Game game;
        game.phase = Phase::Playing;
        game.currentRound = 0;
        game.totalRounds = 0;
        game.redScore = 0;
        game.blueScore = 0;
        game.countdownSec = 0;
        game.elapsedSec = -1;
        game.paused = false;
        snapshot.game = Received<Game>{game, std::chrono::steady_clock::now()};
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.alertText().contains(QStringLiteral("局 0/0 · 红 0:0 蓝 · 剩余 0 s · 已过 -1 s · 未暂停")));
        QCOMPARE(terminal.phaseLabel(), QStringLiteral("比赛中"));
        const QString reported = terminal.alertText();
        QTest::qWait(250);
        QCOMPARE(terminal.alertText(), reported); // No local countdown or phase advance.

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto* strip = window->findChild<QQuickItem*>("topStatusStrip");
        auto* toggle = window->findChild<QQuickItem*>("topStatusToggle");
        QVERIFY(strip && toggle);
        QQuickItem* alert = nullptr;
        for (auto* item : strip->findChildren<QQuickItem*>())
            if (item->property("text").toString() == reported) alert = item;
        QVERIFY(alert);
        QQmlExpression tooltip(qmlContext(toggle), toggle, QStringLiteral("ToolTip.text"));
        QCOMPARE(tooltip.evaluate().toString(), reported);
        QVERIFY(!tooltip.hasError());
        QVERIFY(!reported.contains('\n')); // The existing 34 px status row stays one line.
        QVERIFY(window->setProperty("topStatusExpanded", true));
        QTest::qWait(200);
        QCOMPARE(alert->property("lineCount").toInt(), 1);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("historical-game-1600x900.png")));
        QVERIFY(window->setProperty("topStatusExpanded", false));
        window->resize(1180, 680);
        QTest::qWait(200);
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            QTest::mouseMove(window, toggle->mapToScene(toggle->boundingRect().center()).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(toggle->property("hovered").toBool(), 1000);
            QQmlExpression visible(qmlContext(toggle), toggle, QStringLiteral("ToolTip.visible"));
            QTRY_VERIFY_WITH_TIMEOUT(visible.evaluate().toBool(), 1000);
            snapshot.game->at = std::chrono::steady_clock::now();
            terminal.applyHistoricalSnapshot(snapshot, true);
            QTest::qWait(100);
            const QString preview = rm27::app::previewPath("historical-game-tooltip-1180x680.png");
            QVERIFY(window->grabWindow().save(preview));
            qInfo().noquote() << "HISTORICAL_GAME_PREVIEW" << preview;
        } else {
            qInfo() << "Native cursor hover is checked separately with the Windows backend";
        }

        // Preserve the original widths and signed times instead of narrowing to UI integers.
        game.currentRound = 256;
        game.totalRounds = 300;
        game.redScore = 70000;
        game.blueScore = 4294967295U;
        game.countdownSec = 2147483647;
        game.elapsedSec = -2147483647 - 1;
        snapshot.game->value = game;
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.alertText().contains(QStringLiteral("局 256/300 · 红 70000:4294967295 蓝 · 剩余 2147483647 s · 已过 -2147483648 s")));

        // A replacement sample cannot inherit any previously reported optional values.
        game = {};
        game.paused = true;
        snapshot.game = Received<Game>{game, std::chrono::steady_clock::now()};
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.alertText().contains(QStringLiteral("局 --/-- · 红 --:-- 蓝 · 剩余 -- s · 已过 -- s · 已暂停")));
        QCOMPARE(terminal.phaseLabel(), QStringLiteral("未知"));
        game.paused.reset();
        snapshot.game->value = game;
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.alertText().contains(QStringLiteral("暂停未知")));
        snapshot.game.reset(); // Expiry of GameStatus is independent of another fresh topic.
        snapshot.economy = Received<Economy>{{10}, std::chrono::steady_clock::now()};
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.refereeFresh());
        QVERIFY(terminal.alertText().contains(QStringLiteral("比赛数据未知 / 已过期")));
        QVERIFY(!terminal.alertText().contains(QStringLiteral("红 ")));
        QCOMPARE(alert->property("text").toString(), terminal.alertText());
        snapshot.game = Received<Game>{game, std::chrono::steady_clock::now()};
        terminal.applyHistoricalSnapshot(snapshot, false);
        QVERIFY(!terminal.refereeConnected());
        QVERIFY(!terminal.alertText().contains(QStringLiteral("局 ")));
        game.currentRound = 2;
        snapshot.game->value = game;
        terminal.applyHistoricalSnapshot(snapshot, true);
        QVERIFY(terminal.alertText().contains(QStringLiteral("局 2/-- · 红 --:-- 蓝")));
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(!terminal.videoConnected());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
    }

    void historicalHudPresentation()
    {
        using namespace rm27::historical::rm2026;
        TerminalController terminal(false, nullptr, 0, false, 0, true);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(!window->property("startPanelVisible").toBool());
        auto* hud = window->findChild<QQuickItem*>("hudOverlay");
        QVERIFY(hud);
        const auto text = [&](const char* name) {
            auto* item = findVisual(window->contentItem(), QString::fromLatin1(name));
            return item ? item->property("text").toString() : QStringLiteral("ITEM NOT FOUND");
        };
        const auto now = std::chrono::steady_clock::now();
        Snapshot snapshot;
        snapshot.selfConfig = Received<SelfConfig>{{101, 1000, 3, Team::Blue}, now};
        snapshot.selfDynamic = Received<SelfDynamic>{{850, 80, 20.0F, 24.0F}, now};
        snapshot.selfDynamic->value.bufferEnergy = 4294967295U;
        snapshot.selfDynamic->value.chassisEnergy = 123;
        Units units;
        units.ally.base.health = 5000;
        units.enemy.base.health = 4000;
        units.ally.outpost.life = OutpostLife::Alive;
        units.enemy.outpost.life = OutpostLife::Destroyed;
        for (auto& robot : units.ally.robots) robot.health = 900 + robot.roleId;
        for (auto& robot : units.enemy.robots) robot.health = 700 + robot.roleId;
        snapshot.units = Received<Units>{units, now};
        snapshot.economy = Received<Economy>{{100}, now};
        Game game;
        game.phase = Phase::Playing;
        snapshot.game = Received<Game>{game, now};
        EventReport event;
        event.id = 9;
        event.kind = EventKind::DartHit;
        event.param = "<b>raw parameter</b>";
        snapshot.recentEvents = {{1, event}, {2, event}};
        terminal.applyHistoricalSnapshot(snapshot, true);
        QCOMPARE(terminal.eventMessages().size(), 2);
        QCOMPARE(terminal.eventMessages()[0].toMap().value("id").toString(), QStringLiteral("1"));
        QCOMPARE(terminal.eventMessages()[1].toMap().value("id").toString(), QStringLiteral("2"));
        QVERIFY(terminal.eventMessages()[0].toMap().value("text").toString().contains(QStringLiteral("飞镖命中")));
        QVERIFY(terminal.eventMessages()[0].toMap().value("text").toString().contains(QStringLiteral("<b>raw parameter</b>")));
        terminal.applyHistoricalSnapshot(snapshot, true);
        QCOMPARE(terminal.eventMessages().size(), 2); // Polling cannot duplicate history.
        auto* eventList = window->findChild<QQuickItem*>("eventList");
        QVERIFY(eventList);
        QTRY_COMPARE(eventList->property("count").toInt(), 2);
        auto* eventText = findVisual(window->contentItem(), QStringLiteral("eventMessageText"));
        QTRY_VERIFY(eventText = findVisual(window->contentItem(), QStringLiteral("eventMessageText")));
        QCOMPARE(eventText->property("textFormat").toInt(), 0); // QQuickText::PlainText.
        QTRY_COMPARE(text("friendly_baseHp"), QStringLiteral("我方 · 蓝方 基地 5000"));
        QCOMPARE(text("enemy_baseHp"), QStringLiteral("敌方 · 红方 基地 4000"));
        QCOMPARE(text("friendly_outpostStatus"), QStringLiteral("前哨 存活"));
        QCOMPARE(text("enemy_outpostStatus"), QStringLiteral("前哨 摧毁"));
        QCOMPARE(text("coinReadout"), QStringLiteral("金币 100"));
        QCOMPARE(text("friendlySlotHp0"), QStringLiteral("907"));
        QCOMPARE(text("enemySlotHp5"), QStringLiteral("701"));
        QCOMPARE(text("friendlySlotHp1"), QStringLiteral("--")); // No aerial HP in this protocol.
        QCOMPARE(text("selfRobotLabel"), QStringLiteral("本车 · 英雄"));
        QCOMPARE(terminal.hpLabel(), QStringLiteral("850 / 1000"));
        QCOMPARE(terminal.bufferEnergyLabel(), QStringLiteral("4294967295 J"));
        QCOMPARE(window->findChild<QObject*>("bufferEnergyReadout")->property("value").toString(), QStringLiteral("4294967295 J"));
        QCOMPARE(window->findChild<QObject*>("heatReadout")->property("value").toString(), QStringLiteral("20"));
        QCOMPARE(terminal.barrelHeatLabel(), QStringLiteral("20"));
        QCOMPARE(terminal.chassisPowerLabel(), QStringLiteral("--"));
        QCOMPARE(terminal.ammoLabel(), QStringLiteral("--"));
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("blue"));
        QCOMPARE(window->findChild<QObject*>("friendly_baseFill")->property("ratio").toDouble(), 0.0);
        QCOMPARE(terminal.phaseLabel(), QStringLiteral("比赛中"));
        QVERIFY(terminal.mapSnapshot().isEmpty());
        QVERIFY(!terminal.videoConnected());
        QVERIFY(!terminal.controllerConnected());
        QVERIFY(!terminal.applyConnectionProfile());
        terminal.toggleFallbackMode();
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("historical-hud-1600x900.png")));

        // One topic can disappear while another stays fresh; only its fields clear.
        snapshot.units.reset();
        snapshot.selfDynamic.reset();
        terminal.applyHistoricalSnapshot(snapshot, true);
        QTRY_COMPARE(text("friendly_baseHp"), QStringLiteral("我方 · 蓝方 基地 --"));
        QCOMPARE(text("friendlySlotHp0"), QStringLiteral("--"));
        QCOMPARE(text("coinReadout"), QStringLiteral("金币 100"));
        QCOMPARE(terminal.hpLabel(), QStringLiteral("-- / --"));
        QVERIFY(!terminal.hudSnapshot().contains("allowedShots"));
        QCOMPARE(terminal.bufferEnergyLabel(), QStringLiteral("--"));
        QCOMPARE(window->findChild<QObject*>("bufferEnergyReadout")->property("value").toString(), QStringLiteral("--"));
        QCOMPARE(terminal.barrelHeatLabel(), QStringLiteral("--"));

        terminal.applyHistoricalSnapshot({}, true);
        QTRY_COMPARE(text("coinReadout"), QStringLiteral("金币 --"));
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("unknown"));
        QCOMPARE(text("selfRobotLabel"), QStringLiteral("本车 · 机型待确认"));
        QVERIFY(terminal.refereeConnected());
        QVERIFY(!terminal.refereeFresh());
        terminal.applyHistoricalSnapshot(snapshot, false); // Disconnected ignores leftover input.
        QVERIFY(!terminal.refereeConnected());
        QVERIFY(terminal.hudSnapshot().isEmpty());
        QVERIFY(terminal.eventMessages().isEmpty());

        snapshot.selfConfig = Received<SelfConfig>{{1, 1000, 2, Team::Red}, now};
        snapshot.selfDynamic = Received<SelfDynamic>{{0, 0, {}, {}}, now};
        snapshot.selfDynamic->value.bufferEnergy = 0;
        snapshot.economy = Received<Economy>{{0}, now};
        units.enemy.outpost.life = OutpostLife::Unknown;
        units.enemy.base.health = 0;
        snapshot.units = Received<Units>{units, now};
        terminal.applyHistoricalSnapshot(snapshot, true);
        QTRY_COMPARE(text("coinReadout"), QStringLiteral("金币 0"));
        QCOMPARE(terminal.hpLabel(), QStringLiteral("0 / 1000"));
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("red"));
        QCOMPARE(text("enemy_baseHp"), QStringLiteral("敌方 · 蓝方 基地 0"));
        QCOMPARE(text("enemy_outpostStatus"), QStringLiteral("前哨 未知"));
        QCOMPARE(terminal.hudSnapshot().value("allowedShots").toUInt(), 0U);
        QCOMPARE(terminal.bufferEnergyLabel(), QStringLiteral("0 J"));
        QCOMPARE(window->findChild<QObject*>("bufferEnergyReadout")->property("value").toString(), QStringLiteral("0 J"));
        QVERIFY(!terminal.hudSnapshot().contains("speedMps"));
        QVERIFY(!terminal.hudSnapshot().contains("yawDeg"));
        QVERIFY(!terminal.controlsArmed());
        snapshot.recentEvents.clear();
        event.id = -5;
        event.kind = EventKind::Unknown;
        event.param = std::string(10000, 'x');
        for (unsigned sequence = 1; sequence <= 40; ++sequence)
            snapshot.recentEvents.push_back({sequence, event});
        terminal.applyHistoricalSnapshot(snapshot, true);
        QCOMPARE(terminal.eventMessages().size(), 24);
        QCOMPARE(terminal.eventMessages().first().toMap().value("id").toString(), QStringLiteral("17"));
        const auto longText = terminal.eventMessages().last().toMap().value("text").toString();
        QVERIFY(longText.contains(QStringLiteral("未知事件 [-5]")));
        QVERIFY(longText.endsWith(QStringLiteral("…")) && longText.size() < 600);
        QVERIFY(!terminal.controlsArmed());
    }

    void simulationUdpVideo()
    {
        QFile fixture(qEnvironmentVariable("RM27_TEST_TS"));
        if (!fixture.open(QIODevice::ReadOnly)) QSKIP("Set RM27_TEST_TS to an MPEG-TS video sample");
        const QByteArray transportStream = fixture.readAll();
        QVERIFY(transportStream.size() >= 1316);
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", true);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* startPanel = window->findChild<QQuickItem*>("startPanel");
        QVERIFY(startPanel);
        QVERIFY(QMetaObject::invokeMethod(startPanel, "enterTerminal"));
        QVERIFY(!window->property("startPanelVisible").toBool());
        auto* videoStateText = window->findChild<QQuickItem*>("videoStateText");
        QVERIFY(videoStateText);
        // 发送端在首帧之前就断流：不能永远停留在“等待图传”。
        QTRY_VERIFY_WITH_TIMEOUT(videoStateText->property("text").toString()
                                    .contains(QStringLiteral("首帧超时")), 2500);
        QCOMPARE(terminal.decodedFrames(), 0);
        QVERIFY(!terminal.videoConnected());
        QVERIFY(terminal.videoStale());
        QVERIFY(!videoStateText->property("text").toString().contains(QStringLiteral("旧帧")));
        QCOMPARE(videoStateText->property("color").value<QColor>(), QColor("#FF5F68"));
        auto* videoFallbackText = window->findChild<QQuickItem*>("videoFallbackText");
        QVERIFY(videoFallbackText && videoFallbackText->isVisible());
        QVERIFY(videoFallbackText->property("text").toString().contains(QStringLiteral("首帧超时")));
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("video-first-frame-timeout.png")));
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());

        QUdpSocket sender;
        // Referee and actual TS failures are independent, not a synthetic link flag.
        quint64 refereeSequence = 0;
        QTimer refereeTransmit;
        refereeTransmit.setInterval(100);
        connect(&refereeTransmit, &QTimer::timeout, &refereeTransmit, [&] {
            auto document = QJsonDocument::fromJson(R"json({
                "channel":"rm27.sim.v1.referee", "sequence":0,
                "sent_at_unix_ms":1789999999000, "phase":"match",
                "telemetry":{"hp":920,"max_hp":1000,"chassis_power_w":80,
                "buffer_energy_j":48,"barrel_heat":0,"ammo":100,
                "latency_ms":0,"packet_loss_pct":0}
            })json");
            auto packet = document.object();
            packet.insert(QStringLiteral("sequence"), double(++refereeSequence));
            sender.writeDatagram(QJsonDocument(packet).toJson(QJsonDocument::Compact),
                                 QHostAddress::LocalHost, testRefereePort);
        });
        refereeTransmit.start();
        qsizetype offset = 0;
        QTimer transmit;
        transmit.setInterval(21); // About 500 kbps for the generated 10-second sample.
        connect(&transmit, &QTimer::timeout, &transmit, [&] {
            if (offset + 1316 > transportStream.size()) offset = 0;
            sender.writeDatagram(transportStream.constData() + offset, 1316,
                                 QHostAddress::LocalHost, testVideoDecodePort);
            offset += 1316;
        });
        transmit.start();
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 15000);
        QVERIFY(terminal.videoConnected());
        QVERIFY(!terminal.videoStale());
        QVERIFY(!videoFallbackText->isVisible());
        QVERIFY(videoStateText->property("text").toString().contains(QStringLiteral("已解码")));
        transmit.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.videoConnected(), 2500);
        QTRY_VERIFY(videoStateText->property("text").toString().contains(QStringLiteral("已过期")));
        QVERIFY(videoStateText->property("text").toString().contains(QStringLiteral("旧帧非实时")));
        QCOMPARE(videoStateText->property("color").value<QColor>(), QColor("#FF5F68"));
        QVERIFY(!videoFallbackText->isVisible());
        QVERIFY(terminal.refereeFresh());
        QVERIFY(!terminal.controlsArmed());

        const int framesBeforeRecovery = terminal.decodedFrames();
        offset = 0; // Resume at TS headers and a keyframe, without restarting the app.
        transmit.start();
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > framesBeforeRecovery + 5, 15000);
        QTRY_VERIFY(terminal.videoConnected());
        QVERIFY(!terminal.videoStale());
        QTRY_VERIFY(!videoStateText->property("text").toString().contains(QStringLiteral("已过期")));
        QVERIFY(!videoStateText->property("text").toString().contains(QStringLiteral("旧帧非实时")));
        QVERIFY(terminal.videoDiagnostics().value("recoveryAfterStaleMs").toLongLong() >= 0);
        QVERIFY(terminal.videoDiagnostics().value("frameAgeMs").toLongLong() >= 0);
        QVERIFY(!terminal.videoDiagnostics().value("endToEndMeasured").toBool());
        refereeTransmit.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.refereeFresh(), 2500);
        QVERIFY(terminal.videoConnected());
        QVERIFY(!terminal.controlsArmed());
        refereeTransmit.start();
        QTRY_VERIFY_WITH_TIMEOUT(terminal.refereeFresh(), 2500);
        QVERIFY(terminal.videoConnected());

        // 重新确认链路并更换本机图传端口，也必须重新启用帧过期检测。
        QUdpSocket portReservation;
        QVERIFY(portReservation.bind(QHostAddress::LocalHost, 0));
        testVideoDecodePort = portReservation.localPort();
        portReservation.close();
        QVERIFY(terminal.connection()->setProperty("simulationVideoPort", testVideoDecodePort));
        QVERIFY(terminal.applyConnectionProfile());
        offset = 0;
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 15000);
        QVERIFY(terminal.videoConnected());
        transmit.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.videoConnected(), 2500);
        QTRY_VERIFY(videoStateText->property("text").toString().contains(QStringLiteral("旧帧非实时")));
        QVERIFY(!terminal.controlsArmed());
    }

    void playbackAndRecovery()
    {
        resetIsolatedSettings();
        const auto fixture = QUrl::fromLocalFile(qEnvironmentVariable("RM27_TEST_VIDEO"));
        QVERIFY2(!fixture.isEmpty(), "Set RM27_TEST_VIDEO to an existing video fixture");
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* player = window->findChild<QMediaPlayer*>("localPlayer");
        auto* pause = window->findChild<QQuickItem*>("playbackButton");
        auto* timeline = window->findChild<QQuickItem*>("videoTimeline");
        auto* loop = window->findChild<QQuickItem*>("loopPlayback");
        auto* playbackDock = window->findChild<QQuickItem*>("playbackDock");
        auto* playbackDockToggle = window->findChild<QQuickItem*>("playbackDockToggle");
        auto* viewport = window->findChild<QQuickItem*>("videoViewport");
        auto* videoSurface = window->findChild<QQuickItem*>("localVideoOutput");
        auto* operatorCamera = window->findChild<QCamera*>("operatorCamera");
        auto* cameraPanel = window->findChild<QQuickItem*>("operatorCameraPanel");
        auto* cameraOpen = window->findChild<QQuickItem*>("operatorCameraOpen");
        auto* cameraStop = window->findChild<QQuickItem*>("operatorCameraStop");
        auto* cameraSelector = window->findChild<QQuickItem*>("operatorCameraSelector");
        QVERIFY(player && pause && timeline && loop && viewport && videoSurface
                && playbackDock && playbackDockToggle && operatorCamera && cameraPanel
                && cameraOpen && cameraStop && cameraSelector);
        QVERIFY(!operatorCamera->isActive());
        QVERIFY(cameraOpen->isVisible());
        QVERIFY(!cameraSelector->isVisible());
        auto click = [window](QQuickItem* item, qreal fraction = 0.5) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() * fraction, item->height() / 2)).toPoint());
        };
        auto open = [window](const QUrl& url) {
            return QMetaObject::invokeMethod(window, "openLocalVideo", Q_ARG(QUrl, url));
        };
        QVERIFY(open(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleFallbackMode();
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));

        click(pause);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        const auto pausedAt = player->position();
        QTest::qWait(300);
        QCOMPARE(player->position(), pausedAt);
        // 应用链路配置不能把仍在显示的本地回放误标为可操作模式，也不应重播。
        QVERIFY(terminal.applyConnectionProfile());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player->position(), pausedAt);
        QVERIFY(!terminal.controlsArmed());
        click(timeline, 0.5);
        QTRY_VERIFY(qAbs(player->position() - player->duration() / 2) < 700);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("playback-1600x900.png")));
        window->resize(1180, 680);
        QTest::qWait(200);
        QVERIFY(viewport->height() > 400);
        QVERIFY(timeline->width() > 100);
        click(playbackDockToggle);
        QTRY_COMPARE(playbackDock->height(), 0.0);
        QVERIFY(playbackDockToggle->isVisible());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("playback-collapsed-1180x680.png")));
        click(playbackDockToggle);
        QTRY_COMPARE(playbackDock->height(), 48.0);
        click(cameraOpen);
        QTRY_VERIFY(cameraPanel->height() > cameraPanel->width() * 0.5625 + 30);
        QVERIFY(cameraSelector->isVisible());
        click(cameraStop);
        QTRY_COMPARE(cameraPanel->height(), 28.0);
        QVERIFY(!cameraSelector->isVisible());
        auto* hud = window->findChild<QQuickItem*>("hudOverlay");
        auto* weapon = window->findChild<QQuickItem*>("weaponPanel");
        auto* map = window->findChild<QQuickItem*>("minimap");
        auto* friendly = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemy = window->findChild<QQuickItem*>("enemyTeam");
auto* baseSummary = window->findChild<QQuickItem*>("friendly_baseHp");
        auto* messageFeed = window->findChild<QQuickItem*>("messageFeed");
        auto* eventList = window->findChild<QQuickItem*>("eventList");
        auto* levelIndicator = window->findChild<QQuickItem*>("levelIndicator");
        auto* shieldLabel = window->findChild<QQuickItem*>("shieldLabel");
        auto* shieldBarFill = window->findChild<QQuickItem*>("shieldBarFill");
        auto* fireIndicator = window->findChild<QQuickItem*>("fireIndicator");
        auto* yawIndicator = window->findChild<QQuickItem*>("yawIndicator");
        auto* yawLabel = window->findChild<QQuickItem*>("yawIndicatorLabel");
        auto* yawMarker = window->findChild<QQuickItem*>("yawIndicatorMarker");
        auto* selfRobotLabel = window->findChild<QQuickItem*>("selfRobotLabel");
        auto* selfPhoto = window->findChild<QQuickItem*>("selfRobotVisual");
        auto* friendlyTeamRow = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemyTeamRow = window->findChild<QQuickItem*>("enemyTeam");
auto* structures = window->findChild<QQuickItem*>("enemyStructures");
        auto* weaponPanel = window->findChild<QQuickItem*>("weaponPanel");
        QVERIFY(friendlyTeamRow && enemyTeamRow && structures && weaponPanel);
        const int enemySlotsBefore = enemyTeamRow->childItems().size();
        // 数据未给出本车时不再排除任何一台：我方也显示全部六台。
        QCOMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore);
        // 建筑在车队上方；两条队伍栏同一行，不与侧边仪表相撞。
        QVERIFY(structures->y() + structures->height() <= enemyTeamRow->y());
        QCOMPARE(enemyTeamRow->y(), friendlyTeamRow->y());
        QVERIFY2(enemyTeamRow->x() + enemyTeamRow->width() <= friendlyTeamRow->x(),
                 qPrintable(QStringLiteral("敌方右沿=%1 我方左沿=%2")
                                .arg(enemyTeamRow->x() + enemyTeamRow->width())
                                .arg(friendlyTeamRow->x())));
        QVERIFY2(friendlyTeamRow->x() + friendlyTeamRow->width() <= window->width() - 20,
                 qPrintable(QStringLiteral("我方右沿=%1 窗口宽=%2")
                                .arg(friendlyTeamRow->x() + friendlyTeamRow->width())
                                .arg(window->width())));
        QVERIFY2(enemyTeamRow->y() + enemyTeamRow->height() <= weaponPanel->y(),
                 qPrintable(QStringLiteral("队伍栏底=%1 仪表顶=%2")
                                .arg(enemyTeamRow->y() + enemyTeamRow->height())
                                .arg(weaponPanel->y())));
        auto* friendlySlotHp0 = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotHp0"));
        auto* friendlySlotPhoto0 = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotPhoto0"));
        // 六张机型照片都按原文件名内置进了资源（缺一张就会退化成线稿，这里直接查资源）。
        for (const QString& alias : {QStringLiteral("sentry.png"), QStringLiteral("aerial.jpg"),
                                     QStringLiteral("infantry3.png"), QStringLiteral("infantry4.png"),
                                     QStringLiteral("engineer.jpg"), QStringLiteral("hero.png")}) {
            QVERIFY2(QFile(QStringLiteral(":/rm27/robots/") + alias).exists(), qPrintable(alias));
        }
        QVERIFY2(selfRobotLabel, "selfRobotLabel 未找到");
        QVERIFY2(selfPhoto, "selfRobotVisual 未找到");
        QVERIFY(friendlySlotHp0 && friendlySlotPhoto0);
        // 数据未给出本车时：左下角明确显示待确认。
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));
        QVERIFY(hud && weapon && map && friendly && enemy && baseSummary);
        QCOMPARE(baseSummary->property("text").toString(), QStringLiteral("我方 基地 --"));
        QVERIFY(messageFeed);
        QVERIFY(eventList);
        QVERIFY(levelIndicator && shieldLabel && shieldBarFill && fireIndicator);
        // LV 读数写在本车线稿下方，不与图形叠在一起（左下角也不再套圆形边框）。
        QVERIFY2(levelIndicator->y() >= selfPhoto->y() + selfPhoto->height(),
                 qPrintable(QStringLiteral("LV y=%1 线稿底=%2")
                                .arg(levelIndicator->y())
                                .arg(selfPhoto->y() + selfPhoto->height())));
        QVERIFY(yawIndicator && yawLabel && yawMarker);
        // 无数据时偏航条整条变暗、不显示标记，也不保留旧角度。
        QCOMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW --°"));
        QVERIFY(!yawMarker->isVisible());
        QCOMPARE(viewport->size(), window->size());
        QVERIFY2(weapon->y() + weapon->height() < map->y(),
                 qPrintable(QStringLiteral("仪表底=%1 地图顶=%2")
                                .arg(weapon->y() + weapon->height())
                                .arg(map->y())));
        QVERIFY(enemy->x() + enemy->width() < friendly->x());
        // 偏航条：轨道压在俯仰弧/对照弧下端所在的水平线上（半径 = 宽度×0.205，
        // 两段弧各画 ±0.17π），水平居中于准星下方，且停在底部浮层之上。
        constexpr double kPi = 3.14159265358979323846;
        const double unit = qMin(hud->width() / 1600.0, hud->height() / 900.0);
        const double arcBottomLine = hud->height() * 0.5
            + hud->width() * 0.205 * std::sin(kPi * 0.17);
        QVERIFY2(qAbs(yawIndicator->y() + 8 * unit - arcBottomLine) < 2.0,
                 qPrintable(QStringLiteral("y=%1 arcLine=%2")
                                .arg(yawIndicator->y()).arg(arcBottomLine)));
        QCOMPARE(qRound(yawIndicator->x() + yawIndicator->width() / 2 - hud->width() / 2), 0);
        QVERIFY(yawIndicator->y() > hud->height() * 0.5);
        QVERIFY(playbackDockToggle->isVisible());
        QVERIFY(yawIndicator->y() + yawIndicator->height() <= playbackDockToggle->y());
        QVERIFY(yawIndicator->y() + yawIndicator->height() <= playbackDock->y());
        const QByteArray hudPacket = R"json({
            "channel":"rm27.sim.v1.referee","sequence":1,"sent_at_unix_ms":1789999999000,
            "phase":"match",
            "telemetry":{"hp":850,"max_hp":1000,"chassis_power_w":80,
                "buffer_energy_j":45,"barrel_heat":30,"ammo":90,
                "latency_ms":12,"packet_loss_pct":0.1},
            "hud":{"own_team":"blue","friendly_hp":[900,800,700,600,500,400],
                "enemy_hp":[880,780,680,580,480,380],
                "friendly_positions":[[0.1,0.2],[0.2,0.3],[0.3,0.4],[0.4,0.5],[0.5,0.6],[0.6,0.7]],
                "enemy_positions":[[0.9,0.8],[0.8,0.7],[0.7,0.6],[0.6,0.5],[0.5,0.4],[0.4,0.3]],
                "friendly_coins":100,"enemy_coins":80,"restart_count":2,
                "pitch_deg":12.5,"yaw_deg":-37.5,"self_index":3,"speed_mps":24.0,"allowed_shots":80,
                "level":3,"shield":60,"max_shield":100,
                "structures":{"friendly_base":{"hp":4500,"max_hp":5000},
                    "enemy_base":{"hp":4000,"max_hp":5000},
                    "friendly_outpost":{"hp":0,"max_hp":1500},
                    "enemy_outpost":{"hp":800,"max_hp":1500}},
                "actions":{"climb":false,"fire":true,"speed_up":false}}
        })json";
        QUdpSocket sender;
        QCOMPARE(sender.writeDatagram(hudPacket, QHostAddress::LocalHost, testRefereePort), hudPacket.size());
        QTRY_COMPARE(terminal.hudSnapshot().value("ownTeam").toString(), QStringLiteral("blue"));
        QCOMPARE(terminal.hudSnapshot().value("friendlyHp").toList().at(0).toInt(), 900);
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("blue"));
        QCOMPARE(baseSummary->property("text").toString(), QStringLiteral("我方 · 蓝方 基地 4500"));
        QCOMPARE(terminal.hudSnapshot().value("level").toInt(), 3);
        auto* friendlyBaseHp = window->findChild<QQuickItem*>("friendly_baseHp");
        auto* friendlyOutpostStatus = window->findChild<QQuickItem*>("friendly_outpostStatus");
        QVERIFY(friendlyBaseHp && friendlyOutpostStatus);
        QCOMPARE(friendlyBaseHp->property("text").toString(), QStringLiteral("我方 · 蓝方 基地 4500"));
        QCOMPARE(friendlyOutpostStatus->property("text").toString(), QStringLiteral("前哨 摧毁"));
        auto* enemyOutpost = window->findChild<QQuickItem*>("enemy_outpostStatus");
        QVERIFY(enemyOutpost);
        QCOMPARE(enemyOutpost->property("text").toString(), QStringLiteral("前哨 存活"));
        auto* baseFill = window->findChild<QQuickItem*>("friendly_baseFill");
        QVERIFY(baseFill);
        QVERIFY(baseFill->property("known").toBool());
        QVERIFY(baseFill->property("fillFromRight").toBool());
        QVERIFY(qAbs(baseFill->property("ratio").toDouble() - 0.9) < 0.001);
        QVERIFY(!window->findChild<QObject*>("friendly_baseRing"));
        QCOMPARE(levelIndicator->property("text").toString(), QStringLiteral("LV 3"));
        // 偏航角反馈：读数与标记同步，标记落在刻度条内。
        QTRY_VERIFY(qAbs(terminal.hudSnapshot().value("yawDeg").toDouble() + 37.5) < 1e-6);
        QTRY_COMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW -37.5°"));
        QVERIFY(yawMarker->isVisible());
        QVERIFY(yawMarker->x() >= 0);
        QVERIFY(yawMarker->x() + yawMarker->width() <= yawIndicator->width());
        // 标记在轨道上方、读数在刻度下方：两者不占同一条纵带。
        QVERIFY(yawMarker->y() + yawMarker->height() <= yawLabel->y());
        // 本车身份：裁判数据给出 self_index=3（步兵4）→ 本车移出我方列表（六台 → 五台），
        // 其余队友血量按机型下标取，不会因为少了一格而错位；本车完全交给左下角自车模块。
        QTRY_COMPARE(terminal.hudSnapshot().value("selfIndex").toInt(), 3);
        QTRY_COMPARE(hud->property("selfIndex").toInt(), 3);
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore - 1);
        QVERIFY(!findVisual(friendlyTeamRow, QStringLiteral("friendlySlot3")));
        auto* friendlySlotHp0After = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotHp0"));
        QVERIFY(friendlySlotHp0After);
        QTRY_COMPARE(friendlySlotHp0After->property("text").toString(), QStringLiteral("900"));
        // 敌方列表不受影响，始终六台。
        QCOMPARE(enemyTeamRow->childItems().size(), enemySlotsBefore);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 步兵4"));
        QVERIFY(selfPhoto->property("photoSource").toString().contains(QStringLiteral("infantry4")));
        // 队伍栏里的机型线稿确实从资源加载成功（不是回退线稿）。
        QVERIFY(findVisual(friendlyTeamRow, QStringLiteral("friendlySlotPhoto0"))
                    ->property("photoReady").toBool());
        QVERIFY(shieldLabel->property("text").toString().contains(QStringLiteral("60/100")));
        QVERIFY(shieldBarFill->width() > 0);
        QCOMPARE(fireIndicator->property("color").value<QColor>(), QColor("#FF626C"));
        QByteArray robotEvent = hudPacket;
        robotEvent.replace("\"sequence\":1", "\"sequence\":2");
        robotEvent.replace("\"hud\":{", "\"event\":{\"id\":\"upgrade\",\"category\":\"robot\",\"text\":\"模拟：英雄升级\"},\"hud\":{");
        QCOMPARE(sender.writeDatagram(robotEvent, QHostAddress::LocalHost, testRefereePort), robotEvent.size());
        QTRY_COMPARE(terminal.eventMessages().size(), 1);
        QTRY_COMPARE(eventList->property("count").toInt(), 1);
        robotEvent.replace("\"sequence\":2", "\"sequence\":3");
        QCOMPARE(sender.writeDatagram(robotEvent, QHostAddress::LocalHost, testRefereePort), robotEvent.size());
        QCOMPARE(terminal.eventMessages().size(), 1);
        QByteArray systemEvent = robotEvent;
        systemEvent.replace("\"sequence\":3", "\"sequence\":4");
        systemEvent.replace("\"id\":\"upgrade\",\"category\":\"robot\",\"text\":\"模拟：英雄升级\"",
                            "\"id\":\"dart\",\"category\":\"system\",\"text\":\"模拟：飞镖命中\"");
        QCOMPARE(sender.writeDatagram(systemEvent, QHostAddress::LocalHost, testRefereePort), systemEvent.size());
        QTRY_COMPARE(terminal.eventMessages().size(), 2);
        QTRY_COMPARE(eventList->property("count").toInt(), 2);
        QVERIFY(messageFeed->property("color").value<QColor>().alpha() < 128);
        videoSurface->setVisible(false);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("hud-design-preview-1180x680.png")));
        videoSurface->setVisible(true);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("hud-simulation-1180x680.png")));
        QByteArray redPacket = systemEvent;
        redPacket.replace("\"sequence\":4", "\"sequence\":5");
        redPacket.replace("\"own_team\":\"blue\"", "\"own_team\":\"red\"");
        QCOMPARE(sender.writeDatagram(redPacket, QHostAddress::LocalHost, testRefereePort), redPacket.size());
        QTRY_COMPARE(hud->property("ownTeam").toString(), QStringLiteral("red"));
        QCOMPARE(baseSummary->property("text").toString(), QStringLiteral("我方 · 红方 基地 4500"));
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("hud-red-team-1180x680.png")));
        // 检查实际填色：左侧保留左端，右侧保留右端；红蓝阵营变化不改变方向。
        auto* enemyBaseFill = window->findChild<QQuickItem*>("enemy_baseFill");
        QVERIFY(enemyBaseFill);
        QVERIFY(!enemyBaseFill->property("fillFromRight").toBool());
        const auto fillPixel = [&](QQuickItem* fill, qreal fraction, qreal verticalFraction = 0.5) {
            const auto image = window->grabWindow();
            const QPointF point = fill->mapToScene(QPointF(fill->width() * fraction, fill->height() * verticalFraction));
            return image.pixelColor(QPoint(qRound(point.x() * image.width() / window->width()),
                qRound(point.y() * image.height() / window->height())));
        };
        const QColor gap = fillPixel(enemyBaseFill, 0.95), progress = fillPixel(enemyBaseFill, 0.05);
        QVERIFY(progress.green() > gap.green() + 20);
        QVERIFY(progress.green() > 140); // 填充主体提亮，而不只增加外沿高光。
        QVERIFY(fillPixel(baseFill, 0.95).red() > fillPixel(baseFill, 0.05).red() + 20);
        QVERIFY(fillPixel(enemyBaseFill, 0.05, 0.18).green() > progress.green() + 15);
        QVERIFY(qAbs(fillPixel(enemyBaseFill, 0.95, 0.18).green() - gap.green()) < 5);
        auto* actionHint = window->findChild<QQuickItem*>("actionHint");
        QVERIFY(actionHint);
        QVERIFY(actionHint->mapRectToScene(actionHint->boundingRect()).bottom() + 8
                <= hud->property("bottomClearance").toReal());
        QVERIFY(!actionHint->mapRectToScene(actionHint->boundingRect())
                .intersects(map->mapRectToScene(map->boundingRect())));
        QByteArray zeroBasePacket = redPacket;
        zeroBasePacket.replace("\"sequence\":5", "\"sequence\":6");
        zeroBasePacket.replace("\"hp\":4500", "\"hp\":0");
        QCOMPARE(sender.writeDatagram(zeroBasePacket, QHostAddress::LocalHost, testRefereePort), zeroBasePacket.size());
        QTRY_COMPARE(baseFill->property("ratio").toDouble(), 0.0);
        QVERIFY(baseFill->property("known").toBool());
        const QColor zeroFill = fillPixel(baseFill, 0.95);
        QCOMPARE(friendlyBaseHp->property("text").toString(), QStringLiteral("我方 · 红方 基地 0"));
        QByteArray fullBasePacket = redPacket;
        fullBasePacket.replace("\"sequence\":5", "\"sequence\":7");
        fullBasePacket.replace("\"hp\":4500", "\"hp\":5000");
        QCOMPARE(sender.writeDatagram(fullBasePacket, QHostAddress::LocalHost, testRefereePort), fullBasePacket.size());
        QTRY_COMPARE(baseFill->property("ratio").toDouble(), 1.0);
        QVERIFY(baseFill->property("known").toBool());
        QVERIFY(fillPixel(baseFill, 0.05).red() > zeroFill.red() + 20);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.hudSnapshot().isEmpty(), 2500);
        QCOMPARE(terminal.eventMessages().size(), 0);
        QCOMPARE(friendlyBaseHp->property("text").toString(), QStringLiteral("我方 基地 --"));
        QCOMPARE(friendlyOutpostStatus->property("text").toString(), QStringLiteral("前哨 未知"));
        QCOMPARE(baseFill->property("ratio").toDouble(), 0.0);
        QVERIFY(!baseFill->property("known").toBool());
        QCOMPARE(levelIndicator->property("text").toString(), QStringLiteral("LV --"));
        QCOMPARE(shieldBarFill->width(), 0.0);
        QCOMPARE(fireIndicator->property("color").value<QColor>(), QColor("#82939F"));
        // 数据过期后偏航条同样回到未知，不保留旧角度。
        QCOMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW --°"));
        QVERIFY(!yawMarker->isVisible());
        // 本车身份同样回到未知：列表恢复六台、左下角回到待确认，不保留上一次机型。
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));
        QVERIFY(!selfPhoto->property("photoReady").toBool());
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("unknown"));
        QCOMPARE(baseSummary->property("text").toString(), QStringLiteral("我方 基地 --"));
        QVERIFY(hud->setProperty("ownTeam", "red"));
        QCOMPARE(friendly->property("accent").value<QColor>(), QColor("#FF626C"));
        QCOMPARE(enemy->property("accent").value<QColor>(), QColor("#49D7EF"));
        QVERIFY(hud->setProperty("ownTeam", "blue"));
        QCOMPARE(friendly->property("accent").value<QColor>(), QColor("#49D7EF"));
        QVERIFY(hud->setProperty("ownTeam", "unknown"));
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("playback-1180x680.png")));
        window->resize(1600, 900);

        click(pause);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(loop);
        QTRY_COMPARE(player->loops(), QMediaPlayer::Infinite);
        player->setPosition(player->duration() - 250);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() < 2000, 4000);
        QVERIFY(player->isPlaying());
        click(loop);
        QTRY_COMPARE(player->loops(), 1);
        player->setPosition(player->duration() - 250);
        QTRY_COMPARE_WITH_TIMEOUT(player->mediaStatus(), QMediaPlayer::EndOfMedia, 4000);
        click(pause);
        QTRY_VERIFY(player->isPlaying());

        QVERIFY(open(QUrl::fromLocalFile("Z:/rm27-missing-test-file.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(player->error() != QMediaPlayer::NoError, 5000);
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(open(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        QCOMPARE(player->error(), QMediaPlayer::NoError);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        QVERIFY(!terminal.controlsArmed());
        player->stop();
    }

    // 启动面板 + 离线哨兵段入口：这是「无网也能直接进去」的那条路径。
    void startPanelOfflineSentinel()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        auto* settings = terminal.connection();
        QVERIFY(settings);

        // 预设下拉里必须带 203.0.113.0/24 这一项，现场想不起地址时可直接点选。
        bool sentinelPresetFound = false;
        const QVariantList presets = settings->presets();
        for (const QVariant& preset : presets) {
            if (preset.toMap().value(QStringLiteral("label")).toString()
                    .contains(QStringLiteral("203.0.113.0/24"))) {
                sentinelPresetFound = true;
            }
        }
        QVERIFY(sentinelPresetFound);
        QVERIFY(presets.size() >= 3);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* startPanel = window->findChild<QQuickItem*>("startPanel");
        auto* hostField = window->findChild<QQuickItem*>("startPanelRefereeHost");
        auto* presetCombo = window->findChild<QQuickItem*>("startPanelPreset");
        auto* sentinelNote = window->findChild<QQuickItem*>("startPanelSentinel");
        auto* enterButton = window->findChild<QQuickItem*>("startPanelEnter");
        auto* offlineButton = window->findChild<QQuickItem*>("startPanelOfflineButton");
        auto* banner = window->findChild<QQuickItem*>("offlineDrillBanner");
        QVERIFY(startPanel && hostField && presetCombo && sentinelNote);
        QVERIFY(enterButton && offlineButton && banner);

        // 每次启动都弹面板，且默认不是哨兵段。
        QVERIFY(startPanel->isVisible());
        QVERIFY(!sentinelNote->isVisible());
        QVERIFY(!banner->isVisible());
        QCOMPARE(hostField->property("text").toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(presetCombo->property("count").toInt(), presets.size());

        // 非哨兵地址绝不能被当成离线入口。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("192.168.1.10")));
        QVERIFY(!settings->offlineSentinel());
        QVERIFY(!sentinelNote->isVisible());

        // 点选哨兵预设 → 主机被填成哨兵地址、提示块出现。
        QVERIFY(QMetaObject::invokeMethod(settings, "selectPreset", Q_ARG(int, 2)));
        QCOMPARE(settings->refereeHost(), QStringLiteral("203.0.113.7"));
        QVERIFY(settings->offlineSentinel());
        QTRY_VERIFY(sentinelNote->isVisible());
        QCOMPARE(hostField->property("text").toString(), QStringLiteral("203.0.113.7"));

        // 选择预设会写回默认端口，进入前恢复本例申请的隔离端口。
        QVERIFY(settings->setProperty("refereePort", testRefereePort));
        QVERIFY(settings->setProperty("videoProbePort", testVideoProbePort));
        QVERIFY(settings->setProperty("simulationVideoPort", testVideoDecodePort));

        window->resize(1180, 680);
        QTest::qWait(220);
        QVERIFY(enterButton->isVisible());
        QVERIFY(enterButton->mapToScene(QPointF(0, 0)).y() + enterButton->height()
                <= window->height());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("start-panel-1180x680.png")));

        // 离线入口进入：面板收起、常驻横幅出现、进入模拟模式、控制保持锁定。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          enterButton->mapToScene(enterButton->boundingRect().center()).toPoint());
        QTRY_VERIFY(!startPanel->isVisible());
        QVERIFY(terminal.offlineDrill());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QTRY_VERIFY(banner->isVisible());
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleFallbackMode();
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("start-panel-offline-1180x680.png")));

        // F1 重新打开面板；离线演练入口同样可以再次进入。
        // 右列卡片在 1180×680 下需要滚动才可见，因此这里调用面板函数（与点击按钮同一条代码路径），
        // 左列顶部的哨兵快捷按钮则必须仍在视口内。
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(startPanel->isVisible());
        auto* offlineShortcut = window->findChild<QQuickItem*>("startPanelOfflineShortcut");
        QVERIFY(offlineShortcut && offlineShortcut->isVisible());
        QVERIFY(QMetaObject::invokeMethod(startPanel, "enterOffline"));
        QTRY_VERIFY(!startPanel->isVisible());
        QVERIFY(terminal.offlineDrill());

        // 改回环回地址并套用 → 离线演练结束，横幅消失，仍是本机模拟。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("127.0.0.1")));
        QVERIFY(terminal.applyConnectionProfile());
        QVERIFY(!terminal.offlineDrill());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QTRY_VERIFY(!banner->isVisible());

        // 隐形快捷键 Ctrl+Shift+O：面板关闭时也能直接进离线演练。
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTest::qWait(60);
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(terminal.offlineDrill());
        QTRY_VERIFY(banner->isVisible());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QVERIFY(!startPanel->isVisible());

        // 参数校验：非法地址必须被挡住，不能静默进入。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("referee.local")));
        QVERIFY(!settings->validationMessage().isEmpty());
        QVERIFY(!settings->entryAllowed());
        QVERIFY(!terminal.applyConnectionProfile());
    }

    // 视频链路分阶段诊断：到达缺口、断流、重同步、来源拒绝都必须真的计数。
    void videoLinkDiagnostics()
    {
        rm27::video::UdpVideoIngress ingress;
        QVERIFY(ingress.start(testVideoDecodePort));
        QUdpSocket sender;
        const QByteArray payload(64, 'x');
        auto send = [&] { return sender.writeDatagram(payload, QHostAddress::LocalHost, testVideoDecodePort); };

        QCOMPARE(ingress.packetCount(), quint64(0));
        QCOMPARE(ingress.silentMs(), qint64(-1));
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.packetCount(), quint64(1));
        QVERIFY(ingress.receivedAny());
        QCOMPARE(ingress.arrivalGapCount(), quint32(0));
        QVERIFY(ingress.silentMs() >= 0);

        // 静默超过到达缺口阈值后再收包 → 记一次到达缺口（不是官方序号缺口）。
        QTest::qWait(400);
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.packetCount(), quint64(2));
        QCOMPARE(ingress.arrivalGapCount(), quint32(1));

        // 静默超过 700ms → 断流一次；再收包 → 重同步一次。
        QTRY_COMPARE_WITH_TIMEOUT(ingress.stallCount(), quint32(1), 3000);
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.resyncCount(), quint32(1));
        QCOMPARE(ingress.rejectedSourceCount(), quint32(0));

        // 控制器把读数聚合成分阶段诊断，并明确标出尚未接入的两段。
        TerminalController terminal(false, nullptr, testRefereePort);
        QVERIFY(terminal.connection()->setProperty("videoProbePort", testVideoProbePort));
        QVERIFY(terminal.connection()->setProperty("refereePort", testRefereePort));
        QVERIFY(terminal.applyConnectionProfile());
        const QVariantMap idle = terminal.videoDiagnostics();
        QCOMPARE(idle.value(QStringLiteral("stage")).toString(), QStringLiteral("等待首包"));
        QVERIFY(idle.value(QStringLiteral("stagePending")).toString()
                    .contains(QStringLiteral("待官方")));
        QVERIFY(terminal.videoDiagnosticsLabel().contains(QStringLiteral("来源拒绝")));

        QUdpSocket probe;
        QCOMPARE(probe.writeDatagram(payload, QHostAddress::LocalHost, testVideoProbePort), payload.size());
        QTRY_COMPARE(terminal.videoDiagnostics().value(QStringLiteral("stage")).toString(),
                     QStringLiteral("已收包·等待解码"));
        QVERIFY(terminal.videoDiagnostics().value(QStringLiteral("packets")).toULongLong() >= 1);
        QVERIFY(terminal.videoDiagnosticsLabel().contains(QStringLiteral("已收包")));
    }

    // 配置原子写 + 坏文件自愈：损坏文件被隔离，优先从 .bak 恢复，否则回落默认值。
    void connectionConfigRecovery()
    {
        const QByteArray previousEnv = qgetenv("RM27_SETTINGS_FILE");
        QTemporaryDir recoveryDirectory;
        QVERIFY(recoveryDirectory.isValid());
        const QString path = recoveryDirectory.filePath(QStringLiteral("rm27-recovery-test.ini"));

        // 1) 造一个被截断、不属于本应用的文件。
        {
            QFile garbage(path);
            QVERIFY(garbage.open(QIODevice::WriteOnly | QIODevice::Text));
            garbage.write("\x00garbage ][ truncated-not-our-ini");
            garbage.close();
        }

        qputenv("RM27_SETTINGS_FILE", path.toUtf8());
        {
            rm27::app::ConnectionSettings settings;
            QVERIFY(!settings.storageNotice().isEmpty());
            QCOMPARE(settings.refereeHost(), QStringLiteral("127.0.0.1"));
            QVERIFY(settings.validationMessage().isEmpty());
            QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak")));
            const QStringList quarantined = QDir(recoveryDirectory.path())
                .entryList({QStringLiteral("rm27-recovery-test.ini.corrupt-*")}, QDir::Files);
            QCOMPARE(quarantined.size(), 1);

            // 2) 保存：原子写 + 生成 .bak + 清空自愈提示，且不留 .tmp。
            QVERIFY(settings.save());
            QVERIFY(settings.storageNotice().isEmpty());
            QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));
            QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));
            QFile written(path);
            QVERIFY(written.open(QIODevice::ReadOnly));
            const QByteArray body = written.readAll();
            written.close();
            // QSettings 的 INI 写法是分组：键 meta/schema 落地为 [meta] 段下的 schema。
            QVERIFY(body.contains("[meta]"));
            QVERIFY(body.contains("schema"));
            QVERIFY(body.contains("referee_host"));
        }
        // 3) 主文件再损坏 → 这次应从 .bak 恢复，而不是回落默认值。
        {
            QFile broken(path);
            QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Text));
            broken.write("###");
            broken.close();
        }
        {
            rm27::app::ConnectionSettings settings;
            QVERIFY(settings.storageNotice().contains(QStringLiteral("备份")));
            QVERIFY(settings.validationMessage().isEmpty());
        }

        qputenv("RM27_SETTINGS_FILE", previousEnv);
    }

    // 界面占用闸门 + 单一输入闸门：占屏时命令类动作被拦，界面类动作仍可派发。
    void inputGateAndUiOccupancy()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, testRefereePort);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("重装操作"));

        // 动作表即快捷键表：提示由映射生成。
        const QVariantList actions = terminal.inputActions();
        QVERIFY(actions.size() >= 7);
        bool sawOfflineHint = false;
        for (const QVariant& entry : actions) {
            const QVariantMap action = entry.toMap();
            QVERIFY(!action.value(QStringLiteral("id")).toString().isEmpty());
            QVERIFY(!action.value(QStringLiteral("keys")).toString().isEmpty());
            const QString keys = action.value(QStringLiteral("keys")).toString();
            QCOMPARE(QKeySequence::fromString(keys, QKeySequence::PortableText)
                         .toString(QKeySequence::PortableText), keys);
            if (action.value(QStringLiteral("hint")).toString()
                == QStringLiteral("离线演练进入 [Ctrl+Shift+O]")) {
                sawOfflineHint = true;
            }
        }
        QVERIFY(sawOfflineHint);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // 启动面板可见 → 界面占用；命令类动作被拦，界面类动作仍可派发。
        QTRY_VERIFY(terminal.uiOccupied());
        QVERIFY(!terminal.submitInputAction(QStringLiteral("controls.toggleArmed")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));
        QVERIFY(terminal.submitInputAction(QStringLiteral("panel.start")));
        QVERIFY(!terminal.controlsArmed());
        // 未知动作直接拒绝。
        QVERIFY(!terminal.submitInputAction(QStringLiteral("no.such.action")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("未知输入动作")));
        // 「检查指令门」在占屏时也必须报告占用而不是别的原因。
        QVERIFY(!terminal.submitInputAction(QStringLiteral("gate.inspectMotion")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));

        // 收起面板 → 占用解除，命令类动作改由链路原因拦截（仍不放行）。
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTRY_VERIFY(!terminal.uiOccupied());
        QVERIFY(!terminal.submitInputAction(QStringLiteral("controls.toggleArmed")));
        QVERIFY(!terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));
        QVERIFY(!terminal.lastInputDecision().isEmpty());
    }

    void respawnShortcuts()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window); QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *freeShortcut = window->findChild<QObject*>("freeRespawnShortcut");
        auto *paidShortcut = window->findChild<QObject*>("paidRespawnShortcut");
        QVERIFY(freeShortcut && paidShortcut);
        QSignalSpy freeActivation(freeShortcut, SIGNAL(activated()));
        QSignalSpy paidActivation(paidShortcut, SIGNAL(activated()));
        QSignalSpy uiAction(&terminal, &TerminalController::inputActionRequested);
        QVERIFY(freeActivation.isValid() && paidActivation.isValid());
        QVERIFY(!freeShortcut->property("autoRepeat").toBool());
        QVERIFY(!paidShortcut->property("autoRepeat").toBool());
        QTRY_VERIFY(terminal.uiOccupied());
        QVERIFY(!freeShortcut->property("enabled").toBool());
        QTest::keyClick(window, Qt::Key_Y); QCOMPARE(freeActivation.size(), 0);
        QVERIFY(!terminal.submitInputAction("event.freeRespawn"));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));
        QCOMPARE(uiAction.size(), 0); // Refused event is never a pretend UI dispatch.
        dismissStartPanel(window);
        QTRY_VERIFY(freeShortcut->property("enabled").toBool());
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Y, Qt::AltModifier);
        QCOMPARE(freeActivation.size(), 0);
        QTest::keyClick(window, Qt::Key_Y);
        QTRY_COMPARE(freeActivation.size(), 1);
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("请求免费复活")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("未发送")));
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier, "y", true, 1);
        QCoreApplication::sendEvent(window, &repeat);
        QCOMPARE(freeActivation.size(), 1);
        QTest::keyClick(window, Qt::Key_F1, Qt::ShiftModifier);
        QTRY_COMPARE(paidActivation.size(), 1);
        QVERIFY(!window->property("startPanelVisible").toBool());
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("请求付费复活")));
        QCOMPARE(uiAction.size(), 0); QVERIFY(!terminal.controlsArmed());
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(window->property("startPanelVisible").toBool());
        QCOMPARE(uiAction.size(), 1); // Original F1 action retained.
        dismissStartPanel(window);
        terminal.enterOfflineDrill();
        QVERIFY(!terminal.submitInputAction("event.freeRespawn"));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("模拟或回放模式禁止下发")));
        terminal.enterLocalPlayback();
        QVERIFY(!terminal.submitInputAction("event.paidRespawn"));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("模拟或回放模式禁止下发")));
        QCOMPARE(uiAction.size(), 1);
    }

    // 性能读数：出帧率与 GUI 线程抖动（抖动大 = 解码/IO 回到 GUI 线程）。
    void performanceAndFreshness()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, testRefereePort);
        QCOMPARE(terminal.refereeFreshnessLabel(), QStringLiteral("无数据"));
        QCOMPARE(terminal.refereeFreshnessScale(), 0.0);
        QVERIFY(!terminal.guiThreadStalled());
        QVERIFY(terminal.performanceLabel().contains(QStringLiteral("出帧")));

        // 收到模拟裁判数据 → 分级进入「新鲜」。
        QUdpSocket sender;
        const QByteArray packet = R"json({
            "channel":"rm27.sim.v1.referee","sequence":1,"sent_at_unix_ms":1789999999000,
            "phase":"match","telemetry":{"hp":800,"max_hp":1000,"chassis_power_w":70,
                "buffer_energy_j":40,"barrel_heat":20,"ammo":80,"latency_ms":10,
                "packet_loss_pct":0.0}
        })json";
        QCOMPARE(sender.writeDatagram(packet, QHostAddress::LocalHost, testRefereePort), packet.size());
        QTRY_COMPARE(terminal.refereeFreshnessLabel(), QStringLiteral("新鲜"));
        QCOMPARE(terminal.refereeFreshnessScale(), 1.0);

        // 数据停发后分级逐级下降，最终过期（分级只影响显示，不影响放行判断）。
        QTRY_VERIFY_WITH_TIMEOUT(
            terminal.refereeFreshnessLabel() == QStringLiteral("已过期"), 4000);
        QCOMPARE(terminal.refereeFreshnessScale(), 0.0);

        // GUI 线程被人为阻塞 → 心跳抖动检出（用累计计数，避免被下一次心跳复位）；
        // 阻塞结束后瞬时标志自动清除。
        const int stallsBefore = terminal.guiStallCount();
        QThread::msleep(600);
        QTRY_VERIFY2_WITH_TIMEOUT(terminal.guiStallCount() > stallsBefore,
                                  qPrintable(terminal.performanceLabel()), 3000);
        QVERIFY2(terminal.guiPeakJitterMs() > 150, qPrintable(terminal.performanceLabel()));
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.guiThreadStalled(), 3000);
        QVERIFY(terminal.performanceLabel().contains(QStringLiteral("阻塞")));
    }

    void localVideoReselection()
    {
        const auto fixture = QUrl::fromLocalFile(qEnvironmentVariable("RM27_TEST_VIDEO"));
        QVERIFY(QFileInfo::exists(fixture.toLocalFile()));
        QTemporaryDir directory;
        const auto other = QUrl::fromLocalFile(directory.filePath("second clip.mp4"));
        QVERIFY(QFile::copy(fixture.toLocalFile(), other.toLocalFile()));
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* player = window->findChild<QMediaPlayer*>("localPlayer");
        auto* dialog = window->findChild<QObject*>("localVideoDialog");
        QVERIFY(player && dialog);
        // 离屏测试使用 Qt 自带 Quick 对话框，不弹出 Windows 原生窗口。
        QQmlExpression option(qmlContext(dialog), dialog, QStringLiteral("FileDialog.DontUseNativeDialog"));
        const QVariant flags = option.evaluate();
        QVERIFY(!option.hasError());
        QVERIFY(dialog->setProperty("options", flags));
        QVERIFY(terminal.requestVideoSource(1));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(terminal.videoSourceLabel(), QStringLiteral("不加载画面"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(terminal.videoSourceLabel(), QStringLiteral("不加载画面"));
        QVERIFY(player->source().isEmpty());
        QVERIFY(terminal.requestVideoSource(1));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->setProperty("selectedFile", fixture));
        // 注入选择器的最终选择信号；不依赖原生窗口或 Quick 文件列表内部实现。
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCOMPARE(player->source(), fixture);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        player->pause();
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        const qint64 position = player->position();
        QVERIFY(terminal.requestVideoSource(1)); // 曾被“同源短路”拦住，无法再次选片。
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(terminal.uiOccupied() && !terminal.controlsArmed());
        QCOMPARE(player->source(), fixture);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(player->source(), fixture);
        QCOMPARE(player->position(), position);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTRY_VERIFY(!terminal.uiOccupied());
        for (const auto& file : {other, fixture, fixture}) {
            QVERIFY(terminal.requestVideoSource(1));
            QTRY_VERIFY(dialog->property("visible").toBool());
            QVERIFY(dialog->setProperty("selectedFile", file));
            QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
            QCOMPARE(player->source(), file);
            QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
            QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
            QVERIFY(player->isPlaying() && !terminal.controlsArmed());
        }
        // 无效媒体之后，仍能通过同一个选择器换回有效视频。
        const auto bad = QUrl::fromLocalFile(directory.filePath("invalid.mp4"));
        QVERIFY(QFile::copy(QStringLiteral(RM27_QML_SOURCE), bad.toLocalFile()));
        for (const auto& file : {bad, fixture}) {
            QVERIFY(terminal.requestVideoSource(1));
            QTRY_VERIFY(dialog->property("visible").toBool());
            QVERIFY(dialog->setProperty("selectedFile", file));
            QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QCOMPARE(player->source(), file);
            if (file == bad)
                QTRY_VERIFY_WITH_TIMEOUT(player->error() != QMediaPlayer::NoError, 10000);
            else
                QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
            QVERIFY(!terminal.controlsArmed());
        }
    }

    // 切源代价：重复打开本地文件选择不重启播放，UDP 同源短路，异源需最短驻留。
    void sourceSwitchCost()    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QCOMPARE(terminal.videoSourceLabel(), QStringLiteral("不加载画面"));
        // 首次切到本机 UDP 联调：允许，并由表现层真正切源。
        QVERIFY(terminal.requestVideoSource(2));
        QTRY_COMPARE(terminal.videoSourceLabel(), QStringLiteral("本机 UDP 联调"));
        // 同源再切：短路，不重启解码。
        QVERIFY(!terminal.requestVideoSource(2));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("已是当前画面来源")));
        // 冷却期内换源：拒绝。
        QVERIFY(!terminal.requestVideoSource(1));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("最短驻留")));
        // 知道来源编号非法时明确拒绝。
        QVERIFY(!terminal.requestVideoSource(9));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("未知画面来源")));
    }

    // 开局挑选本车机型：只作裁判数据到达前的身份；写入配置、面板下拉跟随，
    // 且本车会被移出我方列表（六台 → 五台），本车自身交给左下角自车模块。
    void ownRobotSelection()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, testRefereePort);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* combo = window->findChild<QQuickItem*>("startPanelOwnRobot");
        auto* preview = window->findChild<QQuickItem*>("startPanelOwnRobotPreview");
        auto* selfRobotLabel = window->findChild<QQuickItem*>("selfRobotLabel");
        auto* selfPhoto = window->findChild<QQuickItem*>("selfRobotVisual");
        auto* friendlyTeamRow = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemyTeamRow = window->findChild<QQuickItem*>("enemyTeam");
        QVERIFY(combo && preview && selfRobotLabel && selfPhoto && friendlyTeamRow && enemyTeamRow);
        const int allSlots = enemyTeamRow->childItems().size();

        // 默认未指定：下拉停在第一项、没有预览、两边都显示六台、左下角待确认。
        QCOMPARE(combo->property("currentIndex").toInt(), 0);
        QVERIFY(!preview->isVisible());
        QCOMPARE(friendlyTeamRow->childItems().size(), allSlots);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));

        // 机型表同时提供仪表量程：英雄 16 m/s，其余 24 m/s（本车未知时按 24）。
        auto* photoTable = window->findChild<QObject*>("robotPhotos");
        QVERIFY(photoTable);
        const auto speedMaxOf = [photoTable](int roleIndex) {
            QVariant result;
            const bool ok = QMetaObject::invokeMethod(photoTable, "speedMaxAt",
                Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, roleIndex));
            return ok ? result.toInt() : -1;
        };
        QCOMPARE(speedMaxOf(5), 16);   // 英雄
        QCOMPARE(speedMaxOf(3), 24);   // 步兵4
        QCOMPARE(speedMaxOf(-1), 24);  // 本车未知

        // 在开局配置里选「工程」：写入配置、面板下拉与缩略图跟随。
        terminal.connection()->setOwnRobot(QStringLiteral("engineer"));
        QTRY_COMPARE(terminal.ownRobotPreference(), QStringLiteral("engineer"));
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 5); // 未指定 + 六台机型，工程排第 5 项
        QTRY_VERIFY(preview->isVisible());
        QTRY_COMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 工程"));
        QVERIFY(selfPhoto->property("photoSource").toString().contains(QStringLiteral("engineer")));
        // 本车移出我方列表：六台 → 五台，且被移走的正是工程那一格。
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), allSlots - 1);
        QCOMPARE(friendlyTeamRow->width(), enemyTeamRow->width());
        QVERIFY(findVisual(friendlyTeamRow, QStringLiteral("friendlySlot0"))->width()
                > findVisual(enemyTeamRow, QStringLiteral("enemySlot0"))->width());
        QVERIFY(!findVisual(friendlyTeamRow, QStringLiteral("friendlySlot4")));
        QVERIFY(findVisual(friendlyTeamRow, QStringLiteral("friendlySlot0")));
        QCOMPARE(enemyTeamRow->childItems().size(), allSlots);

        // 选择会落盘（进入终端时走的就是这条保存路径），下次启动仍生效。
        QVERIFY(terminal.connection()->save());
        const QString iniPath = QString::fromUtf8(qgetenv("RM27_SETTINGS_FILE"));
        QVERIFY(!iniPath.isEmpty());
        QSettings persisted(iniPath, QSettings::IniFormat);
        QCOMPARE(persisted.value(QStringLiteral("identity/own_robot")).toString(),
                 QStringLiteral("engineer"));

        // 非法 id（非 ASCII）被拒绝，等价于未指定，不会写出脏值。
        terminal.connection()->setOwnRobot(QStringLiteral("步兵"));
        QCOMPARE(terminal.connection()->ownRobot(), QString());
        terminal.connection()->setOwnRobot(QString());
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), allSlots);
    }

    void displayPreferences()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        auto* settings = terminal.connection();
        const int originalPort = settings->refereePort();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        window->resize(1180, 680);
        auto* audio = window->findChild<QAudioOutput*>("videoAudioOutput");
        auto* map = window->findChild<QQuickItem*>("minimap");
        auto* aim = window->findChild<QQuickItem*>("aimOverlay");
        auto* fps = window->findChild<QQuickItem*>("performanceReadout");
        auto* panel = window->findChild<QQuickItem*>("startPanel");
        QVERIFY(audio && map && aim && fps && panel);
        QCOMPARE(settings->volume(), 60);
        settings->setVolume(0);
        QTRY_COMPARE(audio->volume(), 0.0);
        settings->setVolume(140);
        QTRY_COMPARE(audio->volume(), 1.0);
        settings->setMouseSensitivity(0);
        QCOMPARE(settings->mouseSensitivity(), 10);
        settings->setMouseSensitivity(500);
        QCOMPARE(settings->mouseSensitivity(), 300);
        QVERIFY(panel->setProperty("displayExpanded", true));
        auto* controls = window->findChild<QQuickItem*>("displaySettings");
        QVERIFY(controls && controls->isVisible());
        auto* mapToggle = window->findChild<QQuickItem*>("mapToggle");
        QVERIFY(mapToggle);
        mapToggle->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(!settings->showMap());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(settings->showMap());
        // 原图始终由本地用户选择；测试使用隔离图或显式指定的本机原图，不读取实际配置。
        QTemporaryDir directory;
        QString imagePath = qEnvironmentVariable("RM27_TEST_MAP");
        if (imagePath.isEmpty()) {
            imagePath = directory.filePath("test-map.png");
            QImage image(720, 400, QImage::Format_RGB32);
            image.fill(QColor("#253549"));
            QVERIFY(image.save(imagePath));
        }
        const QString imageUrl = QUrl::fromLocalFile(imagePath).toString();
        settings->setMapImage(imageUrl);
        QTRY_VERIFY(map->property("imageReady").toBool());
        settings->setMapImage(QStringLiteral("https://example.com/map.png"));
        QCOMPARE(settings->mapImage(), QString());
        settings->setMapImage(QStringLiteral("file://server/share/map.png"));
        QCOMPARE(settings->mapImage(), QString());
        settings->setMapImage(imageUrl);
        dismissStartPanel(window);
        settings->setShowMap(false);
        settings->setShowCrosshair(false);
        settings->setShowFps(false);
        QTRY_VERIFY(!map->isVisible() && !fps->isVisible());
        QTRY_VERIFY(!aim->property("crosshairEnabled").toBool());
        settings->setShowMap(true);
        settings->setShowCrosshair(true);
        settings->setShowFps(true);
        QTRY_VERIFY(map->isVisible() && fps->isVisible());
        QTRY_VERIFY(aim->property("crosshairEnabled").toBool());
        settings->setFullscreen(true);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        settings->setFullscreen(false);
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        window->resize(1600, 900);
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-map-settings-preview.png")));
        QVERIFY(window->setProperty("startPanelVisible", true));
        auto* body = window->findChild<QQuickItem*>("startPanelBody");
        auto* flickable = body->property("contentItem").value<QQuickItem*>();
        QVERIFY(flickable);
        QVERIFY(flickable->setProperty("contentY", qMax(0.0,
            flickable->property("contentHeight").toReal() - flickable->height())));
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-display-settings-preview.png")));
        settings->setShowMap(false);
        settings->setShowCrosshair(false);
        settings->setShowFps(false);
        settings->setFullscreen(true);
        QVERIFY(settings->save());
        rm27::app::ConnectionSettings restored;
        QCOMPARE(restored.volume(), 100);
        QCOMPARE(restored.mouseSensitivity(), 300);
        QCOMPARE(restored.mapImage(), imageUrl);
        QVERIFY(!restored.showMap() && !restored.showCrosshair() && !restored.showFps());
        QVERIFY(restored.fullscreen());
        settings->setFullscreen(false);
        QCOMPARE(settings->refereePort(), originalPort);
        QVERIFY(terminal.uiOccupied() && !terminal.controlsArmed());
    }

    void mqttConnectionStatus()
    {
        QTcpServer broker;
        QVERIFY(broker.listen(QHostAddress::LocalHost, 0));
        TerminalController terminal(false, nullptr, testRefereePort, true, broker.serverPort());
        QVERIFY(terminal.mqttEnabled());
        QVERIFY(!terminal.mqttConnected());
        QTRY_VERIFY(broker.hasPendingConnections());
        auto* peer = broker.nextPendingConnection();
        QTRY_VERIFY(peer->bytesAvailable() >= 31);
        peer->readAll();
        peer->write(QByteArray("\x20\x02\x00\x00", 4));
        QTRY_VERIFY(peer->bytesAvailable() > 0);
        peer->readAll();
        QVERIFY(!terminal.mqttConnected()); // TCP 已连不等于订阅建立。
        peer->write(QByteArray("\x90\x03\x00\x01\x00", 5));
        QTRY_VERIFY(terminal.mqttConnected());
        QVERIFY(!terminal.refereeFresh()); // 订阅建立也不等于收到新鲜数据。
        peer->disconnectFromHost();
        QTRY_VERIFY(!terminal.mqttConnected());
        QVERIFY(!terminal.controlsArmed());
    }

    // 折叠只影响表现层，预设身份不冒充有效数据；小窗口和宽屏都不遮挡底部入口。
    void uiHierarchy()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto* panel = window->findChild<QQuickItem*>("startPanel");
        auto* diagnostics = window->findChild<QQuickItem*>("startPanelDiagnostics");
        auto* performance = window->findChild<QQuickItem*>("startPanelPerformance");
        auto* fields = window->findChild<QQuickItem*>("startPanelEndpointFields");
        auto* interfaces = window->findChild<QQuickItem*>("startPanelInterfaces");
        auto* keymap = window->findChild<QQuickItem*>("startPanelKeymap");
        auto* enter = window->findChild<QQuickItem*>("startPanelEnter");
        auto* baseCaption = window->findChild<QQuickItem*>("friendly_baseHp");
        auto* hud = window->findChild<QQuickItem*>("hudOverlay");
        auto* fallback = window->findChild<QQuickItem*>("videoFallbackText");
        auto* videoState = window->findChild<QQuickItem*>("videoStateText");
        QVERIFY(panel && diagnostics && performance && fields && interfaces && keymap && enter);
        QVERIFY(baseCaption && hud && fallback && videoState);
        QVERIFY(!window->findChild<QQuickItem*>("ownTeamBadge"));
        QVERIFY(!window->findChild<QQuickItem*>("teamCoinBadge"));
        QVERIFY(!diagnostics->isVisible() && !performance->isVisible());
        QVERIFY(!fields->isVisible() && !interfaces->isVisible() && !keymap->isVisible());
        const auto click = [window](QQuickItem* item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(item->boundingRect().center()).toPoint());
        };
        window->resize(1180, 680);
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-start-compact-1180x680.png")));
        const int portBefore = terminal.connection()->refereePort();
        auto* advancedToggle = window->findChild<QQuickItem*>("startPanelAdvancedToggle");
        auto* diagnosticsToggle = window->findChild<QQuickItem*>("startPanelDiagnosticsToggle");
        auto* shortcutsToggle = window->findChild<QQuickItem*>("startPanelShortcutsToggle");
        auto* body = window->findChild<QQuickItem*>("startPanelBody");
        auto* offline = window->findChild<QQuickItem*>("startPanelOfflineButton");
        QVERIFY(advancedToggle && diagnosticsToggle && shortcutsToggle && body && offline);
        click(advancedToggle);
        QTRY_VERIFY(fields->isVisible() && interfaces->isVisible());
        click(diagnosticsToggle);
        QTRY_VERIFY(diagnostics->isVisible() && performance->isVisible());
        shortcutsToggle->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(keymap->isVisible());
        QCOMPARE(terminal.connection()->refereePort(), portBefore);
        QVERIFY(!terminal.controlsArmed());
        QTest::qWait(100);
        auto* flickable = body->property("contentItem").value<QQuickItem*>();
        QVERIFY(flickable);
        QVERIFY(flickable->setProperty("contentY", qMax(0.0,
            flickable->property("contentHeight").toReal() - flickable->height())));
        QTest::qWait(100);
        QVERIFY(offline->mapToScene(QPointF()).y() >= body->mapToScene(QPointF()).y());
        QVERIFY(offline->mapToScene(offline->boundingRect().bottomRight()).y()
                <= body->mapToScene(body->boundingRect().bottomRight()).y());
        QVERIFY(enter->mapToScene(enter->boundingRect().bottomRight()).y() <= window->height());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-start-expanded-1180x680.png")));
        QVERIFY(flickable->setProperty("contentY", 0));
        QTest::qWait(100);
        QVERIFY(panel->setProperty("shortcutsExpanded", false));
        click(diagnosticsToggle);
        click(advancedToggle);
        QTRY_VERIFY(!fields->isVisible() && !diagnostics->isVisible() && !keymap->isVisible());
        QVERIFY(enter->mapToScene(enter->boundingRect().bottomRight()).y() <= window->height());

        terminal.connection()->setOwnTeam(QStringLiteral("blue"));
        dismissStartPanel(window);
        QTRY_COMPARE(baseCaption->property("text").toString(), QStringLiteral("我方 · 蓝方 基地 --"));
        terminal.connection()->setOwnTeam(QStringLiteral("red"));
        QTRY_COMPARE(baseCaption->property("text").toString(), QStringLiteral("我方 · 红方 基地 --"));
        QCOMPARE(fallback->property("text").toString(),
                 QStringLiteral("尚未选择画面\n从启动面板选择本机图传或本地回放"));
        QVERIFY(fallback->isVisible());
        QVERIFY(!videoState->isVisible());
        for (const QSize size : {QSize(1180, 680), QSize(1600, 900), QSize(2560, 1440)}) {
            window->resize(size);
            QTest::qWait(100);
            auto* enemy = window->findChild<QQuickItem*>("enemyTeam");
            auto* friendly = window->findChild<QQuickItem*>("friendlyTeam");
            auto* telemetry = window->findChild<QQuickItem*>("rightTelemetry");
            auto* structures = window->findChild<QQuickItem*>("friendlyStructures");
            auto* map = window->findChild<QQuickItem*>("minimap");
            auto* weapon = window->findChild<QQuickItem*>("weaponPanel");
            QVERIFY(enemy && friendly && telemetry && structures && map && weapon);
            QVERIFY(enemy->x() + enemy->width() < friendly->x());
            QCOMPARE(enemy->width(), friendly->width());
            QVERIFY(qAbs(enemy->x() + friendly->x() + friendly->width() - window->width()) < 0.01);
            auto* enemyBase = window->findChild<QQuickItem*>("enemy_baseCapsule");
            auto* friendlyBase = window->findChild<QQuickItem*>("friendly_baseCapsule");
            auto* enemyOutpost = window->findChild<QQuickItem*>("enemy_outpostCapsule");
            auto* friendlyOutpost = window->findChild<QQuickItem*>("friendly_outpostCapsule");
            QVERIFY(enemyBase && friendlyBase && enemyOutpost && friendlyOutpost);
            QCOMPARE(enemyBase->width(), friendlyBase->width());
            QVERIFY(enemyOutpost->x() > enemyBase->x());
            QVERIFY(friendlyOutpost->x() < friendlyBase->x());
            QVERIFY(friendlyOutpost->height() < friendlyBase->height());
            QVERIFY(friendlyOutpost->property("color").value<QColor>().alpha()
                    < friendlyBase->property("color").value<QColor>().alpha());
            QCOMPARE(friendlyBase->property("radius").toDouble(), friendlyBase->height() / 2);
            QVERIFY(friendlyBase->property("color").value<QColor>().alpha() > 200);
            QVERIFY(structures->y() + structures->height() < friendly->y());
            QVERIFY(telemetry->x() > window->width() / 2);
            QVERIFY(telemetry->y() >= friendly->y() + friendly->height());
            QVERIFY(map->width() > window->width() * 0.20);
            QVERIFY(map->width() < window->width() * 0.25);
            QCOMPARE(map->opacity(), 1.0); // Dim the base image, not coordinate markers.
            QVERIFY(map->y() > weapon->y() + weapon->height());
            auto* actions = window->findChild<QQuickItem*>("actionStates");
            auto* hint = window->findChild<QQuickItem*>("actionHint");
            QVERIFY(actions && hint);
            const double unit = hud->property("unit").toReal();
            auto* aim = window->findChild<QQuickItem*>("aimOverlay");
            QVERIFY(aim);
            const double radius = aim->property("arcRadius").toReal();
            QVERIFY(qAbs(radius - window->width() * 0.205) < 0.01);
            QCOMPARE(aim->property("arcLineWidth").toReal(), 1.8);
            QVERIFY(qAbs(aim->property("arcHalfAngle").toReal() - std::acos(-1) * 0.17) < 0.001);
            QVERIFY(hint->mapRectToScene(hint->boundingRect()).bottom() + 8 <= hud->property("bottomClearance").toReal());
            const QRectF mapRect = map->mapRectToScene(map->boundingRect());
            QVERIFY(!hint->mapRectToScene(hint->boundingRect()).intersects(mapRect));
            QVERIFY(qAbs(actions->property("spacing").toReal() - 8 * unit) < 0.01);
            QVERIFY(qAbs(actions->x() - window->width() * 0.705) < 0.01);
            const QRectF actionBounds = actions->mapRectToScene(actions->property("childrenRect").toRectF());
            const double actionLowerEdge = actionBounds.right() + 8 * unit > mapRect.left()
                ? qMin(hud->property("bottomClearance").toReal(), mapRect.top())
                : hud->property("bottomClearance").toReal();
            QVERIFY(qAbs(actions->y() - qMin(window->height() * 0.65,
                actionLowerEdge - actions->height() - 8 * unit)) < 0.01);
            QRectF previous;
            QPointF previousStart;
            for (const auto* name : {"climbIndicator", "fireIndicator", "speedUpIndicator"}) {
                auto* indicator = window->findChild<QQuickItem*>(name);
                QVERIFY(indicator && indicator->parentItem() == actions);
                QCOMPARE(indicator->rotation(), 0.0);
                const QPointF start = indicator->mapToScene(QPointF(0, indicator->height() / 2));
                const QPointF baseline = indicator->mapToScene(QPointF(indicator->width(), indicator->height() / 2))
                    - indicator->mapToScene(QPointF(0, indicator->height() / 2));
                QVERIFY2(qAbs(baseline.y()) < 0.001, qPrintable(QStringLiteral("%1: baseline.y=%2")
                    .arg(QString::fromLatin1(name)).arg(baseline.y())));
                QVERIFY(baseline.x() > 0);
                const QRectF bounds = indicator->mapRectToScene(indicator->boundingRect());
                QVERIFY(bounds.left() >= window->width() * 0.68); // 整组让出更宽的中央视野。
                QVERIFY(bounds.top() > window->height() / 2);
                QVERIFY(bounds.bottom() < hud->property("bottomClearance").toReal());
                QVERIFY(!bounds.intersects(QRectF(window->width() * 0.38, window->height() * 0.39,
                    window->width() * 0.24, window->height() * 0.22)));
                QVERIFY2(!bounds.intersects(mapRect), qPrintable(QStringLiteral("%1 %2x%3: text=(%4,%5,%6,%7) map=(%8,%9,%10,%11) font=%12")
                    .arg(QString::fromLatin1(name)).arg(window->width()).arg(window->height())
                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                    .arg(mapRect.x()).arg(mapRect.y()).arg(mapRect.width()).arg(mapRect.height())
                    .arg(indicator->property("font").value<QFont>().family())));
                QVERIFY(!bounds.intersects(weapon->mapRectToScene(weapon->boundingRect())));
                QVERIFY2(!bounds.intersects(previous), qPrintable(QStringLiteral("%1 %2×%3: top=%4 bottom=%5 previousBottom=%6")
                    .arg(QString::fromLatin1(name)).arg(window->width()).arg(window->height())
                    .arg(bounds.top()).arg(bounds.bottom()).arg(previous.bottom())));
                if (!previous.isEmpty()) {
                    // 首字水平向内退，三个首字共线，而非竖直左对齐。
                    const QPointF offset = start - previousStart;
                    QVERIFY(offset.x() < 0);
                    QVERIFY(qAbs(offset.x() + 0.4 * offset.y()) < 0.01);
                    QVERIFY2(bounds.top() >= previous.bottom() + 4 * unit,
                        qPrintable(QStringLiteral("%1 %2×%3: gap=%4 required=%5")
                            .arg(QString::fromLatin1(name)).arg(window->width()).arg(window->height())
                            .arg(bounds.top() - previous.bottom()).arg(4 * unit)));
                    QVERIFY(bounds.top() - previous.bottom() <= 8 * unit + 2);
                }
                previous = bounds;
                previousStart = start;
                const auto font = indicator->property("font").value<QFont>();
                QVERIFY(qAbs(font.pixelSize() - qMax(14.0, 18 * unit)) <= 0.5);
                QCOMPARE(font.weight(), QFont::DemiBold);
            }
            // 隐藏地图时恢复原来的低位；重新显示后按实际文字宽度避让。
            terminal.connection()->setShowMap(false);
            QTRY_VERIFY(qAbs(actions->y() - qMin(window->height() * 0.65,
                hud->property("bottomClearance").toReal() - actions->height() - 8 * unit)) < 0.01);
            terminal.connection()->setShowMap(true);
            QTRY_VERIFY(!actions->mapRectToScene(actions->property("childrenRect").toRectF())
                .intersects(map->mapRectToScene(map->boundingRect())));
            // 保留的上半段镜像一致，裁剪不改轮廓；右下半段不绘制。
            const QImage arcImage = window->grabWindow();
            // 逐行测量实际弧线像素重心，不能只检查半径参数或三处亮度。
            const double scale = double(arcImage.width()) / window->width();
            double maxMirrorError = 0;
            const int topRow = qCeil((window->height() / 2.0
                - radius * std::sin(std::acos(-1) * 0.17) + 3) * scale);
            const int bottomRow = qFloor((window->height() / 2.0 - 4) * scale);
            for (int row = topRow; row <= bottomRow; ++row) {
                const double dy = (row + 0.5) / scale - window->height() / 2.0;
                const double dx = std::sqrt(radius * radius - dy * dy);
                double centers[2];
                for (int side = 0; side < 2; ++side) {
                    const int expected = qRound((window->width() / 2.0 + (side ? dx : -dx)) * scale);
                    const int span = qCeil(5 * scale);
                    int background = 255;
                    for (int column = expected - span; column <= expected + span; ++column)
                        background = qMin(background, arcImage.pixelColor(column, row).green());
                    double weighted = 0, total = 0;
                    for (int column = expected - span; column <= expected + span; ++column) {
                        const int weight = arcImage.pixelColor(column, row).green() - background;
                        weighted += (column + 0.5) * weight;
                        total += weight;
                    }
                    QVERIFY(total > 10);
                    centers[side] = weighted / total;
                }
                maxMirrorError = qMax(maxMirrorError, qAbs(centers[0] + centers[1] - arcImage.width()) / scale);
            }
            qInfo("UPPER_ARC_MIRROR %dx%d maxDeviation=%.3f logical pixels",
                window->width(), window->height(), maxMirrorError);
            QVERIFY(maxMirrorError <= 1.0);
            const auto arcBrightness = [&](bool right, double angle) {
                const QPoint point(qRound((window->width() / 2.0 + (right ? 1 : -1) * radius * std::cos(angle))
                    * arcImage.width() / window->width()),
                    qRound((window->height() / 2.0 + radius * std::sin(angle)) * arcImage.height() / window->height()));
                int peak = 0;
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                        peak = qMax(peak, arcImage.pixelColor(point + QPoint(dx, dy)).green());
                return peak;
            };
            for (const double fraction : {0.04, 0.10, 0.15}) {
                const double upperAngle = -std::acos(-1) * fraction;
                QVERIFY(qAbs(arcBrightness(true, upperAngle) - arcBrightness(false, upperAngle)) < 12);
            }
            const double lowerAngle = std::acos(-1) * 0.04;
            QVERIFY(arcBrightness(true, lowerAngle) + 8 < arcBrightness(false, lowerAngle));
            QVERIFY(enemy->y() + enemy->height() < weapon->y());
            QVERIFY(videoState->parentItem()->y() > hud->property("rosterBottom").toReal());
            auto* photo = findVisual(enemy, QStringLiteral("enemySlotPhoto0"));
            QVERIFY(photo);
            QCOMPARE(photo->property("photoOpacity").toReal(), 0.72);
            QVERIFY(qAbs(photo->height() - 62 * unit) < 0.01);
            QVERIFY(qAbs(photo->width() - (enemy->width() - 5 * 12 * unit) / 6) < 0.01);
            auto* toggle = window->findChild<QQuickItem*>("topStatusToggle");
            QVERIFY(toggle);
            QVERIFY(enemy->x() + enemy->width() + 8 <= toggle->x());
            QVERIFY(toggle->x() + toggle->width() + 8 <= friendly->x());
        }
        window->resize(1600, 900);
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-hud-preset-1600x900.png")));
        auto* topStrip = window->findChild<QQuickItem*>("topStatusStrip");
        auto* topToggle = window->findChild<QQuickItem*>("topStatusToggle");
        auto* roster = window->findChild<QQuickItem*>("friendlyTeam");
        auto* structures = window->findChild<QQuickItem*>("friendlyStructures");
        QVERIFY(topStrip && topToggle && roster && structures);
        const double rosterY = roster->y(), structureY = structures->y();
        click(topToggle);
        QTest::qWait(50);
        QVERIFY(qAbs(roster->y() - rosterY - topStrip->y()) < 0.01);
        QVERIFY(qAbs(structures->y() - structureY - topStrip->y()) < 0.01);
        QTRY_COMPARE(topStrip->y(), -topStrip->height());
        QVERIFY(topToggle->y() >= 0 && topToggle->isVisible());
        QCOMPARE(roster->y(), rosterY - topStrip->height());
        QCOMPARE(structures->y(), structureY - topStrip->height());
        QVERIFY(structures->y() >= 0);
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-top-collapsed-1600x900.png")));
        topToggle->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(topStrip->y(), 0.0);
        // Drag up/down: buildings and rosters follow the same strip animation.
        auto from = topToggle->mapToScene(topToggle->boundingRect().center()).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, from - QPoint(0, 16), 30);
        QTest::mouseMove(window, from - QPoint(0, 30), 30);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from - QPoint(0, 30));
        QTRY_COMPARE(topStrip->y(), -topStrip->height());
        QCOMPARE(roster->y(), rosterY - topStrip->height());
        from = topToggle->mapToScene(topToggle->boundingRect().center()).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, from + QPoint(0, 16), 30);
        QTest::mouseMove(window, from + QPoint(0, 35), 30);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + QPoint(0, 35));
        QTRY_COMPARE(topStrip->y(), 0.0);
        QCOMPARE(roster->y(), rosterY);
        QCOMPARE(structures->y(), structureY);
        QVERIFY(!terminal.controlsArmed());
        terminal.enterOfflineDrill();
        QTRY_COMPARE(structures->y(), structureY + 26);
        auto* offlineBanner = window->findChild<QQuickItem*>("offlineDrillBanner");
        QVERIFY(offlineBanner && offlineBanner->isVisible());
        QVERIFY(topToggle->x() > window->width() * 0.75);
        QVERIFY(topToggle->x() + topToggle->width() <= window->width());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-offline-hud-1600x900.png")));
        dismissStartPanel(window);
        click(topToggle);
        QTRY_COMPARE(topStrip->y(), -topStrip->height());
        QCOMPARE(offlineBanner->y(), 0.0);
        QVERIFY(offlineBanner->isVisible());
        QCOMPARE(structures->y(), structureY + 26 - topStrip->height());
        QCOMPARE(roster->y(), rosterY + 26 - topStrip->height());
        QVERIFY(structures->y() >= offlineBanner->y() + offlineBanner->height());
        QVERIFY(window->grabWindow().save(rm27::app::previewPath("ui-offline-top-collapsed-1600x900.png")));
    }

    void radarAndRefereeFreshness()
    {
        TerminalController terminal(false, nullptr, testRefereePort);
        terminal.connection()->setOwnTeam(QStringLiteral("blue"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* waiting = window->findChild<QQuickItem*>("mapWaitingLabel");
        QVERIFY(waiting && waiting->isVisible());
        QFile fixtures(QDir(QFileInfo(QStringLiteral(RM27_QML_SOURCE)).absolutePath())
                           .filePath("../contracts/sim-v1-fixtures.json"));
        QVERIFY(fixtures.open(QIODevice::ReadOnly));
        auto referee = QJsonDocument::fromJson(fixtures.readAll()).object()
                           .value("cases").toArray().first().toObject().value("packet").toObject();
        auto radar = QJsonDocument::fromJson(R"json({
            "channel":"rm27.sim.v1.radar","sequence":0,"sent_at_unix_ms":1790000000000,
            "red":[{"id":0,"x":0.8,"y":0.7}],
            "blue":[{"id":1,"x":0.3,"y":0.4}]
        })json").object();
        QUdpSocket sender;
        const auto send = [&](const QJsonObject& packet) {
            const auto payload = QJsonDocument(packet).toJson(QJsonDocument::Compact);
            QCOMPARE(sender.writeDatagram(payload, QHostAddress::LocalHost, testRefereePort), payload.size());
        };
        QTimer refereeTimer, radarTimer, duplicateTimer;
        refereeTimer.setInterval(100);
        radarTimer.setInterval(100);
        connect(&refereeTimer, &QTimer::timeout, &refereeTimer, [&] {
            referee.insert("sequence", referee.value("sequence").toDouble() + 1);
            send(referee);
        });
        connect(&radarTimer, &QTimer::timeout, &radarTimer, [&] {
            radar.insert("sequence", radar.value("sequence").toDouble() + 1);
            send(radar);
        });
        refereeTimer.start();
        QTRY_COMPARE(terminal.mapSnapshot().value("source").toString(), QStringLiteral("simulation-referee"));
        QTRY_VERIFY(!waiting->isVisible());
        send(radar);
        QTRY_COMPARE(terminal.mapSnapshot().value("source").toString(), QStringLiteral("simulation-radar"));
        QCOMPARE(terminal.mapSnapshot().value("friendlyPositions").toList().first().toMap().value("x").toDouble(), 0.3);
        auto reordered = radar;
        reordered.insert("blue", QJsonArray{QJsonObject{{"id", 1}, {"x", 0.9}, {"y", 0.9}}});
        duplicateTimer.setInterval(100);
        connect(&duplicateTimer, &QTimer::timeout, &duplicateTimer, [&] { send(reordered); });
        duplicateTimer.start(); // continuing duplicate traffic must not keep radar fresh
        send(reordered);
        QTest::qWait(50);
        QCOMPARE(terminal.mapSnapshot().value("friendlyPositions").toList().first().toMap().value("x").toDouble(), 0.3);
        QTRY_COMPARE_WITH_TIMEOUT(terminal.mapSnapshot().value("source").toString(),
                                 QStringLiteral("simulation-referee"), 2000);
        QVERIFY(terminal.refereeFresh()); // new referee packets cannot keep old radar fresh
        duplicateTimer.stop();
        radar.insert("sequence", 0);
        radarTimer.start();
        QTRY_COMPARE(terminal.mapSnapshot().value("source").toString(), QStringLiteral("simulation-radar"));
        refereeTimer.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.refereeFresh(), 2000);
        QVERIFY(terminal.hudSnapshot().isEmpty());
        QCOMPARE(terminal.mapSnapshot().value("source").toString(), QStringLiteral("simulation-radar"));
        terminal.connection()->setOwnTeam(QStringLiteral("red"));
        QCOMPARE(terminal.mapSnapshot().value("friendlyPositions").toList().first().toMap().value("x").toDouble(), 0.8);
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(terminal.applyConnectionProfile());
        QVERIFY(terminal.mapSnapshot().isEmpty()); // rebind clears previous session immediately
        QTRY_COMPARE(terminal.mapSnapshot().value("source").toString(), QStringLiteral("simulation-radar"));
        radarTimer.stop();
        QTRY_VERIFY_WITH_TIMEOUT(terminal.mapSnapshot().isEmpty(), 2000);
        QTRY_VERIFY(waiting->isVisible());
        QVERIFY(!terminal.controlsArmed());
    }

    void optionalCameraSmoke()
    {
        if (qEnvironmentVariableIsEmpty("RM27_TEST_CAMERA"))
            QSKIP("Set RM27_TEST_CAMERA=1 to test a local camera");
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No camera is available on this host");

        TerminalController terminal(false, nullptr, testRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* panel = window->findChild<QQuickItem*>("operatorCameraPanel");
        auto* camera = window->findChild<QCamera*>("operatorCamera");
        auto* preview = window->findChild<QQuickItem*>("operatorCameraPreview");
        auto* selector = window->findChild<QQuickItem*>("operatorCameraSelector");
        auto* cameraOpen = window->findChild<QQuickItem*>("operatorCameraOpen");
        QVERIFY(panel && camera && preview && selector && cameraOpen);
        QVERIFY(!camera->isActive());
        QVERIFY(!selector->isVisible());
        QVERIFY(selector->property("count").toInt() > 0);
        const auto fixture = QUrl::fromLocalFile(qEnvironmentVariable("RM27_TEST_VIDEO"));
        QVERIFY(QMetaObject::invokeMethod(window, "openLocalVideo", Q_ARG(QUrl, fixture)));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        auto* sink = preview->property("videoSink").value<QVideoSink*>();
        QVERIFY(sink);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          cameraOpen->mapToScene(cameraOpen->boundingRect().center()).toPoint());
        QTRY_VERIFY(selector->isVisible());
        QVERIFY(QMetaObject::invokeMethod(panel, "selectCamera", Q_ARG(int, 0)));
        QTRY_VERIFY_WITH_TIMEOUT(camera->isActive(), 5000);
        QVERIFY(!selector->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(sink->videoFrame().isValid(), 7000);
        const int framesBeforeClose = terminal.decodedFrames();
        QVERIFY(QMetaObject::invokeMethod(panel, "stopCamera"));
        QTRY_VERIFY(!camera->isActive());
        QVERIFY(!preview->isVisible());
        QTRY_COMPARE(panel->height(), 28.0);
        QTRY_VERIFY(terminal.decodedFrames() > framesBeforeClose);
        QVERIFY(!terminal.controlsArmed());
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    // 测试使用独立的配置文件：绝不读写用户真实的启动面板设置。
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) return 1;
    const QString settingsFile = settingsDirectory.filePath(QStringLiteral("rm27-test-connection.ini"));
    qputenv("RM27_SETTINGS_FILE", settingsFile.toUtf8());
    PlaybackTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_video_playback.moc"
