#include "app/terminal_controller.hpp"

#include "sim/sim_referee_receiver.hpp"
#include "sim/sim_mqtt_receiver.hpp"
#include "video/udp_video_ingress.hpp"

#include <algorithm>
#include <utility>
#include <QDateTime>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVariantList>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QPlaybackOptions>
#endif

using rm27::CommandKind;
using rm27::GateBlock;
using rm27::MatchPhase;
using rm27::TerminalMode;

namespace {

// GUI 心跳周期与「疑似被阻塞」阈值（项目决策）。
constexpr int kHeartbeatIntervalMs = 200;
constexpr int kGuiStallThresholdMs = 150;

}  // namespace

TerminalController::TerminalController(const bool demoMode, QObject* parent,
                                       const quint16 simulationPort,
                                       const bool simulationMqtt,
                                       const quint16 simulationMqttPort)
    : QObject(parent)
{
    connection_ = new rm27::app::ConnectionSettings(this);
    // 面板改配置（例如开局挑选本车机型）后界面要立刻跟随，因此把配置变更并入状态变更。
    connect(connection_, &rm27::app::ConnectionSettings::changed,
        this, &TerminalController::stateChanged);

    const rm27::ConnectionProfile savedProfile = connection_->profile();
    const quint16 refereePort = simulationPort != 0 ? simulationPort : savedProfile.refereePort;
    const quint16 videoProbePort = savedProfile.videoProbePort;
    const bool mqttEnabled = simulationMqtt || savedProfile.useMqtt;
    const quint16 mqttPort = simulationMqttPort != 0 ? simulationMqttPort : savedProfile.mqttPort;

    simReceiver_ = new rm27::sim::RefereeReceiver(this);
    connect(simReceiver_, &rm27::sim::RefereeReceiver::sampleReceived,
        this, &TerminalController::applySimulationSample);
    connect(simReceiver_, &rm27::sim::RefereeReceiver::dataBecameStale, this, [this] {
        state_.referee.fresh = false;
        state_.controlsArmed = false;
        hudSnapshot_.clear();
        eventMessages_.clear();
        setAlert(QStringLiteral("模拟裁判数据已过期 · 指令保持禁用"), true);
    });
    if (mqttEnabled) {
        simMqttReceiver_ = new rm27::sim::MqttReceiver(this, mqttPort);
        connect(simMqttReceiver_, &rm27::sim::MqttReceiver::payloadReceived,
                simReceiver_, &rm27::sim::RefereeReceiver::acceptPayload);
        simMqttReceiver_->start();
        mqttPortInUse_ = mqttPort;
    }

    videoIngress_ = new rm27::video::UdpVideoIngress(this);
    connect(videoIngress_, &rm27::video::UdpVideoIngress::linkChanged, this, [this](const bool fresh) {
        if (simulationVideoActive_) return;
        state_.video.connected = state_.video.connected || fresh;
        state_.video.fresh = fresh;
        if (!fresh) {
            videoStatusLabel_ = QStringLiteral("UDP 视频数据已过期 · 无解码画面");
        }
        emit stateChanged();
    });
    connect(videoIngress_, &rm27::video::UdpVideoIngress::statsChanged,
        this, [this](const quint64, const quint64) {
            if (simulationVideoActive_) return;
            videoStatusLabel_ = videoDiagnosticsLabel();
            emit stateChanged();
        });
    // 分阶段诊断：断流开始与重同步都刷新一次读数，便于现场判断是「没数据」还是「数据回来了」。
    connect(videoIngress_, &rm27::video::UdpVideoIngress::stallStarted,
        this, [this](const int silentMs) {
            if (simulationVideoActive_) return;
            videoStatusLabel_ = QStringLiteral("UDP 视频断流 %1 ms · 无解码画面").arg(silentMs);
            emit stateChanged();
        });
    connect(videoIngress_, &rm27::video::UdpVideoIngress::resynced,
        this, [this](const int stallMs) {
            if (simulationVideoActive_) return;
            videoStatusLabel_ = QStringLiteral("UDP 视频已重同步 · 断流 %1 ms").arg(stallMs);
            emit stateChanged();
        });

    videoFrameStaleTimer_ = new QTimer(this);
    videoFrameStaleTimer_->setSingleShot(true);
    videoFrameStaleTimer_->setInterval(700);
    connect(videoFrameStaleTimer_, &QTimer::timeout, this, [this] {
        state_.video.fresh = false;
        videoStatusLabel_ = QStringLiteral("本机 UDP 测试图传已过期");
        emit stateChanged();
    });

    // GUI 线程心跳：解码若回到 GUI 线程，这里的抖动会立刻显现。
    // 「把解码交给最擅长的进程/线程」这条要求，用可观测指标守住，而不是只写在注释里。
    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setInterval(kHeartbeatIntervalMs);
    connect(heartbeatTimer_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 drift = now - heartbeatExpectedAtMs_;
        heartbeatExpectedAtMs_ = now + kHeartbeatIntervalMs;
        guiJitterMs_ = static_cast<int>(
            std::clamp<qint64>(drift - kHeartbeatIntervalMs, 0, 60000));
        maxGuiJitterMs_ = std::max(maxGuiJitterMs_, guiJitterMs_);
        const bool stalled = guiJitterMs_ > kGuiStallThresholdMs;
        bool needsStateRefresh = stalled != guiThreadStalled_;
        if (stalled && !guiThreadStalled_) {
            // 计数：瞬时标志很快会被下一个心跳清零，累计次数才便于事后定位。
            ++guiStallCount_;
        }
        guiThreadStalled_ = stalled;

        // 新鲜度分级变化时刷新一次读数（分级只用于显示，不参与放行判断）。
        const rm27::FreshnessGrade grade = rm27::gradeForAge(
            lastRefereeSampleAtMs_ < 0 ? -1 : now - lastRefereeSampleAtMs_);
        if (grade != refereeGrade_) {
            refereeGrade_ = grade;
            needsStateRefresh = true;
        }
        emit performanceChanged();
        if (needsStateRefresh) {
            emit stateChanged();
        }
    });
    heartbeatExpectedAtMs_ = QDateTime::currentMSecsSinceEpoch() + kHeartbeatIntervalMs;
    heartbeatTimer_->start();

    // 出帧率：1 秒窗口内的解码帧数。
    fpsTimer_ = new QTimer(this);
    fpsTimer_->setInterval(1000);
    connect(fpsTimer_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsed = std::max<qint64>(1, now - fpsWindowStartedAtMs_);
        presentFps_ = static_cast<double>(decodedFrames_ - fpsWindowFrames_) * 1000.0
            / static_cast<double>(elapsed);
        fpsWindowFrames_ = decodedFrames_;
        fpsWindowStartedAtMs_ = now;
        emit performanceChanged();
    });
    fpsWindowStartedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    fpsTimer_->start();

    const bool refereeListening = simReceiver_->start(refereePort);
    const bool videoListening = videoIngress_->start(videoProbePort);

    if (demoMode) {
        state_.phase = MatchPhase::Match;
        state_.referee = {true, true};
        state_.video = {true, true};
        state_.controller = {true, true};
        videoStatusLabel_ = QStringLiteral("演示视频状态 · 解码器待接入");
        setAlert(QStringLiteral("演示数据已连接 · 控制仍处于锁定状态"), false);
    } else if (!refereeListening || !videoListening) {
        setAlert(QStringLiteral("无法监听本机端口 %1/%2").arg(refereePort).arg(videoProbePort), true);
    } else if (rm27::profileUsesOfflineSentinel(savedProfile)) {
        // 上次就停在离线演练：启动时直接以模拟模式呈现，等用户在启动面板确认。
        offlineDrill_ = true;
        state_.mode = TerminalMode::Simulation;
        setAlert(QStringLiteral("离线演练待确认 · 哨兵段 %1")
                     .arg(QString::fromUtf8(rm27::kOfflineSentinelCidr)), false);
    }
}

