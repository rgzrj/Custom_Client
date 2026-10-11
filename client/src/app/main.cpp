#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QUrl>
#include <QQuickStyle>

#include "app/terminal_controller.hpp"
#include "app/runtime_log.hpp"
#include <QDebug>
#include <QScopeGuard>
#include <mutex>
#include <QMediaPlayer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#if defined(RM27_RM2026_VIDEO_HUD) || defined(RM27_RM2026_LOCAL_HUD)
#include <QCommandLineParser>
#endif
#if defined(RM27_RM2026_VIDEO_HUD) || defined(RM27_RM2026_LOCAL_HUD)
#include "video/historical_video_receiver.hpp"
#include <QUdpSocket>
#endif
#ifdef RM27_RM2026_LOCAL_HUD
#include "protocol/rm2026/mqtt_observer.hpp"
#include <QJsonArray>
#include <iostream>
#include <memory>
#endif

namespace {
rm27::app::RuntimeLog* runtimeLog = nullptr;
std::mutex logHandlerMutex;
void logMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    std::lock_guard<std::mutex> guard(logHandlerMutex);
    static const char* levels[] = {"DEBUG", "WARN", "ERROR", "FATAL", "INFO"};
    if (runtimeLog) runtimeLog->append(QString::fromLatin1(levels[type]) + ' ' + context.category + ' ' + message);
}
}

#if defined(RM27_RM2026_VIDEO_HUD)
namespace {
void addPairingOptions(QCommandLineParser& parser)
{
    parser.addOption({"pairing-robot-id", "Explicit pairing target; independent of display preferences.", "id"});
    parser.addOption({"pairing-region", "Official pairing procedure: global or japan.", "region", "global"});
    parser.addOption({"pairing-confirmed", "Operator reports official pairing completed; not hardware verification."});
}

bool startPairing(const QCommandLineParser& parser, TerminalController& terminal)
{
    if (!parser.isSet("pairing-robot-id"))
        return !parser.isSet("pairing-region") && !parser.isSet("pairing-confirmed");
    bool valid = false;
    const auto rawId = parser.value("pairing-robot-id");
    const int id = rawId.toInt(&valid);
    if (!valid || QString::number(id) != rawId
        || !terminal.beginPairing(parser.value("pairing-region"), id)) return false;
    return !parser.isSet("pairing-confirmed")
        || terminal.confirmExternalPairing(terminal.pairingDiagnostics().value("session").toULongLong());
}
}
#endif

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    rm27::app::RuntimeLog log;
    runtimeLog = &log;
    const auto previousHandler = qInstallMessageHandler(logMessage);
    // Restore the handler before destroying the worker, including early CLI failures.
    const auto handlerGuard = qScopeGuard([&] {
        qInstallMessageHandler(previousHandler);
        std::lock_guard<std::mutex> guard(logHandlerMutex);
        runtimeLog = nullptr;
    });
    qInfo().noquote() << "START terminal; log=" << log.path();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication::setOrganizationName(QStringLiteral("RM27"));
    QGuiApplication::setApplicationName(QStringLiteral("RM2027 自定义终端"));

