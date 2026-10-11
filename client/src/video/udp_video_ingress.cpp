#include "video/udp_video_ingress.hpp"

#include <QDateTime>
#include <QHostAddress>
#include <QNetworkDatagram>

namespace rm27::video {
namespace {

constexpr qint64 kMaximumUdpPayload = 65507;
constexpr int kStaleAfterMs = 700;
constexpr int kStatsIntervalMs = 250;
// 相邻包静默超过该值即计一次到达缺口：它只说明「这一路暂时没数据」，
// 不代表官方分片序号缺口——官方序号语义未到位前不做这种断言。
constexpr int kArrivalGapMs = 250;

}  // namespace

UdpVideoIngress::UdpVideoIngress(QObject* parent)
    : QObject(parent)
{
    staleTimer_.setSingleShot(true);
    staleTimer_.setInterval(kStaleAfterMs);
    statsTimer_.setInterval(kStatsIntervalMs);

    connect(&socket_, &QUdpSocket::readyRead, this, &UdpVideoIngress::readPendingDatagrams);
    connect(&staleTimer_, &QTimer::timeout, this, [this] {
        fresh_ = false;
        stallBeganAtMs_ = QDateTime::currentMSecsSinceEpoch();
        ++stallCount_;
        emit stallStarted(kStaleAfterMs);
        emit linkChanged(false);
    });
    connect(&statsTimer_, &QTimer::timeout, this, &UdpVideoIngress::reportStats);
}

bool UdpVideoIngress::start(const quint16 port)
{
    // 只监听环回：正式 UDP 分包格式未到位前，本入口只做「收到多少包/字节」的度量，
    // 不在真实网卡上开启未知协议的接收口。
    socket_.close();
    if (!socket_.bind(QHostAddress::LocalHost, port)) {
        return false;
    }
    statsTimer_.start();
    return true;
}

qint64 UdpVideoIngress::silentMs() const
{
    if (!receivedAny_) {
        return -1;
    }
    return QDateTime::currentMSecsSinceEpoch() - lastDatagramAtMs_;
}

void UdpVideoIngress::noteArrival()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (receivedAny_ && now - lastDatagramAtMs_ >= kArrivalGapMs) {
        ++arrivalGapCount_;
    }
    lastDatagramAtMs_ = now;
    receivedAny_ = true;

    staleTimer_.start();
    if (!fresh_) {
        fresh_ = true;
        if (stallBeganAtMs_ > 0) {
            emit resynced(static_cast<int>(now - stallBeganAtMs_));
            ++resyncCount_;
            stallBeganAtMs_ = 0;
        }
        emit linkChanged(true);
    }
}

void UdpVideoIngress::readPendingDatagrams()
{
    while (socket_.hasPendingDatagrams()) {
        const qint64 pendingSize = socket_.pendingDatagramSize();
        const auto datagram = socket_.receiveDatagram(kMaximumUdpPayload);
        if (pendingSize <= 0 || pendingSize > kMaximumUdpPayload) {
            continue;
        }
        // 探针只服务本机联调：非环回来源一律丢弃并计数，便于发现误接或探测。
        if (!datagram.senderAddress().isLoopback()) {
            ++rejectedSourceCount_;
            continue;
        }

        ++packetCount_;
        byteCount_ += static_cast<quint64>(datagram.data().size());
        noteArrival();
    }
}

void UdpVideoIngress::reportStats()
{
    if (packetCount_ == reportedPacketCount_) {
        return;
    }
    reportedPacketCount_ = packetCount_;
    emit statsChanged(packetCount_, byteCount_);
}

}  // namespace rm27::video
