#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace rm27::sim {

// Loopback-only, read-only MQTT 3.1.1 test subscriber. Not an RM2027 adapter.
class MqttReceiver final : public QObject {
    Q_OBJECT
public:
    explicit MqttReceiver(QObject* parent = nullptr, quint16 port = 39004);
    void start();

signals:
    void payloadReceived(const QByteArray& payload);
    void linkChanged(bool connected);

private:
    void readPackets();
    void reconnect();
    void handlePacket(quint8 header, const QByteArray& body);

    QTimer retry_;
    QByteArray pending_;
    quint16 port_;
    bool subscribed_{false};
    // Destroy the socket first: disconnected may still clear pending_ and start retry_.
    QTcpSocket socket_;
};

}  // namespace rm27::sim
