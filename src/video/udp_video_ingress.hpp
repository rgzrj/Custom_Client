#pragma once

#include <QObject>
#include <QTimer>
#include <QUdpSocket>

namespace rm27::video {

inline constexpr quint16 kSimulationVideoPort = 39002;

// 项目模拟图传探针。
//
// 官方 UDP 分包格式未到位前，本类只做「到了多少包、多久没到、是否重同步、来源是否可信」
// 这一类与协议无关的度量：不缓存、不重组、不推断序号，也不假装能判定官方分片缺口。
// 组装与解码阶段的计数留给协议到位后的流水线实现（见 TerminalController 的诊断输出）。
class UdpVideoIngress final : public QObject {
    Q_OBJECT

public:
    explicit UdpVideoIngress(QObject* parent = nullptr);
    [[nodiscard]] bool start(quint16 port = kSimulationVideoPort);

    [[nodiscard]] quint64 packetCount() const noexcept { return packetCount_; }
    [[nodiscard]] quint64 byteCount() const noexcept { return byteCount_; }
    [[nodiscard]] bool receivedAny() const noexcept { return receivedAny_; }
    // 相邻包到达间隔超过 kArrivalGapMs 的次数（丢包/卡顿的粗代理，不是官方序号缺口）。
    [[nodiscard]] quint32 arrivalGapCount() const noexcept { return arrivalGapCount_; }
    [[nodiscard]] quint32 stallCount() const noexcept { return stallCount_; }
    [[nodiscard]] quint32 resyncCount() const noexcept { return resyncCount_; }
    // 非环回来源被丢弃的报文数：探针只服务本机联调，出现非零说明有别的主机在往本端口发包。
    [[nodiscard]] quint32 rejectedSourceCount() const noexcept { return rejectedSourceCount_; }
    // 距最后一包的毫秒数；从未收到时为 -1。
    [[nodiscard]] qint64 silentMs() const;

signals:
    void linkChanged(bool fresh);
    void statsChanged(quint64 packetCount, quint64 byteCount);
    void stallStarted(int silentMs);
    void resynced(int stallMs);

private:
    void readPendingDatagrams();
    void reportStats();
    void noteArrival();

    QUdpSocket socket_;
    QTimer staleTimer_;
    QTimer statsTimer_;
    quint64 packetCount_{0};
    quint64 byteCount_{0};
    quint64 reportedPacketCount_{0};
    qint64 lastDatagramAtMs_{0};
    qint64 stallBeganAtMs_{0};
    bool receivedAny_{false};
    bool fresh_{false};
    quint32 arrivalGapCount_{0};
    quint32 stallCount_{0};
    quint32 resyncCount_{0};
    quint32 rejectedSourceCount_{0};
};

}  // namespace rm27::video
