#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <optional>
#include <array>
#include <vector>

#include "core/terminal_state.hpp"

namespace rm27::sim {

inline constexpr quint16 kRefereePort = 39001;

struct RefereeSample {
    quint64 sequence{0};
    qint64 sentAtUnixMs{0};
    MatchPhase phase{MatchPhase::Boot};
    int hp{0};
    int maxHp{0};
    double chassisPowerW{0.0};
    double bufferEnergyJ{0.0};
    int barrelHeat{0};
    int ammo{0};
    int latencyMs{0};
    double packetLossPercent{0.0};
    struct Hud {
        QString ownTeam;
        std::array<int, 6> friendlyHp{};
        std::array<int, 6> enemyHp{};
        std::array<std::array<double, 2>, 6> friendlyPositions{};
        std::array<std::array<double, 2>, 6> enemyPositions{};
        int friendlyCoins{0};
        int enemyCoins{0};
        int restartCount{0};
        double pitchDeg{0.0};
        // 偏航角：项目模拟可选字段（-180..180）。官方语义未确认，旧模拟包不带它时保持未知。
        std::optional<double> yawDeg;
        // 本车下标：项目模拟可选字段（0..5，对应队列表六个位置）。
        // 官方协议未确认，旧模拟包不带它时保持未知，界面不做本车标记。
        std::optional<int> selfIndex;
        double speedMps{0.0};
        int allowedShots{0};
        std::optional<int> level;
        std::optional<int> shield;
        std::optional<int> maxShield;
        struct Actions {
            bool climb{false};
            bool fire{false};
            bool speedUp{false};
        };
        std::optional<Actions> actions;
        struct Structure {
            int hp{0};
            int maxHp{0};
        };
        // 项目模拟：依次为我方基地、敌方基地、我方前哨、敌方前哨。
        std::optional<std::array<Structure, 4>> structures;
    };
    std::optional<Hud> hud;
    struct Event {
        QString id;
        QString category;
        QString text;
    };
    std::optional<Event> event;
};

[[nodiscard]] std::optional<RefereeSample> parseRefereeDatagram(const QByteArray& payload);

// 仅本机模拟雷达全量快照：固定红/蓝阵营，id 为六个模拟槽位，不是正式机器人 ID。
struct RadarSample {
    quint64 sequence{0};
    struct Point { int id; double x; double y; };
    std::array<std::vector<Point>, 2> teams;
};
[[nodiscard]] std::optional<RadarSample> parseRadarDatagram(const QByteArray& payload);

class RefereeReceiver final : public QObject {
    Q_OBJECT

public:
    explicit RefereeReceiver(QObject* parent = nullptr);
    [[nodiscard]] bool start(quint16 port = kRefereePort);
    void acceptPayload(const QByteArray& payload);

signals:
    void sampleReceived(const rm27::sim::RefereeSample& sample);
    void dataBecameStale();
    void radarReceived(const rm27::sim::RadarSample& sample);
    void radarBecameStale();

private:
    void readPendingDatagrams();

    QUdpSocket socket_;
    QTimer staleTimer_;
    quint64 lastSequence_{0};
    bool receivedAny_{false};
    QTimer radarStaleTimer_;
    quint64 lastRadarSequence_{0};
    bool radarReceivedAny_{false};
};

}  // namespace rm27::sim