QString TerminalController::modeLabel() const
{
    switch (state_.mode) {
    case TerminalMode::HeavyOperator:
        return QStringLiteral("重装操作");
    case TerminalMode::IntegratedCommand:
        return QStringLiteral("综合指挥");
    case TerminalMode::Fallback:
        return QStringLiteral("自定义客户端");
    case TerminalMode::Simulation:
        return QStringLiteral("模拟");
    case TerminalMode::Replay:
        return QStringLiteral("回放");
    }
    return QStringLiteral("未知");
}

QString TerminalController::phaseLabel() const
{
    switch (state_.phase) {
    case MatchPhase::Boot:
        return QStringLiteral("启动中");
    case MatchPhase::Preparation:
        return QStringLiteral("准备阶段");
    case MatchPhase::SelfCheck:
        return QStringLiteral("自检阶段");
    case MatchPhase::Countdown:
        return QStringLiteral("倒计时");
    case MatchPhase::Match:
        return QStringLiteral("比赛中");
    case MatchPhase::Ended:
        return QStringLiteral("比赛结束");
    }
    return QStringLiteral("未知");
}

QString TerminalController::alertText() const
{
    return alertText_;
}

bool TerminalController::alertCritical() const noexcept
{
    return alertCritical_;
}

bool TerminalController::refereeConnected() const noexcept
{
    return state_.referee.connected;
}

