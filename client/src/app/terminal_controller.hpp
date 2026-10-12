#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QVideoSink>
#include <QElapsedTimer>
#include <QPointer>

#include "app/connection_settings.hpp"
#include "app/operator_input.hpp"
#include "core/freshness_grade.hpp"
#include "core/rm2026_state.hpp"
#include "core/input_mapping.hpp"
#include "core/pairing_session.hpp"
#include "core/terminal_state.hpp"
#include "core/video_source_policy.hpp"
#ifdef RM27_UPSTREAM_BACKEND
#include "protocol/rm2026/upstream_backend.hpp"
#endif
#ifdef RM27_UPSTREAM_VIDEO
#include "video/upstream_video.hpp"
#endif

class QTimer;

namespace rm27::sim {
class RefereeReceiver;
class MqttReceiver;
struct RefereeSample;
}

namespace rm27::video {
class UdpVideoIngress;
}

class TerminalController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool upstreamVideoActive READ upstreamVideoActive NOTIFY stateChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY stateChanged)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY stateChanged)
    Q_PROPERTY(QString alertText READ alertText NOTIFY stateChanged)
    Q_PROPERTY(bool alertCritical READ alertCritical NOTIFY stateChanged)
    Q_PROPERTY(bool refereeConnected READ refereeConnected NOTIFY stateChanged)
    Q_PROPERTY(bool refereeFresh READ refereeFresh NOTIFY stateChanged)
    Q_PROPERTY(bool videoConnected READ videoConnected NOTIFY stateChanged)
    Q_PROPERTY(bool videoStale READ videoStale NOTIFY stateChanged)
    Q_PROPERTY(bool controllerConnected READ controllerConnected NOTIFY stateChanged)
    Q_PROPERTY(bool controlsArmed READ controlsArmed NOTIFY stateChanged)
    Q_PROPERTY(QString hpLabel READ hpLabel NOTIFY stateChanged)
    Q_PROPERTY(QString chassisPowerLabel READ chassisPowerLabel NOTIFY stateChanged)
    Q_PROPERTY(QString bufferEnergyLabel READ bufferEnergyLabel NOTIFY stateChanged)
    Q_PROPERTY(QString barrelHeatLabel READ barrelHeatLabel NOTIFY stateChanged)
    Q_PROPERTY(QString ammoLabel READ ammoLabel NOTIFY stateChanged)
    Q_PROPERTY(QString latencyLabel READ latencyLabel NOTIFY stateChanged)
    Q_PROPERTY(QString packetLossLabel READ packetLossLabel NOTIFY stateChanged)
    Q_PROPERTY(QString videoStatusLabel READ videoStatusLabel NOTIFY stateChanged)
    // 当前视频来源与实际出帧观测；实验收包/解码细节见运行日志。
    // 独立模拟 UDP 探针计数不能代替实际视频路径统计。
    Q_PROPERTY(QVariantMap videoDiagnostics READ videoDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap pairingDiagnostics READ pairingDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(QString videoDiagnosticsLabel READ videoDiagnosticsLabel NOTIFY stateChanged)
    // 界面占用闸门：表现层上报启动面板/弹窗/选手面板是否正在占屏。
    Q_PROPERTY(bool uiOccupied READ uiOccupied NOTIFY stateChanged)
    // 裁判数据新鲜度分级（仅用于显示与降级标注；指令门仍只看 referee.fresh）。
    Q_PROPERTY(QString refereeFreshnessLabel READ refereeFreshnessLabel NOTIFY stateChanged)
    Q_PROPERTY(double refereeFreshnessScale READ refereeFreshnessScale NOTIFY stateChanged)
    // 有线输入动作映射表（界面标题与按键提示同源）与最近一次派发结果。
    Q_PROPERTY(QVariantList inputActions READ inputActions CONSTANT)
    Q_PROPERTY(QString lastInputDecision READ lastInputDecision NOTIFY stateChanged)
    // 画面来源与切源代价提示。
    Q_PROPERTY(QString videoSourceLabel READ videoSourceLabel NOTIFY stateChanged)
    Q_PROPERTY(QString sourceSwitchHint READ sourceSwitchHint NOTIFY stateChanged)
    // 性能读数：出帧率与 GUI 线程调度抖动（用来确认解码没有回到 GUI 线程）。
    Q_PROPERTY(double presentFps READ presentFps NOTIFY performanceChanged)
    Q_PROPERTY(int guiJitterMs READ guiJitterMs NOTIFY performanceChanged)
    Q_PROPERTY(int guiPeakJitterMs READ guiPeakJitterMs NOTIFY performanceChanged)
    Q_PROPERTY(bool guiThreadStalled READ guiThreadStalled NOTIFY performanceChanged)
    Q_PROPERTY(int guiStallCount READ guiStallCount NOTIFY performanceChanged)
    Q_PROPERTY(QString performanceLabel READ performanceLabel NOTIFY performanceChanged)
    Q_PROPERTY(int decodedFrames READ decodedFrames NOTIFY frameCountChanged)
    Q_PROPERTY(QVariantMap hudSnapshot READ hudSnapshot NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap mapSnapshot READ mapSnapshot NOTIFY stateChanged)
    Q_PROPERTY(bool mqttConnected READ mqttConnected NOTIFY stateChanged)
    Q_PROPERTY(bool mqttEnabled READ mqttEnabled NOTIFY stateChanged)
    Q_PROPERTY(QVariantList eventMessages READ eventMessages NOTIFY stateChanged)
    // 启动面板的链路配置（QML 以 var 动态访问其子属性，故此处暴露为 QObject*）。
    Q_PROPERTY(QObject* connection READ connectionObject CONSTANT)
    Q_PROPERTY(bool offlineDrill READ offlineDrill NOTIFY stateChanged)
    Q_PROPERTY(bool historicalLocal READ historicalLocal CONSTANT)
    Q_PROPERTY(bool upstreamData READ upstreamData CONSTANT)
    Q_PROPERTY(bool protocolTestSource READ protocolTestSource NOTIFY stateChanged)
    Q_PROPERTY(QString linkBannerText READ linkBannerText NOTIFY stateChanged)
    Q_PROPERTY(QString ownTeamPreference READ ownTeamPreference NOTIFY stateChanged)
    // 开局配置里挑选的本车机型（角色 id，空串表示未指定）；裁判数据到位后由数据决定实际本车。
    Q_PROPERTY(QString ownRobotPreference READ ownRobotPreference NOTIFY stateChanged)

public:
    // simulationPort / simulationMqttPort 传 0 表示「用启动面板保存的端点」；
    // 传入非 0 值表示显式覆盖（自动化测试与命令行联调使用）。
    explicit TerminalController(bool demoMode, QObject* parent = nullptr,
                                quint16 simulationPort = 0,
                                bool simulationMqtt = false,
                                quint16 simulationMqttPort = 0,
                                bool historicalLocal = false,
                                bool upstreamData = false);
    bool configureRm2026(const QString& host, quint16 port, unsigned robotId, bool testSource);
    bool configureRm2026Video(const QHostAddress& bind, quint16 port,
                             const QHostAddress& source, unsigned base, bool testSource);
    [[nodiscard]] bool upstreamVideoActive() const noexcept { return upstreamVideoActive_; }
    [[nodiscard]] QVariantMap refereeDiagnostics() const;
    Q_INVOKABLE void observeInputWindow(QObject *window);
#ifdef RM27_UPSTREAM_BACKEND
    rm27::protocol::rm2026::CommandSendResult submitRm2026Command(
        const rm27::historical::rm2026::Command &command);
#endif
    // Typed, read-only application boundary; never accepts wire JSON or opens a control gate.
    void applyHistoricalSnapshot(const rm27::historical::rm2026::Snapshot& snapshot,
                                 bool subscribed);
    [[nodiscard]] bool historicalLocal() const noexcept { return historicalLocal_; }
    [[nodiscard]] bool upstreamData() const noexcept { return upstreamData_; }
    [[nodiscard]] bool protocolTestSource() const noexcept { return protocolTestSource_; }

    [[nodiscard]] QString modeLabel() const;
    [[nodiscard]] QString phaseLabel() const;
    [[nodiscard]] QString alertText() const;
    [[nodiscard]] bool alertCritical() const noexcept;
    [[nodiscard]] bool refereeConnected() const noexcept;
    [[nodiscard]] bool refereeFresh() const noexcept;
    [[nodiscard]] bool videoConnected() const noexcept;
    [[nodiscard]] bool videoStale() const noexcept;
    [[nodiscard]] bool controllerConnected() const noexcept;
    [[nodiscard]] bool controlsArmed() const noexcept;
    [[nodiscard]] QString hpLabel() const;
    [[nodiscard]] QString chassisPowerLabel() const;
    [[nodiscard]] QString bufferEnergyLabel() const;
    [[nodiscard]] QString barrelHeatLabel() const;
    [[nodiscard]] QString ammoLabel() const;
    [[nodiscard]] QString latencyLabel() const;
    [[nodiscard]] QString packetLossLabel() const;
    [[nodiscard]] QString videoStatusLabel() const;
    [[nodiscard]] QVariantMap videoDiagnostics() const;
    [[nodiscard]] QString videoDiagnosticsLabel() const;
    [[nodiscard]] bool uiOccupied() const noexcept;
    [[nodiscard]] QString refereeFreshnessLabel() const;
    [[nodiscard]] double refereeFreshnessScale() const;
    [[nodiscard]] QVariantList inputActions() const;
    [[nodiscard]] QString lastInputDecision() const;
    [[nodiscard]] QString videoSourceLabel() const;
    [[nodiscard]] QString sourceSwitchHint() const;
    [[nodiscard]] double presentFps() const noexcept;
    [[nodiscard]] int guiJitterMs() const noexcept;
    [[nodiscard]] int guiPeakJitterMs() const noexcept;
    [[nodiscard]] bool guiThreadStalled() const noexcept;
    // 累计检出的 GUI 线程阻塞次数：瞬时标志会被下一个心跳复位，计数不会。
    [[nodiscard]] int guiStallCount() const noexcept;
    [[nodiscard]] QString performanceLabel() const;

    Q_INVOKABLE void setUiOccupied(bool occupied);
    // 单一输入闸门：命令类动作必须先过 evaluateCommand，界面类动作只派发。
    Q_INVOKABLE bool submitInputAction(const QString& actionId);
    // 本地选片可重复打开；UDP 同源短路、异源需最短驻留，确认后才记录来源。
    Q_INVOKABLE bool requestVideoSource(int source);
    [[nodiscard]] QVariantMap hudSnapshot() const;
    [[nodiscard]] QVariantMap mapSnapshot() const;
    [[nodiscard]] bool mqttConnected() const { return mqttConnected_; }
    [[nodiscard]] bool mqttEnabled() const { return upstreamData_ || simMqttReceiver_ != nullptr; }
    [[nodiscard]] QVariantList eventMessages() const;
    [[nodiscard]] QObject* connectionObject() const;
    [[nodiscard]] rm27::app::ConnectionSettings* connection() const noexcept;
    [[nodiscard]] bool offlineDrill() const noexcept;
    [[nodiscard]] QString linkBannerText() const;
    [[nodiscard]] QString ownTeamPreference() const;
    [[nodiscard]] QString ownRobotPreference() const;

    Q_INVOKABLE bool applyConnectionProfile();
    Q_INVOKABLE void enterOfflineDrill();
    Q_INVOKABLE void toggleFallbackMode();
    Q_INVOKABLE void toggleControlsArmed();
    Q_INVOKABLE void inspectMotionGate();
    Q_INVOKABLE void enterLocalPlayback();
    Q_INVOKABLE void enterSimulationVideo();
    Q_INVOKABLE void configureSimulationVideoPlayer(QObject* player);
    Q_INVOKABLE void observeVideoSink(QVideoSink* sink);
    Q_INVOKABLE bool beginPairing(const QString& region, int robotId, int timeoutMs = 60000);
    Q_INVOKABLE bool confirmExternalPairing(qulonglong session);
    Q_INVOKABLE void cancelPairing();
    [[nodiscard]] QVariantMap pairingDiagnostics() const;
    // Adapter callbacks must carry the session token; reported ID is not authentication.
    bool reportPairingIdentity(qulonglong session, std::optional<std::uint32_t> id);
    bool pairingDeviceDisconnected(qulonglong session);
    int decodedFrames() const { return decodedFrames_; }

signals:
    void stateChanged();
    void frameCountChanged();
    void performanceChanged();
    // 由表现层执行的界面动作与画面来源切换请求（控制器本身不碰播放器与窗口）。
    void inputActionRequested(const QString& actionId);
    void videoSourceRequested(int source);
    void connectionProfileApplied();

private:
    void applySimulationSample(const rm27::sim::RefereeSample& sample);
    void showDecision(const rm27::GateDecision& decision);
    void setAlert(QString text, bool critical);
    [[nodiscard]] QString videoStage() const;
    [[nodiscard]] static QString blockReasonText(rm27::GateBlock block);
    void bindPairingVideoSink();
    void stopUpstreamVideo();
    void pollUpstreamVideo();

    rm27::TerminalState state_{};
    rm27::PairingSession pairing_;
    QPointer<QVideoSink> pairingVideoSink_;
    QMetaObject::Connection videoFrameConnection_;
    QMetaObject::Connection pairingVideoConnection_;
    rm27::app::ConnectionSettings* connection_{nullptr};
    rm27::app::OperatorInput* operatorInput_{nullptr};
#ifdef RM27_UPSTREAM_BACKEND
    rm27::historical::rm2026::CommandContext commandContext() const;
#endif
    void syncOperatorInput();
    rm27::sim::RefereeReceiver* simReceiver_{nullptr};
    rm27::sim::MqttReceiver* simMqttReceiver_{nullptr};
    rm27::video::UdpVideoIngress* videoIngress_{nullptr};
    QString alertText_{QStringLiteral("等待裁判系统数据")};
    QString hpLabel_{QStringLiteral("-- / --")};
    QString chassisPowerLabel_{QStringLiteral("-- W")};
    QString bufferEnergyLabel_{QStringLiteral("-- J")};
    QString barrelHeatLabel_{QStringLiteral("--")};
    QString ammoLabel_{QStringLiteral("--")};
    QString latencyLabel_{QStringLiteral("-- ms")};
    QString packetLossLabel_{QStringLiteral("-- %")};
    QString videoStatusLabel_{QStringLiteral("等待 UDP 视频数据")};
    QVariantMap hudSnapshot_;
    QVariantMap radarSnapshot_;
    QVariantList eventMessages_;
    bool alertCritical_{true};
    bool simulationActive_{false};
    const bool historicalLocal_;
    const bool upstreamData_;
    bool protocolTestSource_{false};
    unsigned requestedRobotId_{0};
    QString upstreamHost_;
    quint16 upstreamPort_{0};
#ifdef RM27_UPSTREAM_BACKEND
    std::unique_ptr<rm27::protocol::rm2026::UpstreamBackend> upstreamBackend_;
#endif
#ifdef RM27_UPSTREAM_VIDEO
    std::unique_ptr<rm27::video::UpstreamVideo> upstreamVideo_;
#endif
    bool upstreamVideoActive_{false};
    bool upstreamVideoTest_{false};
    QVariantMap upstreamVideoStats_;
    quint64 upstreamVideoSerial_{0};
    unsigned upstreamVideoReportedId_{0};
    QString historicalPhaseLabel_{QStringLiteral("未知")};
    bool offlineDrill_{false};
    bool localPlayback_{false};
    bool simulationVideoActive_{false};
    QTimer* videoFrameStaleTimer_{nullptr};
    QElapsedTimer videoClock_;
    qint64 videoStaleAtMs_{-1};
    qint64 lastVideoFrameAtMs_{-1};
    qint64 videoRecoveryMs_{-1};
    int decodedFrames_{0};
    quint16 mqttPortInUse_{0};
    bool mqttConnected_{false};

    // —— 新鲜度分级 / 输入映射 / 切源策略 / 性能读数（均为项目决策的本地状态）——
    qint64 lastRefereeSampleAtMs_{-1};
    rm27::FreshnessGrade refereeGrade_{rm27::FreshnessGrade::Unknown};
    rm27::VideoSource videoSource_{rm27::VideoSource::None};
    qint64 lastSourceSwitchAtMs_{-1};
    QString sourceSwitchHint_;
    QString lastInputDecision_;
    QElapsedTimer heartbeatClock_;
    qint64 heartbeatExpectedAtMs_{0};
    int guiJitterMs_{0};
    int maxGuiJitterMs_{0};
    bool guiThreadStalled_{false};
    int guiStallCount_{0};
    qint64 fpsWindowStartedAtMs_{0};
    int fpsWindowFrames_{0};
    double presentFps_{0.0};
};
