#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QUrl>
#include <QQuickStyle>

#include "app/terminal_controller.hpp"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication::setOrganizationName(QStringLiteral("RM27"));
    QGuiApplication::setApplicationName(QStringLiteral("RM2027 自定义终端"));

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

    // 端口传 0：使用启动面板保存的端点（首次运行为本机模拟环回 39001/39002）。
    TerminalController terminal(demoMode, nullptr, 0, simulationMqtt);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("terminal"), &terminal);
    engine.rootContext()->setContextProperty(QStringLiteral("initialVideo"), initialVideo);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSimUdpVideo"), simulationUdpVideo);
    engine.loadFromModule(QStringLiteral("RM27.Terminal"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    return app.exec();
}