bool TerminalController::refereeFresh() const noexcept
{
    return state_.referee.fresh;
}

bool TerminalController::videoConnected() const noexcept
{
    return state_.video.connected && state_.video.fresh;
}

bool TerminalController::controllerConnected() const noexcept
{
    return state_.controller.connected;
}

bool TerminalController::controlsArmed() const noexcept
{
    return state_.controlsArmed;
}

QString TerminalController::hpLabel() const
{
    return hpLabel_;
}

QString TerminalController::chassisPowerLabel() const
{
    return chassisPowerLabel_;
}

QString TerminalController::bufferEnergyLabel() const
{
    return bufferEnergyLabel_;
}

QString TerminalController::barrelHeatLabel() const
{
    return barrelHeatLabel_;
}

QString TerminalController::ammoLabel() const
{
    return ammoLabel_;
}

QString TerminalController::latencyLabel() const
{
    return latencyLabel_;
}

QString TerminalController::packetLossLabel() const
{
    return packetLossLabel_;
}

QString TerminalController::videoStatusLabel() const
{
    return videoStatusLabel_;
}

// 当前可观测的链路阶段。组装（分片重组）与解码两段在官方 UDP 分包格式到位前不存在，
// 因此这里不编造它们的状态，只如实报告「等待首包 / 已收包 / 断流 / 收包到出帧」。
QString TerminalController::videoStage() const
{
    if (localPlayback_) {
        return QStringLiteral("本地回放（不经过探针）");
    }
    if (!videoIngress_ || !videoIngress_->receivedAny()) {
        return QStringLiteral("等待首包");
    }
    if (!state_.video.fresh) {
        return QStringLiteral("断流");
    }
    if (decodedFrames_ > 0) {
        return QStringLiteral("收包→出帧");
    }
    return QStringLiteral("已收包·等待解码");
}

QVariantMap TerminalController::videoDiagnostics() const
{
    const quint64 packets = videoIngress_ ? videoIngress_->packetCount() : 0;
    const quint64 bytes = videoIngress_ ? videoIngress_->byteCount() : 0;
    const qint64 silent = videoIngress_ ? videoIngress_->silentMs() : -1;

    QVariantMap map;
    map.insert(QStringLiteral("stage"), videoStage());
    // 明确标出还有哪两段没接：避免把「探针度量」误读成完整流水线诊断。
    map.insert(QStringLiteral("stagePending"), QStringLiteral("组装/解码（待官方 UDP 分包格式）"));
    map.insert(QStringLiteral("packets"), QVariant::fromValue(packets));
    map.insert(QStringLiteral("bytes"), QVariant::fromValue(bytes));
    map.insert(QStringLiteral("silentMs"), QVariant::fromValue(silent));
    map.insert(QStringLiteral("arrivalGaps"),
        QVariant::fromValue(videoIngress_ ? videoIngress_->arrivalGapCount() : 0u));
    map.insert(QStringLiteral("stalls"),
        QVariant::fromValue(videoIngress_ ? videoIngress_->stallCount() : 0u));
    map.insert(QStringLiteral("resyncs"),
        QVariant::fromValue(videoIngress_ ? videoIngress_->resyncCount() : 0u));
    map.insert(QStringLiteral("rejectedSources"),
        QVariant::fromValue(videoIngress_ ? videoIngress_->rejectedSourceCount() : 0u));
    map.insert(QStringLiteral("decodedFrames"), decodedFrames_);
    return map;
}

