#include "video/upstream_video.hpp"
#include "video/hevc_sync_filter.hpp"
#include "protocol/rm2026/video_reassembly.hpp"
#include "network/HevcDecoder.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QNetworkDatagram>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>
#include <chrono>

namespace rm27::video {
namespace {
qint64 monoMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool unicast(const QHostAddress& address)
{
    return address.protocol() == QAbstractSocket::IPv4Protocol
        && address.toIPv4Address() && address != QHostAddress::Broadcast && !address.isMulticast();
}
}

struct UpstreamVideo::Impl {
    QThread thread;
    QObject* worker{new QObject};
    mutable QMutex mutex;
    UpstreamVideoView latest;
    std::unique_ptr<QUdpSocket> socket;
    std::unique_ptr<QTimer> timer;
    std::unique_ptr<RM::HevcDecoder> decoder;
    std::unique_ptr<protocol::rm2026::VideoReassembly> assembly;
    HevcSyncFilter sync;
    QHostAddress source;
    quint16 sourcePort{0}, lastSequence{0};
    bool haveSequence{false}, quiet{true}, pumpQueued{false};
    qint64 lastPacket{-1}, budgetStart{0};
    qint64 firstPacket{0}, receiveToAssembleMs{0};
    quint16 receivingSequence{0};
    unsigned budgetPackets{0};
    quint64 packets{0}, rejected{0}, assembled{0}, decoded{0}, resyncs{0}, overwritten{0}, decodeErrors{0};
    QString error;