#ifdef RM27_RM2026_VIDEO_HUD
    QCommandLineParser parser;
    parser.setApplicationDescription("RM2026 read-only HEVC experiment; no control.");
    addPairingOptions(parser);
    parser.addHelpOption();
    parser.addOption({"protocol", "Explicit historical protocol.", "version"});
    parser.addOption({"video-port", "UDP input port (must be explicitly selected).", "port"});
    parser.addOption({"video-bind", "Specific local IPv4 address; no wildcard binding.", "address", "127.0.0.1"});
    parser.addOption({"video-source", "Only accept this sender IPv4 address.", "address", "127.0.0.1"});
    parser.addOption({"allow-network-video", "Explicitly allow non-loopback read-only video input."});
    parser.addOption({"fragment-base", "Test sender fragment base, explicitly 0 or 1.", "index"});
    parser.addOption({"experimental-video", "Acknowledge experimental framing assumptions."});
    parser.addOption({"duration-ms", "Optional bounded run, 1..60000 ms.", "milliseconds"});
    parser.process(app);
    bool valid = false;
    const unsigned inputPort = parser.value("video-port").toUInt(&valid);
    if (!valid || inputPort == 0 || inputPort > 65535
        || parser.value("protocol") != "rm2026-v2.0.0"
        || !parser.isSet("experimental-video")
        || (parser.value("fragment-base") != "0" && parser.value("fragment-base") != "1")) return 2;
    QHostAddress videoBind, videoSource;
    if (!videoBind.setAddress(parser.value("video-bind"))
        || !videoSource.setAddress(parser.value("video-source"))
        || videoBind.protocol() != QAbstractSocket::IPv4Protocol
        || videoSource.protocol() != QAbstractSocket::IPv4Protocol
        || ((!videoBind.isLoopback() || !videoSource.isLoopback())
            && !parser.isSet("allow-network-video"))) return 2;
    if (parser.isSet("duration-ms")) {
        const int duration = parser.value("duration-ms").toInt(&valid);
        if (!valid || duration < 1 || duration > 60000) return 2;
        QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    }
    QGuiApplication::setApplicationName(QStringLiteral("RM2026 HEVC 本机图传实验"));
    QUdpSocket decoderReservation;
    if (!decoderReservation.bind(QHostAddress::LocalHost, 0)) return 2;
    const quint16 decoderPort = decoderReservation.localPort();
    if (decoderPort == inputPort) return 2;
    decoderReservation.close();
    rm27::video::HistoricalVideoReceiver videoReceiver(parser.value("fragment-base").toUInt());
    TerminalController terminal(false, nullptr, 0, false, 0, false, true);
    if (!startPairing(parser, terminal)) return 2;
    terminal.connection()->setSimulationVideoPort(decoderPort);
    const bool simulationUdpVideo = false;
    const QUrl initialVideo;
#elif defined(RM27_RM2026_LOCAL_HUD)
    QCommandLineParser parser;
    parser.setApplicationDescription("RM2026 synthetic loopback-only read-only QML HUD; no real control.");
    parser.addHelpOption();
    parser.addOption({"protocol", "Explicit historical version.", "version"});
    parser.addOption({"host", "Only literal 127.0.0.1 is accepted.", "host", "127.0.0.1"});
    parser.addOption({"port", "Local synthetic peer port.", "port", "39005"});
    parser.addOption({"duration-ms", "Optional bounded run, 1..60000 ms.", "milliseconds"});
    parser.addOption({"synthetic-demo", "Explicit synthetic local test acknowledgement."});
    parser.addOption({"sim-video-port", "Optional loopback MPEG-TS test port; NOT historical video framing.", "port"});
    parser.addOption({"hevc-video-port", "Optional synthetic loopback HEVC fragment port.", "port"});
    parser.addOption({"fragment-base", "Synthetic HEVC sender fragment base, explicitly 0 or 1.", "index"});
    parser.addOption({"robot-id", "Optional decimal MQTT client ID; not hardware pairing or authorization.", "id"});
    parser.process(app);
    bool valid = false;
    const unsigned port = parser.value("port").toUInt(&valid);
    if (!valid || port == 0 || port > 65535 || parser.value("host") != "127.0.0.1"
        || parser.value("protocol") != rm27::protocol::rm2026::MqttObserver::version
        || !parser.isSet("synthetic-demo")) return 2;
    const bool simulationUdpVideo = parser.isSet("sim-video-port");
    const unsigned videoPort = parser.value("sim-video-port").toUInt(&valid);
    if (simulationUdpVideo && (!valid || videoPort == 0 || videoPort > 65535)) return 2;
    const bool syntheticHevc = parser.isSet("hevc-video-port");
    const unsigned hevcPort = parser.value("hevc-video-port").toUInt(&valid);
    if (syntheticHevc && (simulationUdpVideo || !valid || !hevcPort || hevcPort > 65535
        || (parser.value("fragment-base") != "0" && parser.value("fragment-base") != "1"))) return 2;
    if (!syntheticHevc && parser.isSet("fragment-base")) return 2;
    std::optional<unsigned> robotId;
    if (parser.isSet("robot-id")) {
        const auto id = parser.value("robot-id").toUInt(&valid);
        if (!valid || QString::number(id) != parser.value("robot-id")) return 2;
        robotId = id; // Role validation is shared with the existing observer.
    }
    if (parser.isSet("duration-ms")) {
        const int duration = parser.value("duration-ms").toInt(&valid);
        if (!valid || duration < 1 || duration > 60000) return 2;
        QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    }
    // Isolate this development build's preferences from the user's normal terminal.
    QGuiApplication::setApplicationName(QStringLiteral("RM2026 本机只读 HUD 测试"));
    std::unique_ptr<rm27::protocol::rm2026::MqttObserver> observer;
    try {
        observer = std::make_unique<rm27::protocol::rm2026::MqttObserver>(parser.value("protocol").toStdString(), port, robotId);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    TerminalController terminal(false, nullptr, 0, false, 0, true, syntheticHevc);
    if (simulationUdpVideo) terminal.connection()->setSimulationVideoPort(static_cast<int>(videoPort));
    std::unique_ptr<rm27::video::HistoricalVideoReceiver> syntheticVideo;
    QVariantMap syntheticVideoStats;
    const QUrl initialVideo;
#else
    bool demoMode = false;
    bool simulationUdpVideo = false;
    bool simulationMqtt = false;
    QUrl initialVideo;
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--demo")) {
            demoMode = true;
        }
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--sim-udp-video")) {
            simulationUdpVideo = true;
        }
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--sim-mqtt")) {
            simulationMqtt = true;
        }
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--video") && index + 1 < argc) {
            initialVideo = QUrl::fromLocalFile(QFileInfo(QString::fromLocal8Bit(argv[++index])).absoluteFilePath());
        }
    }

    TerminalController terminal(demoMode, nullptr, 0, simulationMqtt);