QString TerminalController::videoDiagnosticsLabel() const
{
    const QVariantMap map = videoDiagnostics();
    const QString silent = map.value(QStringLiteral("silentMs")).toLongLong() < 0
        ? QStringLiteral("--")
        : QStringLiteral("%1 ms").arg(map.value(QStringLiteral("silentMs")).toLongLong());
    return QStringLiteral("UDP 探针 %1 包 / %2 KiB · 阶段=%3 · 缺口 %4 断流 %5 重同步 %6 来源拒绝 %7"
                          " · 静默 %8 · %9")
        .arg(map.value(QStringLiteral("packets")).toULongLong())
        .arg(map.value(QStringLiteral("bytes")).toULongLong() / 1024)
        .arg(map.value(QStringLiteral("stage")).toString())
        .arg(map.value(QStringLiteral("arrivalGaps")).toUInt())
        .arg(map.value(QStringLiteral("stalls")).toUInt())
        .arg(map.value(QStringLiteral("resyncs")).toUInt())
        .arg(map.value(QStringLiteral("rejectedSources")).toUInt())
        .arg(silent)
        .arg(map.value(QStringLiteral("stagePending")).toString());
}

QVariantMap TerminalController::hudSnapshot() const
{
    return hudSnapshot_;
}

QVariantList TerminalController::eventMessages() const
{
    return eventMessages_;
}

void TerminalController::toggleFallbackMode()
{
    if (simulationActive_ || localPlayback_ || offlineDrill_
        || state_.mode == TerminalMode::Simulation || state_.mode == TerminalMode::Replay) {
        setAlert(QStringLiteral("模拟、离线演练或回放期间禁止切换到可下发模式"), true);
        return;
    }

    state_.mode = state_.mode == TerminalMode::Fallback
        ? TerminalMode::HeavyOperator
        : TerminalMode::Fallback;
    state_.controlsArmed = false;
    setAlert(QStringLiteral("模式已切换 · 控制已自动锁定，请人工确认"), true);
}