    Impl()
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    ~Impl()
    {
        QMetaObject::invokeMethod(worker, [this] { stopWorker(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    void stopWorker()
    {
        timer.reset(); socket.reset(); decoder.reset(); assembly.reset();
        sync = {}; sourcePort = 0; haveSequence = false; quiet = true; lastPacket = -1;
        packets = rejected = assembled = decoded = resyncs = overwritten = 0;
        decodeErrors = 0; firstPacket = 0; receiveToAssembleMs = 0;
        budgetStart = monoMs(); budgetPackets = 0; error.clear();
        QMutexLocker lock(&mutex);
        latest = {};
    }
    void recover(bool clear)
    {
        sync.reset(clear);
        decoder->resetStreamState();
        ++resyncs;
    }
    void publishStats()
    {
        QMutexLocker lock(&mutex);
        latest.stats = {{"listening", socket && socket->state() == QAbstractSocket::BoundState},
            {"packets", packets}, {"rejectedPackets", rejected + (assembly ? assembly->rejectedPackets() : 0)},
            {"assembledFrames", assembled}, {"decodedFrames", decoded}, {"resyncs", resyncs},
            {"duplicates", assembly ? assembly->duplicatePackets() : 0},
            {"droppedFrames", assembly ? assembly->droppedFrames() : 0},
            {"bufferedBytes", quint64(assembly ? assembly->bufferedBytes() : 0)},
            {"waitingForKeyframe", sync.waitingForKeyframe()}, {"error", error},
            {"invalidHevcUnits", sync.invalidUnits()}, {"droppedHevcUnits", sync.droppedUnits()},
            {"imageQueueDepth", latest.image.isNull() ? 0 : 1}, {"overwrittenImages", overwritten},
            {"receiveToAssembleMs", receiveToAssembleMs},
            {"decodeErrors", decodeErrors},
            {"decodeMs", decoder ? decoder->getLastDecodeTimeMs() : 0.0}};
    }
    bool startWorker(const QHostAddress& bind, quint16 port, const QHostAddress& sender, unsigned base)
    {
        stopWorker();
        if (!port || base > 1 || !unicast(bind) || !unicast(sender)
            || bind.isLoopback() != sender.isLoopback()) return false;
        decoder = std::make_unique<RM::HevcDecoder>();
        QObject::connect(decoder.get(), &RM::HevcDecoder::decodingError, worker,
            [this](const QString& message) { error = message; ++decodeErrors; });
        if (!decoder->initialize()) { publishStats(); return false; }
        assembly = std::make_unique<protocol::rm2026::VideoReassembly>(quint16(base));
        socket = std::make_unique<QUdpSocket>();
        if (!socket->bind(bind, port, QUdpSocket::DontShareAddress)) {
            error = socket->errorString(); publishStats(); return false;
        }
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);
        source = sender;
        QObject::connect(socket.get(), &QUdpSocket::readyRead, worker, [this] { receive(); });
        timer = std::make_unique<QTimer>();
        QObject::connect(timer.get(), &QTimer::timeout, worker, [this] {
            const auto drops = assembly->droppedFrames();
            assembly->expire(monoMs());
            if (assembly->droppedFrames() != drops) recover(false);
            if (!quiet && lastPacket >= 0 && monoMs() - lastPacket >= 700) {
                quiet = true; assembly->reset(); recover(true);
                sourcePort = 0; haveSequence = false;
                QMutexLocker lock(&mutex);
                latest.image = {}; latest.decodedAtMs = -1;
            }
            publishStats();
        });
        timer->start(50); publishStats(); return true;
    }
    void receive()
    {
        if (!socket || !assembly) return;
        for (int batch = 0; batch < 64 && socket->hasPendingDatagrams(); ++batch) {
            const auto packet = socket->receiveDatagram(1401);
            ++packets;
            const auto now = monoMs();
            if (now - budgetStart >= 1000) { budgetStart = now; budgetPackets = 0; }
            if (++budgetPackets > 8192 || packet.senderAddress() != source
                || packet.data().size() <= 8 || packet.data().size() > 1400
                || (sourcePort && packet.senderPort() != sourcePort)) { ++rejected; continue; }
            const auto& bytes = packet.data();
            const auto rejects = assembly->rejectedPackets(), drops = assembly->droppedFrames();
            auto frame = assembly->accept(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), now);
            if (assembly->droppedFrames() != drops) recover(false);
            if (assembly->rejectedPackets() != rejects) continue;
            sourcePort = packet.senderPort(); lastPacket = now; quiet = false;
            const auto sequence = qFromBigEndian<quint16>(bytes.constData());
            if (!firstPacket || receivingSequence != sequence) { firstPacket = now; receivingSequence = sequence; }
            if (!frame) continue;
            receiveToAssembleMs = now - firstPacket;
            ++assembled;
            if (haveSequence && quint16(frame->sequence - lastSequence) != 1) recover(false);
            lastSequence = frame->sequence; haveSequence = true;
            auto unit = sync.accept(frame->hevc);
            if (!unit) continue;
            const auto image = decoder->decode(QByteArray(
                reinterpret_cast<const char*>(unit->data()), qsizetype(unit->size())));
            if (!error.isEmpty()) { recover(false); error.clear(); continue; }
            if (image.isNull()) continue;
            ++decoded;
            QMutexLocker lock(&mutex);
            if (!latest.image.isNull()) ++overwritten;
            latest.image = image; latest.serial = decoded; latest.decodedAtMs = monoMs();
        }
        publishStats();
        if (socket->hasPendingDatagrams() && !pumpQueued) {
            pumpQueued = true;
            QTimer::singleShot(0, worker, [this] { pumpQueued = false; receive(); });
        }
    }
};

UpstreamVideo::UpstreamVideo() : impl_(std::make_unique<Impl>()) {}
UpstreamVideo::~UpstreamVideo() = default;
bool UpstreamVideo::start(const QHostAddress& bind, quint16 port, const QHostAddress& source, unsigned base)
{
    bool result = false;
    QMetaObject::invokeMethod(impl_->worker, [&] {
        result = impl_->startWorker(bind, port, source, base);
    }, Qt::BlockingQueuedConnection);
    return result;
}
void UpstreamVideo::stop()
{
    QMetaObject::invokeMethod(impl_->worker, [this] { impl_->stopWorker(); }, Qt::BlockingQueuedConnection);
}
UpstreamVideoView UpstreamVideo::view() const
{
    QMutexLocker lock(&impl_->mutex);
    auto result = impl_->latest;
    impl_->latest.image = {}; // Consume once; never accumulate queued image signals.
    return result;
}
} // namespace rm27::video
