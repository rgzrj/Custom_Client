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
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QCommandLineParser>
#include <iostream>
#ifdef RM27_RM2026_LOCAL_HUD
#include "protocol/rm2026/mqtt_observer.hpp"
#include <QJsonArray>
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

#if !defined(RM27_RM2026_LOCAL_HUD)
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

#ifdef RM27_RM2026_LOCAL_HUD
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
    TerminalController terminal(false, nullptr, 0, false, 0, true);
    if (simulationUdpVideo) terminal.connection()->setSimulationVideoPort(static_cast<int>(videoPort));
    const QUrl initialVideo;
#else
    QCommandLineParser parser;
    parser.addHelpOption();
    addPairingOptions(parser);
    parser.addOption({"demo", "Local demonstration."});
    parser.addOption({"sim-mqtt", "Local simulation MQTT."});
    parser.addOption({"sim-udp-video", "Local simulation video."});
    parser.addOption({"video", "Local replay file.", "path"});
    parser.addOption({"protocol", "Explicit protocol version.", "version"});
    parser.addOption({"mqtt-host", "Explicit broker IPv4 address.", "address"});
    parser.addOption({"mqtt-port", "Explicit broker port.", "port"});
    parser.addOption({"robot-id", "Decimal robot ID; red infantry 3 is 3.", "id"});
    parser.addOption({"protocol-test", "Local test source; only 127.0.0.1."});
    parser.addOption({"hevc-udp", "Receive RM2026 HEVC directly into the existing VideoOutput."});
    parser.addOption({"video-bind", "Explicit local IPv4 listen address.", "address"});
    parser.addOption({"video-source", "Explicit permitted sender IPv4 address.", "address"});
    parser.addOption({"video-port", "Explicit UDP port.", "port"});
    parser.addOption({"fragment-base", "Explicit fragment numbering: 0 or 1.", "base"});
    parser.addOption({"allow-network-video", "Enable the explicitly configured non-loopback listener."});
    parser.addOption({"diagnostics-json", "Print local test diagnostics to stdout."});
    parser.addOption({"duration-ms", "Optional bounded run, 1..60000 ms.", "ms"});
    if (!parser.parse(app.arguments())) { std::cerr << parser.errorText().toStdString() << '\n'; return 2; }
    if (parser.isSet("help")) parser.showHelp();
    const bool demoMode = parser.isSet("demo");
    const bool simulationUdpVideo = parser.isSet("sim-udp-video");
    const bool simulationMqtt = parser.isSet("sim-mqtt");
    const QUrl initialVideo = parser.isSet("video")
        ? QUrl::fromLocalFile(QFileInfo(parser.value("video")).absoluteFilePath()) : QUrl{};
    const bool upstreamMode = parser.isSet("protocol");
    const bool nativeVideo = parser.isSet("hevc-udp");
    const bool nativeData = parser.isSet("mqtt-host") || parser.isSet("mqtt-port") || parser.isSet("robot-id");
    QHostAddress nativeBind, nativeSource;
    bool videoPortValid = false;
    const unsigned nativePort = parser.value("video-port").toUInt(&videoPortValid);
    if (nativeVideo) {
        if (!upstreamMode || !videoPortValid || nativePort == 0 || nativePort > 65535
            || (parser.value("fragment-base") != "0" && parser.value("fragment-base") != "1")
            || !nativeBind.setAddress(parser.value("video-bind"))
            || !nativeSource.setAddress(parser.value("video-source"))
            || nativeBind.toString() != parser.value("video-bind")
            || nativeSource.toString() != parser.value("video-source")
            || nativeBind.protocol() != QAbstractSocket::IPv4Protocol
            || nativeSource.protocol() != QAbstractSocket::IPv4Protocol
            || ((!nativeBind.isLoopback() || !nativeSource.isLoopback())
                && !parser.isSet("allow-network-video"))) return 2;
    } else if (parser.isSet("video-bind") || parser.isSet("video-source") || parser.isSet("video-port")
        || parser.isSet("fragment-base") || parser.isSet("allow-network-video")) return 2;
    bool portValid = false, idValid = false;
    const unsigned mqttPort = parser.value("mqtt-port").toUInt(&portValid);
    const unsigned robotId = parser.value("robot-id").toUInt(&idValid);
    if (parser.isSet("pairing-robot-id")) {
        bool valid = false;
        const auto rawId = parser.value("pairing-robot-id");
        const unsigned id = rawId.toUInt(&valid);
        const unsigned role = id >= 101 && id <= 107 ? id - 100 : id;
        if (!valid || QString::number(id) != rawId || !nativeVideo || parser.isSet("protocol-test")
            || (parser.value("pairing-region") != "global" && parser.value("pairing-region") != "japan")
            || (role != 1 && role != 2 && role != 3 && role != 4 && role != 6 && role != 7)
            || (nativeData && (!idValid || id != robotId))) return 2;
    } else if (parser.isSet("pairing-region") || parser.isSet("pairing-confirmed")) return 2;
    if (upstreamMode) {
#ifndef RM27_UPSTREAM_BACKEND
        std::cerr << "This build has no upstream backend.\n"; return 2;
#endif
        if (parser.value("protocol") != "rm2026-v2.0.0"
            || ((!nativeVideo || nativeData) && (!parser.isSet("mqtt-host")
                || !portValid || mqttPort == 0 || mqttPort > 65535 || !idValid
                || QString::number(robotId) != parser.value("robot-id")))
            || demoMode || simulationMqtt || simulationUdpVideo || !initialVideo.isEmpty()
            || (parser.isSet("diagnostics-json") && !parser.isSet("protocol-test"))) return 2;
    } else if (parser.isSet("mqtt-host") || parser.isSet("mqtt-port") || parser.isSet("robot-id")
        || parser.isSet("protocol-test") || parser.isSet("diagnostics-json")) return 2;
    if (parser.isSet("duration-ms")) {
        bool valid = false;
        const unsigned duration = parser.value("duration-ms").toUInt(&valid);
        if (!valid || duration == 0 || duration > 60000) return 2;
        QTimer::singleShot(int(duration), &app, &QCoreApplication::quit);
    }
    bool useUpstream = upstreamMode;