void TerminalController::applySimulationSample(const rm27::sim::RefereeSample& sample)
{
    lastRefereeSampleAtMs_ = QDateTime::currentMSecsSinceEpoch();
    refereeGrade_ = rm27::FreshnessGrade::Fresh;
    const bool firstSample = !simulationActive_;
    simulationActive_ = true;
    state_.mode = localPlayback_ ? TerminalMode::Replay : TerminalMode::Simulation;
    state_.phase = sample.phase;
    state_.referee = {true, true};
    state_.controlsArmed = false;

    hpLabel_ = QStringLiteral("%1 / %2").arg(sample.hp).arg(sample.maxHp);
    chassisPowerLabel_ = QStringLiteral("%1 W").arg(sample.chassisPowerW, 0, 'f', 1);
    bufferEnergyLabel_ = QStringLiteral("%1 J").arg(sample.bufferEnergyJ, 0, 'f', 1);
    barrelHeatLabel_ = QString::number(sample.barrelHeat);
    ammoLabel_ = QString::number(sample.ammo);
    latencyLabel_ = QStringLiteral("%1 ms").arg(sample.latencyMs);
    packetLossLabel_ = QStringLiteral("%1 %").arg(sample.packetLossPercent, 0, 'f', 1);

    if (sample.event) {
        bool seen = false;
        for (const QVariant& message : std::as_const(eventMessages_)) {
            if (message.toMap().value(QStringLiteral("id")).toString() == sample.event->id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            eventMessages_.append(QVariantMap{
                {QStringLiteral("id"), sample.event->id},
                {QStringLiteral("category"), sample.event->category},
                {QStringLiteral("text"), sample.event->text},
            });
            if (eventMessages_.size() > 24) {
                eventMessages_.removeFirst();
            }
        }
    }

    hudSnapshot_.clear();
    if (sample.hud) {
        const auto& hud = *sample.hud;
        QVariantList friendlyHp, enemyHp, friendlyPositions, enemyPositions;
        for (int index = 0; index < 6; ++index) {
            friendlyHp.append(hud.friendlyHp[index]);
            enemyHp.append(hud.enemyHp[index]);
            friendlyPositions.append(QVariantMap{
                {QStringLiteral("x"), hud.friendlyPositions[index][0]},
                {QStringLiteral("y"), hud.friendlyPositions[index][1]}});
            enemyPositions.append(QVariantMap{
                {QStringLiteral("x"), hud.enemyPositions[index][0]},
                {QStringLiteral("y"), hud.enemyPositions[index][1]}});
        }
        hudSnapshot_ = {
            {QStringLiteral("ownTeam"), hud.ownTeam},
            {QStringLiteral("friendlyHp"), friendlyHp},
            {QStringLiteral("enemyHp"), enemyHp},
            {QStringLiteral("friendlyPositions"), friendlyPositions},
            {QStringLiteral("enemyPositions"), enemyPositions},
            {QStringLiteral("friendlyCoins"), hud.friendlyCoins},
            {QStringLiteral("enemyCoins"), hud.enemyCoins},
            {QStringLiteral("restartCount"), hud.restartCount},
            {QStringLiteral("pitchDeg"), hud.pitchDeg},
            {QStringLiteral("speedMps"), hud.speedMps},
            {QStringLiteral("allowedShots"), hud.allowedShots},
        };
        if (hud.level) hudSnapshot_.insert(QStringLiteral("level"), *hud.level);
        // 偏航角与俯仰角分开：只有模拟包带了 yaw 才下发，界面据此显示未知。
        if (hud.yawDeg) hudSnapshot_.insert(QStringLiteral("yawDeg"), *hud.yawDeg);
        // 本车下标：界面据此把「本车」那一格与左下角自车模块换成对应的机型照片。
        if (hud.selfIndex) hudSnapshot_.insert(QStringLiteral("selfIndex"), *hud.selfIndex);
        if (hud.shield) {
            hudSnapshot_.insert(QStringLiteral("shield"), *hud.shield);
            hudSnapshot_.insert(QStringLiteral("maxShield"), *hud.maxShield);
        }
        if (hud.actions) {
            hudSnapshot_.insert(QStringLiteral("climbActive"), hud.actions->climb);
            hudSnapshot_.insert(QStringLiteral("fireActive"), hud.actions->fire);
            hudSnapshot_.insert(QStringLiteral("speedUpActive"), hud.actions->speedUp);
        }
    }

    if (firstSample) {
        setAlert(QStringLiteral("项目模拟裁判数据已连接 · 指令下发已禁用"), false);
    } else {
        emit stateChanged();
    }
}

void TerminalController::toggleControlsArmed()
{
    if (state_.controlsArmed) {
        state_.controlsArmed = false;
        setAlert(QStringLiteral("操作控制已锁定"), false);
        return;
    }

    const auto decision = rm27::evaluateCommand(state_, CommandKind::RobotMotion);
    if (decision.block != GateBlock::ControlsDisarmed) {
        showDecision(decision);
        return;
    }

    state_.controlsArmed = true;
    setAlert(QStringLiteral("操作控制已解锁 · 指令仍受规则门约束"), false);
}

void TerminalController::inspectMotionGate()
{
    showDecision(rm27::evaluateCommand(state_, CommandKind::RobotMotion));
}

void TerminalController::enterLocalPlayback()
{
    simulationVideoActive_ = false;
    videoFrameStaleTimer_->stop();
    decodedFrames_ = 0;
    emit frameCountChanged();
    localPlayback_ = true;
    videoSource_ = rm27::VideoSource::LocalPlayback;
    lastSourceSwitchAtMs_ = QDateTime::currentMSecsSinceEpoch();
    state_.mode = TerminalMode::Replay;
    state_.controlsArmed = false;
    setAlert(QStringLiteral("本地视频回放 · 控制已锁定"), false);
}

void TerminalController::enterSimulationVideo()
{
    decodedFrames_ = 0;
    emit frameCountChanged();
    simulationVideoActive_ = true;
    simulationActive_ = true;
    localPlayback_ = false;
    videoSource_ = rm27::VideoSource::SimulationUdp;
    lastSourceSwitchAtMs_ = QDateTime::currentMSecsSinceEpoch();
    state_.mode = TerminalMode::Simulation;
    state_.video = {false, false};
    state_.controlsArmed = false;
    videoStatusLabel_ = QStringLiteral("等待本机 UDP 测试图传 · 127.0.0.1:39003");
    setAlert(QStringLiteral("本机视频联调 · 指令下发已禁用"), false);
}

QObject* TerminalController::connectionObject() const
{
    return connection_;
}

rm27::app::ConnectionSettings* TerminalController::connection() const noexcept
{
    return connection_;
}

bool TerminalController::offlineDrill() const noexcept
{
    return offlineDrill_;
}

QString TerminalController::linkBannerText() const
{
    if (!offlineDrill_) {
        return QString();
    }
    return QStringLiteral("离线演练 · 哨兵段 %1 · 非官方链路 · 指令已锁定")
        .arg(QString::fromUtf8(rm27::kOfflineSentinelCidr));
}

QString TerminalController::ownTeamPreference() const
{
    return connection_ ? connection_->ownTeam() : QStringLiteral("unknown");
}

QString TerminalController::ownRobotPreference() const
{
    return connection_ ? connection_->ownRobot() : QString();
}

bool TerminalController::applyConnectionProfile()
{
    if (!connection_) {
        return false;
    }
    const rm27::ConnectionProfile profile = connection_->profile();
    const rm27::ProfileIssue issue = rm27::validateProfile(profile);
    if (issue != rm27::ProfileIssue::Ok) {
        setAlert(QString::fromUtf8(rm27::profileIssueText(issue)), true);
        return false;
    }

    // MQTT 接收器按端口重建：只订阅本机模拟主题，不发布、不连接外部 broker。
    if (profile.useMqtt && (!simMqttReceiver_ || mqttPortInUse_ != profile.mqttPort)) {
        delete simMqttReceiver_;
        simMqttReceiver_ = new rm27::sim::MqttReceiver(this, profile.mqttPort);
        connect(simMqttReceiver_, &rm27::sim::MqttReceiver::payloadReceived,
                simReceiver_, &rm27::sim::RefereeReceiver::acceptPayload);
        simMqttReceiver_->start();
        mqttPortInUse_ = profile.mqttPort;
    } else if (!profile.useMqtt && simMqttReceiver_) {
        delete simMqttReceiver_;
        simMqttReceiver_ = nullptr;
        mqttPortInUse_ = 0;
    }

    const bool refereeListening = simReceiver_->start(profile.refereePort);
    const bool videoListening = videoIngress_->start(profile.videoProbePort);
    if (!refereeListening || !videoListening) {
        setAlert(QStringLiteral("无法监听本机端口 %1/%2，请检查端口占用")
                     .arg(profile.refereePort)
                     .arg(profile.videoProbePort),
            true);
        return false;
    }

    offlineDrill_ = rm27::profileUsesOfflineSentinel(profile);
    simulationActive_ = false;
    localPlayback_ = false;
    simulationVideoActive_ = false;
    if (videoFrameStaleTimer_) {
        videoFrameStaleTimer_->stop();
    }
    decodedFrames_ = 0;
    emit frameCountChanged();

    state_.mode = rm27::modeForProfile(profile);
    state_.controlsArmed = false;
    state_.referee = {false, false};
    state_.video = {false, false};

    if (offlineDrill_) {
        videoStatusLabel_ = QStringLiteral("离线演练 · 等待本机模拟数据");
    } else if (state_.mode == TerminalMode::Simulation) {
        videoStatusLabel_ = QStringLiteral("本机模拟 · 等待测试数据与本地图传");
    } else {
        videoStatusLabel_ = QStringLiteral("等待 UDP 视频数据");
    }

    connection_->save();

    if (offlineDrill_) {
        connection_->recordOfflineEntry();
        setAlert(QStringLiteral("离线演练模式 · 哨兵段 %1 · 不连接任何外部地址，也无需要网线")
                     .arg(QString::fromUtf8(rm27::kOfflineSentinelCidr)),
            false);
    } else {
        setAlert(QStringLiteral("链路参数已应用 · %1 · 指令仍受规则门约束")
                     .arg(connection_->entryModeLabel()),
            false);
    }
    return true;
}

void TerminalController::enterOfflineDrill()
{
    if (!connection_) {
        return;
    }
    connection_->enterOfflineSentinel();
    applyConnectionProfile();
}

void TerminalController::configureSimulationVideoPlayer(QObject* object)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    if (auto* player = qobject_cast<QMediaPlayer*>(object)) {
        auto options = player->playbackOptions();
        options.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
        options.setProbeSize(32 * 1024);
        player->setPlaybackOptions(options);
    }
#else
    Q_UNUSED(object);
#endif
}

