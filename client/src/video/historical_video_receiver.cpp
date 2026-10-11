#include "video/historical_video_receiver.hpp"
#include "protocol/rm2026/video_reassembly.hpp"
#include "video/hevc_sync_filter.hpp"

#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <algorithm>
#include <stdexcept>

namespace rm27::video {

class HistoricalVideoWorker final : public QObject {
    Q_OBJECT
public:
    explicit HistoricalVideoWorker(unsigned firstIndex)
        : assembly_(firstIndex <= 1 ? static_cast<std::uint16_t>(firstIndex)
                                  : throw std::invalid_argument("fragment base must be 0 or 1"))
    {
        input_.setParent(this);
        output_.setParent(this);
        timer_.setParent(this);
        clock_.start();
        timer_.setInterval(250);
        connect(&input_, &QUdpSocket::readyRead, this, &HistoricalVideoWorker::receive);
        connect(&timer_, &QTimer::timeout, this, [this] {
            const auto dropped = assembly_.droppedFrames();
            assembly_.expire(clock_.elapsed());
            if (assembly_.droppedFrames() != dropped) sync_.reset();
            emit statsChanged(QVariantMap{{"packets", QVariant::fromValue(packets_)},
                {"assembledFrames", QVariant::fromValue(frames_)},
                {"forwardedFrames", QVariant::fromValue(forwarded_)},
                {"waitingForKeyframe", sync_.waitingForKeyframe()},
                {"droppedDecodeUnits", QVariant::fromValue(quint64(sync_.droppedUnits()))},
                {"invalidHevcUnits", QVariant::fromValue(quint64(sync_.invalidUnits()))},
                {"rejectedPackets", QVariant::fromValue(rejected_ + assembly_.rejectedPackets())},
                {"duplicates", QVariant::fromValue(assembly_.duplicatePackets())},
                {"droppedFrames", QVariant::fromValue(assembly_.droppedFrames())},
                {"bufferedBytes", QVariant::fromValue(quint64(assembly_.bufferedBytes()))}});
        });
    }

    bool start(quint16 inputPort, quint16 decoderPort,
               const QHostAddress& bindAddress, const QHostAddress& sourceAddress)
    {
        stop();
        const auto unicast = [](const QHostAddress& address) {
            return address.protocol() == QAbstractSocket::IPv4Protocol
                && address.toIPv4Address() != 0 && address != QHostAddress::Broadcast
                && !address.isMulticast();
        };
        if (!inputPort || !decoderPort || inputPort == decoderPort
            || !unicast(bindAddress) || !unicast(sourceAddress)
            || bindAddress.isLoopback() != sourceAddress.isLoopback()
            || !input_.bind(bindAddress, inputPort)) return false;
        sourceAddress_ = sourceAddress;
        decoderPort_ = decoderPort;
        timer_.start();
        return true;
    }

