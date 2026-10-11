#include "sim/sim_mqtt_receiver.hpp"

#include <QHostAddress>

namespace rm27::sim {
namespace {

constexpr char kTopic[] = "rm27/sim/v1/referee";
constexpr int kMaximumPacket = 4096 + 64;

void appendString(QByteArray& body, const QByteArray& value)
{
    body.append(char(value.size() >> 8));
    body.append(char(value.size()));
    body.append(value);
}

QByteArray packet(const quint8 header, const QByteArray& body)
{
    QByteArray result(1, char(header));
    int remaining = body.size();
    do {
        quint8 digit = remaining % 128;
        remaining /= 128;
        if (remaining) digit |= 0x80;
        result.append(char(digit));
    } while (remaining);
    result.append(body);
    return result;
}

}  // namespace

MqttReceiver::MqttReceiver(QObject* parent, const quint16 port)
    : QObject(parent), port_(port)
{
    retry_.setSingleShot(true);
    retry_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, &MqttReceiver::reconnect);
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        QByteArray body("\0\4MQTT\4\2\0\0", 10); // MQTT 3.1.1, clean session, no keepalive.
        appendString(body, QByteArrayLiteral("rm27-sim-observer"));
        socket_.write(packet(0x10, body));
    });
    connect(&socket_, &QTcpSocket::readyRead, this, &MqttReceiver::readPackets);
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        pending_.clear();
        subscribed_ = false;
        emit linkChanged(false);
        retry_.start();
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (socket_.state() == QAbstractSocket::UnconnectedState) retry_.start();
    });
}

void MqttReceiver::start()
{
    reconnect();
}

void MqttReceiver::reconnect()
{
    if (socket_.state() == QAbstractSocket::UnconnectedState)
        socket_.connectToHost(QHostAddress::LocalHost, port_);
}

void MqttReceiver::readPackets()
{
    pending_.append(socket_.readAll());
    if (pending_.size() > kMaximumPacket * 2) {
        socket_.abort();
        return;
    }
    while (pending_.size() >= 2) {
        int remaining = 0;
        int multiplier = 1;
        int index = 1;
        quint8 digit = 0;
        do {
            if (index >= pending_.size()) return;
            digit = quint8(pending_.at(index++));
            remaining += (digit & 0x7f) * multiplier;
            multiplier *= 128;
            if (index > 5 || remaining > kMaximumPacket) {
                socket_.abort();
                return;
            }
        } while (digit & 0x80);
        if (pending_.size() < index + remaining) return;
        const quint8 header = quint8(pending_.at(0));
        const QByteArray body = pending_.mid(index, remaining);
        pending_.remove(0, index + remaining);
        handlePacket(header, body);
    }
}

void MqttReceiver::handlePacket(const quint8 header, const QByteArray& body)
{
    if (header == 0x20) {
        if (body.size() != 2 || body.at(1) != 0) {
            socket_.abort();
            return;
        }
        QByteArray subscribe("\0\1", 2); // Packet identifier 1, QoS 0.
        appendString(subscribe, QByteArray(kTopic));
        subscribe.append(char(0));
        socket_.write(packet(0x82, subscribe));
    } else if (header == 0x90) {
        if (body != QByteArray("\0\1\0", 3)) {
            socket_.abort();
            return;
        }
        subscribed_ = true;
        emit linkChanged(true);
    } else if ((header & 0xf0) == 0x30 && subscribed_) {
        if ((header & 0x06) || body.size() < 2) return; // Only QoS 0 test messages.
        const int topicSize = (quint8(body.at(0)) << 8) | quint8(body.at(1));
        if (topicSize != int(sizeof(kTopic) - 1) || body.size() < 2 + topicSize) return;
        if (body.mid(2, topicSize) != QByteArray(kTopic)) return;
        const QByteArray payload = body.mid(2 + topicSize);
        if (!payload.isEmpty() && payload.size() <= 4096)
            emit payloadReceived(payload);
    }
}

}  // namespace rm27::sim