void TerminalController::observeVideoSink(QVideoSink* sink)
{
    if (!sink) return;
    connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!frame.isValid()) return;
        ++decodedFrames_;
        if (decodedFrames_ == 1) qInfo("LOCAL_VIDEO_FIRST_FRAME");
        if (simulationVideoActive_) {
            state_.video = {true, true};
            videoStatusLabel_ = QStringLiteral("本机 UDP 图传 · 已解码 %1 帧").arg(decodedFrames_);
            videoFrameStaleTimer_->start();
            emit stateChanged();
        }
        emit frameCountChanged();
    });
}

void TerminalController::showDecision(const rm27::GateDecision& decision)
{
    if (decision.allowed) {
        setAlert(QStringLiteral("指令安全门检查通过（未发送真实指令）"), false);
        return;
    }
    setAlert(QStringLiteral("指令被阻止：") + blockReasonText(decision.block), true);
}

QString TerminalController::blockReasonText(const rm27::GateBlock block)
{
    switch (block) {
    case GateBlock::UiOccupied:
        return QStringLiteral("界面正在占用操作视野");
    case GateBlock::OfflineMode:
        return QStringLiteral("模拟或回放模式禁止下发");
    case GateBlock::PhaseBlocked:
        return QStringLiteral("当前比赛阶段不允许操作");
    case GateBlock::RefereeDisconnected:
        return QStringLiteral("裁判系统未连接");
    case GateBlock::RefereeDataStale:
        return QStringLiteral("裁判系统数据已过期");
    case GateBlock::ControllerDisconnected:
        return QStringLiteral("有线控制器已断开");
    case GateBlock::ControlsDisarmed:
        return QStringLiteral("操作控制尚未解锁");
    case GateBlock::None:
        break;
    }
    return QStringLiteral("指令安全门状态异常");
}

