#include "protocol/rm2026/mqtt_observer.hpp"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>
#ifdef RM2026_OBSERVER_VIEW
#include <QApplication>
#include "rm2026_observer_view.hpp"
#endif

int main(int argc, char** argv)
{
#ifdef RM2026_OBSERVER_VIEW
    QApplication app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription("Loopback-only historical RM2026 C++ read-only developer probe; no HUD/control.");
    parser.addHelpOption();
    parser.addOption({"protocol", "Explicit historical version.", "version"});
    parser.addOption({"host", "Only literal 127.0.0.1 is accepted.", "host", "127.0.0.1"});
    parser.addOption({"port", "Local test broker port.", "port", "39005"});
    parser.addOption({"robot-id", "Optional numeric MQTT client ID for local identity tests; not hardware pairing.", "id"});
    parser.addOption({"duration-ms", "Optional bounded developer run, 1..60000 ms.", "milliseconds"});
#ifdef RM2026_OBSERVER_VIEW
    parser.addOption({"synthetic-demo", "Permanently label this window as synthetic local test data."});
#endif
    parser.process(app);
    bool valid = false;
    const auto port = parser.value("port").toUInt(&valid);
    if (!valid || port == 0 || port > 65535 || parser.value("host") != "127.0.0.1" ||
        parser.value("protocol") != rm27::protocol::rm2026::MqttObserver::version) {
        std::cerr << "Explicit --protocol rm2026-v2.0.0, literal 127.0.0.1 and port 1..65535 required.\n";
        return 2;
    }
    std::optional<unsigned> robotId;
    if (parser.isSet("robot-id")) {
        const auto value = parser.value("robot-id");
        const auto id = value.toUInt(&valid);
        if (!valid || value != QString::number(id)) {
            std::cerr << "Robot ID must be a canonical unsigned decimal number.\n";
            return 2;
        }
        robotId = id;
    }
    if (parser.isSet("duration-ms")) {
        const int duration = parser.value("duration-ms").toInt(&valid);
        if (!valid || duration < 1 || duration > 60000) return 2;
        QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    }
    try {
        rm27::protocol::rm2026::MqttObserver observer(parser.value("protocol").toStdString(), port, robotId);
        observer.start();
#ifdef RM2026_OBSERVER_VIEW
        Rm2026ObserverView view(port, parser.isSet("synthetic-demo"));
        view.show();
#endif
        QTimer poll;
        QByteArray previous;
        const auto refresh = [&] {
            const auto status = observer.status();
            const auto report = QJsonDocument(status).toJson(QJsonDocument::Compact);
#ifdef RM2026_OBSERVER_VIEW
            if (report != previous) view.updateReport(status);
#endif
            if (report != previous) { std::cout << report.constData() << std::endl; previous = report; }
        };
        refresh(); // Even a short bounded run reports its initial state.
        QObject::connect(&poll, &QTimer::timeout, &app, refresh);
        poll.start(50);
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