    void stop()
    {
        timer_.stop();
        input_.close();
        output_.close();
        assembly_.reset();
        sync_.reset(true);
        haveSequence_ = false;
        sourcePort_ = 0;
        lastAccepted_ = -1;
        budgetPackets_ = 0;
        budgetStart_ = clock_.elapsed();
    }

signals:
    void statsChanged(const QVariantMap& stats);
    void errorOccurred(const QString& message);

private:
    void receive()
    {
        // Project budgets: bounded allocation, work per event and input rate.
        for (int batch = 0; batch < 256 && input_.hasPendingDatagrams(); ++batch) {
            const auto packet = input_.receiveDatagram(1401);
            ++packets_;
            const qint64 now = clock_.elapsed();
            if (now - budgetStart_ >= 1000) {
                budgetStart_ = now;
                budgetPackets_ = 0;
            }
            if (++budgetPackets_ > 8192 || packet.senderAddress() != sourceAddress_
                || packet.data().size() <= 8 || packet.data().size() > 1400) {
                ++rejected_;
                continue;
            }
            // After a quiet session, allow a restarted sender/sequence; never revive
            // an old frame while the current session is still receiving packets.
            if (lastAccepted_ >= 0 && now - lastAccepted_ >= 700) {
                assembly_.reset();
                sync_.reset(true);
                haveSequence_ = false;
                sourcePort_ = 0;
            }
            if (sourcePort_ && packet.senderPort() != sourcePort_) {
                ++rejected_;
                continue;
            }
            const auto& bytes = packet.data();
            const std::vector<std::uint8_t> datagram(bytes.begin(), bytes.end());
            const auto rejectedBefore = assembly_.rejectedPackets();
            const auto droppedBefore = assembly_.droppedFrames();
            auto frame = assembly_.accept(datagram, now);
            if (assembly_.droppedFrames() != droppedBefore) sync_.reset();
            if (assembly_.rejectedPackets() != rejectedBefore) continue;
            sourcePort_ = packet.senderPort();
            lastAccepted_ = now;
            if (!frame) continue;
            ++frames_;
            if (haveSequence_ && quint16(frame->sequence - lastSequence_) != 1) sync_.reset();
            lastSequence_ = frame->sequence;
            haveSequence_ = true;
            auto hevc = sync_.accept(frame->hevc);
            if (!hevc) continue;
            // Internal raw Annex-B HEVC stream, not another RM2026 framed stream.
            // No TS muxer or second decoder dependency is needed.
            bool written = true;
            for (std::size_t offset = 0; offset < hevc->size(); offset += 1392) {
                const auto count = std::min<std::size_t>(1392, hevc->size() - offset);
                if (output_.writeDatagram(reinterpret_cast<const char*>(hevc->data() + offset),
                        qint64(count), QHostAddress::LocalHost, decoderPort_) != qint64(count)) {
                    emit errorOccurred(QStringLiteral("HEVC 本机解码入口写入失败"));
                    sync_.reset();
                    written = false;
                    break;
                }
            }
            if (written) ++forwarded_;
        }
        if (input_.hasPendingDatagrams()) QTimer::singleShot(0, this, &HistoricalVideoWorker::receive);
    }

    protocol::rm2026::VideoReassembly assembly_;
    HevcSyncFilter sync_;
    QHostAddress sourceAddress_;
    QUdpSocket input_, output_;
    QTimer timer_;
    QElapsedTimer clock_;
    quint16 decoderPort_{0}, sourcePort_{0};
    quint16 lastSequence_{0};
    bool haveSequence_{false};
    qint64 lastAccepted_{-1}, budgetStart_{0};
    int budgetPackets_{0};
    quint64 packets_{0}, frames_{0}, forwarded_{0}, rejected_{0};
};

HistoricalVideoReceiver::HistoricalVideoReceiver(unsigned firstIndex, QObject* parent)
    : QObject(parent), worker_(new HistoricalVideoWorker(firstIndex))
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    auto* worker = static_cast<HistoricalVideoWorker*>(worker_);
    connect(worker, &HistoricalVideoWorker::statsChanged, this, &HistoricalVideoReceiver::statsChanged);
    connect(worker, &HistoricalVideoWorker::errorOccurred, this, &HistoricalVideoReceiver::errorOccurred);
    thread_.start();
}

HistoricalVideoReceiver::~HistoricalVideoReceiver()
{
    stop();
    thread_.quit();
    thread_.wait();
}

bool HistoricalVideoReceiver::start(quint16 inputPort, quint16 decoderPort)
{
    return start(inputPort, decoderPort, QHostAddress::LocalHost, QHostAddress::LocalHost);
}

bool HistoricalVideoReceiver::start(quint16 inputPort, quint16 decoderPort,
                                    const QHostAddress& bindAddress, const QHostAddress& sourceAddress)
{
    bool started = false;
    QMetaObject::invokeMethod(worker_, [&] {
        started = static_cast<HistoricalVideoWorker*>(worker_)->start(
            inputPort, decoderPort, bindAddress, sourceAddress);
    }, Qt::BlockingQueuedConnection);
    return started;
}

void HistoricalVideoReceiver::stop()
{
    QMetaObject::invokeMethod(worker_, [this] {
        static_cast<HistoricalVideoWorker*>(worker_)->stop();
    }, Qt::BlockingQueuedConnection);
}

} // namespace rm27::video

#include "historical_video_receiver.moc"