bool TerminalController::uiOccupied() const noexcept
{
    return state_.uiOccupied;
}

void TerminalController::setUiOccupied(const bool occupied)
{
    if (state_.uiOccupied == occupied) {
        return;
    }
    state_.uiOccupied = occupied;
    if (occupied) {
        // 进入占屏状态立即解除解锁：不允许「画面被盖住但控制仍然开着」。
        state_.controlsArmed = false;
        setAlert(QStringLiteral("界面正在占用操作视野 · 指令已被拦截"), true);
    } else {
        setAlert(QStringLiteral("界面已收起 · 指令仍受规则门约束"), false);
    }
}

QVariantList TerminalController::inputActions() const
{
    QVariantList list;
    for (const rm27::InputAction& action : rm27::defaultInputActions()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(action.id)},
            {QStringLiteral("label"), QString::fromStdString(action.label)},
            {QStringLiteral("group"), QString::fromStdString(action.group)},
            {QStringLiteral("keys"), QString::fromStdString(action.defaultKeys)},
            {QStringLiteral("hint"), QString::fromStdString(rm27::actionHint(action))},
            {QStringLiteral("isCommand"), action.kind == CommandKind::RobotMotion},
        });
    }
    return list;
}

QString TerminalController::lastInputDecision() const
{
    return lastInputDecision_;
}