#ifdef RM27_UPSTREAM_BACKEND
    useUpstream = !demoMode && !simulationMqtt && !simulationUdpVideo && initialVideo.isEmpty();
#endif
    TerminalController terminal(demoMode, nullptr, 0, simulationMqtt, 0, false, useUpstream);
    if (upstreamMode && nativeData && !terminal.configureRm2026(parser.value("mqtt-host"), quint16(mqttPort),
        robotId, parser.isSet("protocol-test"))) return 2;
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
#if defined(RM27_UPSTREAM_VIDEO) && !defined(RM27_RM2026_LOCAL_HUD)
    if (nativeVideo && !terminal.configureRm2026Video(nativeBind, quint16(nativePort), nativeSource,
        parser.value("fragment-base").toUInt(), parser.isSet("protocol-test"))) return 2;
#endif
#ifndef RM27_RM2026_LOCAL_HUD
    if (!startPairing(parser, terminal)) return 2;
#endif
#ifdef RM27_RM2026_LOCAL_HUD
    if (syntheticHevc && !terminal.configureRm2026Video(QHostAddress::LocalHost,
        quint16(hevcPort), QHostAddress::LocalHost, parser.value("fragment-base").toUInt(), true)) return 2;
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
                video.insert("transport", QJsonObject::fromVariantMap(terminal.refereeDiagnostics().value("video").toMap().value("transport").toMap()));
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
#if defined(RM27_UPSTREAM_BACKEND) && !defined(RM27_RM2026_LOCAL_HUD)
    QTimer diagnostics;
    if (parser.isSet("diagnostics-json")) {
        QObject::connect(&diagnostics, &QTimer::timeout, &app, [&] {
            const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(terminal.refereeDiagnostics()))
                .toJson(QJsonDocument::Compact);
            std::cout << bytes.constData() << std::endl;
        });
        diagnostics.start(50);
    }
#endif
    return app.exec();
}
