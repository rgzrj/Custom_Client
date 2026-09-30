#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/connection_profile.hpp"

namespace rm27::app {

// 启动面板的链路配置视图模型。
//
// 边界说明：
// - 只描述「本应用监听/连接什么」，不修改 Windows 网卡的 IP、掩码、DNS 或路由；
// - 地址一律要求 IPv4 字面量，不解析域名，因此填错也不会产生 DNS 查询或对外流量；
// - 正式 RM2027 参数（MQTT 主题、QoS、Protobuf、UDP 分包）未到位，本类不提供也不猜测。
class ConnectionSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString refereeHost READ refereeHost WRITE setRefereeHost NOTIFY changed)
    Q_PROPERTY(int refereePort READ refereePort WRITE setRefereePort NOTIFY changed)
    Q_PROPERTY(QString videoBindAddress READ videoBindAddress WRITE setVideoBindAddress NOTIFY changed)
    Q_PROPERTY(int videoProbePort READ videoProbePort WRITE setVideoProbePort NOTIFY changed)
    Q_PROPERTY(int simulationVideoPort READ simulationVideoPort WRITE setSimulationVideoPort NOTIFY changed)
    Q_PROPERTY(bool useMqtt READ useMqtt WRITE setUseMqtt NOTIFY changed)
    Q_PROPERTY(QString mqttHost READ mqttHost WRITE setMqttHost NOTIFY changed)
    Q_PROPERTY(int mqttPort READ mqttPort WRITE setMqttPort NOTIFY changed)
    Q_PROPERTY(QString ownTeam READ ownTeam WRITE setOwnTeam NOTIFY changed)
    // 开局配置里挑选的本车机型（角色 id，空串表示未指定）。
    // 只在裁判数据给出本车身份之前生效；数据到位后由裁判数据决定，见 HudOverlay.selfIndex。
    Q_PROPERTY(QString ownRobot READ ownRobot WRITE setOwnRobot NOTIFY changed)
    Q_PROPERTY(bool offlineSentinel READ offlineSentinel NOTIFY changed)
    Q_PROPERTY(bool localSimulation READ localSimulation NOTIFY changed)
    Q_PROPERTY(QString sentinelCidr READ sentinelCidr CONSTANT)
    Q_PROPERTY(QString sentinelHost READ sentinelHost CONSTANT)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY changed)
    Q_PROPERTY(QString entryModeLabel READ entryModeLabel NOTIFY changed)
    Q_PROPERTY(bool entryAllowed READ entryAllowed NOTIFY changed)
    Q_PROPERTY(bool formalLinkReady READ formalLinkReady NOTIFY changed)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY interfacesChanged)
    Q_PROPERTY(bool hasWiredLink READ hasWiredLink NOTIFY interfacesChanged)
    Q_PROPERTY(QString interfaceSummary READ interfaceSummary NOTIFY interfacesChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(int presetIndex READ presetIndex NOTIFY changed)
    Q_PROPERTY(QString auditLabel READ auditLabel NOTIFY auditChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString storageNotice READ storageNotice NOTIFY changed)

public:
    explicit ConnectionSettings(QObject* parent = nullptr);

    [[nodiscard]] rm27::ConnectionProfile profile() const;

    [[nodiscard]] QString refereeHost() const;
    [[nodiscard]] int refereePort() const;
    [[nodiscard]] QString videoBindAddress() const;
    [[nodiscard]] int videoProbePort() const;
    [[nodiscard]] int simulationVideoPort() const;
    [[nodiscard]] bool useMqtt() const;
    [[nodiscard]] QString mqttHost() const;
    [[nodiscard]] int mqttPort() const;
    [[nodiscard]] QString ownTeam() const;
    [[nodiscard]] QString ownRobot() const;

    [[nodiscard]] bool offlineSentinel() const;
    [[nodiscard]] bool localSimulation() const;
    [[nodiscard]] QString sentinelCidr() const;
    [[nodiscard]] QString sentinelHost() const;
    [[nodiscard]] QString validationMessage() const;
    [[nodiscard]] QString entryModeLabel() const;
    [[nodiscard]] bool entryAllowed() const;
    [[nodiscard]] bool formalLinkReady() const;

    [[nodiscard]] QVariantList interfaces() const;
    [[nodiscard]] bool hasWiredLink() const;
    [[nodiscard]] QString interfaceSummary() const;

    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] int presetIndex() const;
    [[nodiscard]] QString auditLabel() const;
    [[nodiscard]] QString storagePath() const;
    // 配置自愈提示：仅在文件损坏/恢复/写入失败时非空，供启动面板显示。
    [[nodiscard]] QString storageNotice() const;

    void setRefereeHost(const QString& value);
    void setRefereePort(int value);
    void setVideoBindAddress(const QString& value);
    void setVideoProbePort(int value);
    void setSimulationVideoPort(int value);
    void setUseMqtt(bool value);
    void setMqttHost(const QString& value);
    void setMqttPort(int value);
    void setOwnTeam(const QString& value);
    void setOwnRobot(const QString& value);

    // 载入/保存本机配置；存储位置可用环境变量 RM27_SETTINGS_FILE 覆盖（测试隔离用）。
    // 保存走「临时文件 + 原子替换」，并保留上一份可用配置为 .bak；
    // 载入时若文件损坏或不是本应用配置，隔离为 .corrupt-<时间戳> 后优先从 .bak 恢复。
    void load();
    [[nodiscard]] bool save();

    Q_INVOKABLE void refreshInterfaces();
    // 按预设填写字段；越界索引返回 false。
    Q_INVOKABLE bool selectPreset(int index);
    // 填入离线哨兵地址（203.0.113.7），返回是否已处于哨兵段。
    Q_INVOKABLE bool enterOfflineSentinel();
    Q_INVOKABLE void resetToLocalSimulation();
    // 记录一次离线演练入口的使用（本地留痕，便于赛后复盘）。
    Q_INVOKABLE void recordOfflineEntry();

    [[nodiscard]] rm27::ProfileIssue issue() const;

signals:
    void changed();
    void interfacesChanged();
    void auditChanged();

private:
    void updateIssue();
    void setStringField(QString& field, const QString& value);
    void setPortField(int& field, int value);
    void applyDefaults(const rm27::ConnectionProfile& defaults);
    // 读取一份 INI；文件损坏或内容不是本应用配置时返回 false 并给出原因。
    [[nodiscard]] bool readFromFile(const QString& path, QString& failure);

    QString refereeHost_{QStringLiteral("127.0.0.1")};
    int refereePort_{rm27::kDefaultRefereePort};
    QString videoBindAddress_{QStringLiteral("127.0.0.1")};
    int videoProbePort_{rm27::kDefaultVideoProbePort};
    int simulationVideoPort_{rm27::kDefaultSimulationVideoPort};
    bool useMqtt_{false};
    QString mqttHost_{QStringLiteral("127.0.0.1")};
    int mqttPort_{rm27::kDefaultMqttPort};
    QString ownTeam_{QStringLiteral("unknown")};
    QString ownRobot_;

    QVariantList interfaces_;
    QVariantList presets_;
    bool hasWiredLink_{false};
    QString interfaceSummary_;

    rm27::ProfileIssue issue_{rm27::ProfileIssue::Ok};
    bool offlineSentinel_{false};
    bool localSimulation_{true};
    bool entryAllowed_{false};
    bool formalLinkReady_{false};
    QString entryModeLabel_;

    QString lastOfflineEntryAt_;
    QString lastOfflineEntryHost_;
    QString storageNotice_;
};

}  // namespace rm27::app