bool TerminalController::submitInputAction(const QString& actionId)
{
    const rm27::InputAction* action = rm27::findInputAction(actionId.toStdString());
    if (action == nullptr) {
        lastInputDecision_ = QStringLiteral("未知输入动作：%1").arg(actionId);
        emit stateChanged();
        return false;
    }

    const QString hint = QString::fromStdString(rm27::actionHint(*action));
    const rm27::GateDecision gate = rm27::evaluateCommand(state_, action->kind);

    if (action->kind == CommandKind::RobotMotion && !gate.allowed) {
        // 命令类动作必须先过门，被拦就不派发。
        lastInputDecision_ = hint + QStringLiteral(" · 闸门拦截：") + blockReasonText(gate.block);
        showDecision(gate);
        emit stateChanged();
        return false;
    }

    // 界面类动作只派发（否则占屏面板无法被关掉）；同时记录闸门状态供现场判断。
    lastInputDecision_ = hint
        + (gate.allowed ? QStringLiteral(" · 闸门通过（未发送真实指令）")
                        : QStringLiteral(" · 界面动作已派发 · 闸门状态：") + blockReasonText(gate.block));
    emit inputActionRequested(actionId);
    emit stateChanged();
    return true;
}

QString TerminalController::videoSourceLabel() const
{
    return QString::fromUtf8(rm27::videoSourceText(videoSource_));
}

QString TerminalController::sourceSwitchHint() const
{
    return sourceSwitchHint_;
}

bool TerminalController::requestVideoSource(const int source)
{
    rm27::VideoSource target = rm27::VideoSource::None;
    if (source == 1) {
        target = rm27::VideoSource::LocalPlayback;
    } else if (source == 2) {
        target = rm27::VideoSource::SimulationUdp;
    } else if (source != 0) {
        sourceSwitchHint_ = QStringLiteral("未知画面来源编号：%1").arg(source);
        emit stateChanged();
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceLastSwitch = lastSourceSwitchAtMs_ < 0 ? -1 : now - lastSourceSwitchAtMs_;
    const rm27::VideoSourceDecision decision
        = rm27::decideVideoSourceSwitch(videoSource_, target, sinceLastSwitch);

    sourceSwitchHint_ = QStringLiteral("%1 → %2 · %3")
                            .arg(QString::fromUtf8(rm27::videoSourceText(videoSource_)),
                                QString::fromUtf8(rm27::videoSourceText(target)),
                                QString::fromUtf8(decision.reason));
    if (!decision.allowed) {
        if (!decision.sameSource && lastInputDecision_.isEmpty()) {
            lastInputDecision_ = sourceSwitchHint_;
        }
        emit stateChanged();
        return false;
    }

    // 真正的切源由表现层完成（本地回放要用户选文件），完成时经 enter* 记录来源与时刻。
    videoSource_ = target;
    if (target == rm27::VideoSource::None) {
        lastSourceSwitchAtMs_ = now;
    }
    emit videoSourceRequested(source);
    emit stateChanged();
    return true;
}

double TerminalController::presentFps() const noexcept
{
    return presentFps_;
}

int TerminalController::guiJitterMs() const noexcept
{
    return guiJitterMs_;
}

int TerminalController::guiPeakJitterMs() const noexcept
{
    return maxGuiJitterMs_;
}

bool TerminalController::guiThreadStalled() const noexcept
{
    return guiThreadStalled_;
}

int TerminalController::guiStallCount() const noexcept
{
    return guiStallCount_;
}

QString TerminalController::performanceLabel() const
{
    return QStringLiteral("出帧 %1 FPS · GUI 抖动 %2 ms（峰值 %3 · 阻塞 %4 次）%5")
        .arg(presentFps_, 0, 'f', 1)
        .arg(guiJitterMs_)
        .arg(maxGuiJitterMs_)
        .arg(guiStallCount_)
        .arg(guiThreadStalled_ ? QStringLiteral(" · GUI 线程疑似被阻塞") : QString());
}

QString TerminalController::refereeFreshnessLabel() const
{
    return QString::fromUtf8(rm27::freshnessGradeText(refereeGrade_));
}

double TerminalController::refereeFreshnessScale() const
{
    return rm27::freshnessScale(refereeGrade_);
}

void TerminalController::setAlert(QString text, const bool critical)
{
    alertText_ = std::move(text);
    alertCritical_ = critical;
    emit stateChanged();
}