#endif
    QByteArray previousState;
    const auto recordState = [&] {
        const auto pairing = terminal.pairingDiagnostics();
        const auto state = QStringLiteral("STATE mode=%1 refereeFresh=%2 videoFresh=%3 videoStale=%4 armed=%5 pairing=%6 pairingSession=%7")
            .arg(terminal.modeLabel()).arg(terminal.refereeFresh()).arg(terminal.videoConnected())
            .arg(terminal.videoStale()).arg(terminal.controlsArmed())
            .arg(pairing.value("phase").toString()).arg(pairing.value("session").toString()).toUtf8();
        if (state != previousState) { log.append(QString::fromUtf8(state)); previousState = state; }
    };
    QObject::connect(&terminal, &TerminalController::stateChanged, &app, recordState);
    recordState();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("terminal"), &terminal);
    engine.rootContext()->setContextProperty(QStringLiteral("initialVideo"), initialVideo);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSimUdpVideo"), simulationUdpVideo);
    engine.loadFromModule(QStringLiteral("RM27.Terminal"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
#ifdef RM27_RM2026_VIDEO_HUD
    auto* root = engine.rootObjects().first();
    auto* player = root->findChild<QMediaPlayer*>(QStringLiteral("localPlayer"));
    if (!player) return 2;
    terminal.enterSimulationVideo();
    terminal.configureSimulationVideoPlayer(player);
    root->setProperty("simulationVideoSelected", true);
    const QUrl decoderUrl(QStringLiteral("udp://127.0.0.1:%1?localaddr=127.0.0.1&fifo_size=1024&overrun_nonfatal=1")
        .arg(decoderPort));
    player->setSource(decoderUrl);
    QObject::connect(player, &QMediaPlayer::sourceChanged, &videoReceiver, [&videoReceiver, decoderUrl](const QUrl& source) {
        if (source != decoderUrl) videoReceiver.stop();
    });
    QObject::connect(&videoReceiver, &rm27::video::HistoricalVideoReceiver::errorOccurred, &app,
        [](const QString& error) { qWarning().noquote() << error; });
    QObject::connect(&videoReceiver, &rm27::video::HistoricalVideoReceiver::statsChanged, &app,
        [&terminal, previous = QByteArray()](QVariantMap stats) mutable {
            stats.insert("decodedFrames", terminal.decodedFrames());
            const auto report = QJsonDocument(QJsonObject::fromVariantMap(stats)).toJson(QJsonDocument::Compact);
            if (report == previous) return;
            previous = report;
            qInfo().noquote() << "RM2026_HEVC_VIDEO" << report;
        });
    if (!videoReceiver.start(static_cast<quint16>(inputPort), decoderPort, videoBind, videoSource)) {
        qWarning("RM2026 HEVC input bind failed; no other process was closed.");
        return 2;
    }
    qInfo().noquote() << "RM2026_HEVC_INPUT bind=" << videoBind.toString()
        << "port=" << inputPort << "source=" << videoSource.toString()
        << "fragmentBase=" << parser.value("fragment-base") << "control=disabled";
    player->play();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &videoReceiver, [&] {
        videoReceiver.stop();
        player->stop();
    });
#endif
#ifdef RM27_RM2026_LOCAL_HUD
    if (syntheticHevc) {
        QUdpSocket reservation;
        if (!reservation.bind(QHostAddress::LocalHost, 0)) return 2;
        const quint16 decoderPort = reservation.localPort();
        if (decoderPort == hevcPort) return 2;
        reservation.close();
        syntheticVideo = std::make_unique<rm27::video::HistoricalVideoReceiver>(parser.value("fragment-base").toUInt());
        auto* root = engine.rootObjects().first();
        auto* player = root->findChild<QMediaPlayer*>(QStringLiteral("localPlayer"));
        if (!player) return 2;
        terminal.connection()->setSimulationVideoPort(decoderPort);
        terminal.enterSimulationVideo();
        terminal.configureSimulationVideoPlayer(player);
        root->setProperty("simulationVideoSelected", true);
        const QUrl url(QStringLiteral("udp://127.0.0.1:%1?localaddr=127.0.0.1&fifo_size=1024&overrun_nonfatal=1").arg(decoderPort));
        player->setSource(url);
        QObject::connect(player, &QMediaPlayer::sourceChanged, syntheticVideo.get(), [&, url](const QUrl& source) {
            if (source != url) syntheticVideo->stop();
        });
        QObject::connect(syntheticVideo.get(), &rm27::video::HistoricalVideoReceiver::statsChanged,
            &app, [&](const QVariantMap& stats) { syntheticVideoStats = stats; });
        QObject::connect(syntheticVideo.get(), &rm27::video::HistoricalVideoReceiver::errorOccurred,
            &app, [](const QString& error) { qWarning().noquote() << error; });
        if (!syntheticVideo->start(static_cast<quint16>(hevcPort), decoderPort)) return 2;
        player->play();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, syntheticVideo.get(), [&, player] {
            syntheticVideo->stop();
            player->stop();
        });
    }
    QTimer poll;
    QByteArray previous;
    const auto refresh = [&] {
        auto report = observer->status();
        terminal.applyHistoricalSnapshot(observer->snapshot(), report.value("link") == "subscribed");
        report.insert("hud", QJsonObject{{"data", QJsonObject::fromVariantMap(terminal.hudSnapshot())},
            {"hp", terminal.hpLabel()}, {"fresh", terminal.refereeFresh()},
            {"armed", terminal.controlsArmed()}, {"map", QJsonObject::fromVariantMap(terminal.mapSnapshot())},
            {"messages", QJsonArray::fromVariantList(terminal.eventMessages())},
            {"source", terminal.linkBannerText()}, {"alert", terminal.alertText()}});
        if (simulationUdpVideo || syntheticHevc) {
            auto video = QJsonObject::fromVariantMap(terminal.videoDiagnostics());
            video.insert("fresh", terminal.videoConnected());
            video.insert("stale", terminal.videoStale());
            video.insert("label", terminal.videoStatusLabel());
            if (syntheticHevc) {
                video.insert("synthetic", true);
                video.insert("transport", QJsonObject::fromVariantMap(syntheticVideoStats));
            }
            report.insert("video", video);
        }
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
        if (bytes != previous) { std::cout << bytes.constData() << std::endl; previous = bytes; }
    };
    refresh();
    QObject::connect(&poll, &QTimer::timeout, &app, refresh);
    poll.start(50);
    observer->start();
#endif
    return app.exec();
}
