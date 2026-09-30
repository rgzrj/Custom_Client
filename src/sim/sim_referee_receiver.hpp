#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <optional>
#include <array>

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

class RefereeReceiver final : public QObject {
    Q_OBJECT

public:
    explicit RefereeReceiver(QObject* parent = nullptr);
    [[nodiscard]] bool start(quint16 port = kRefereePort);
    void acceptPayload(const QByteArray& payload);

signals:
    void sampleReceived(const rm27::sim::RefereeSample& sample);
    void dataBecameStale();

private:
    void readPendingDatagrams();

    QUdpSocket socket_;
    QTimer staleTimer_;
    quint64 lastSequence_{0};
    bool receivedAny_{false};
};

}  // namespace rm27::sim
