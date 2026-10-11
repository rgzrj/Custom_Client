#pragma once

#include <QObject>
#include <QHostAddress>
#include <QThread>
#include <QVariantMap>

namespace rm27::video {

// Explicit RM2026 network video experiment.
// Reuses Qt Multimedia's HEVC decoder via a private loopback byte-stream socket.
class HistoricalVideoReceiver final : public QObject {
    Q_OBJECT
public:
    explicit HistoricalVideoReceiver(unsigned firstFragmentIndex, QObject* parent = nullptr);
    ~HistoricalVideoReceiver() override;
    bool start(quint16 inputPort, quint16 decoderPort);
    bool start(quint16 inputPort, quint16 decoderPort,
               const QHostAddress& bindAddress, const QHostAddress& sourceAddress);
    void stop();

signals:
    void statsChanged(const QVariantMap& stats);
    void errorOccurred(const QString& message);

private:
    QThread thread_;
    QObject* worker_;
};

} // namespace rm27::video
