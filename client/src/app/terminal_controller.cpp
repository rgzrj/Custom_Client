#include "app/terminal_controller.hpp"

#include "sim/sim_referee_receiver.hpp"
#include "sim/sim_mqtt_receiver.hpp"
#include "video/udp_video_ingress.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <QDateTime>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVariantList>
#include <QTimer>
#include <QHostAddress>
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
                                       const quint16 simulationMqttPort,
                                       const bool historicalLocal,
                                       const bool upstreamData)
    : QObject(parent), historicalLocal_(historicalLocal), upstreamData_(upstreamData)
{
    connection_ = new rm27::app::ConnectionSettings(this);
    // 面板改配置（例如开局挑选本车机型）后界面要立刻跟随，因此把配置变更并入状态变更。
    connect(connection_, &rm27::app::ConnectionSettings::changed,
        this, &TerminalController::stateChanged);

    const rm27::ConnectionProfile savedProfile = connection_->profile();
    const quint16 refereePort = simulationPort != 0 ? simulationPort : savedProfile.refereePort;
    const quint16 videoProbePort = savedProfile.videoProbePort;
    const bool mqttEnabled = !historicalLocal_ && !upstreamData_ && connection_->entryAllowed()
        && (simulationMqtt || savedProfile.useMqtt);
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
        connect(simMqttReceiver_, &rm27::sim::MqttReceiver::linkChanged, this, [this](bool connected) {
            mqttConnected_ = connected;
            emit stateChanged();
        });
        simMqttReceiver_->start();
        mqttPortInUse_ = mqttPort;
    }

    connect(simReceiver_, &rm27::sim::RefereeReceiver::radarReceived, this,
        [this](const rm27::sim::RadarSample& sample) {
            if (upstreamData_) return;
            radarSnapshot_.clear();
            constexpr std::array names{"red", "blue"};
            for (size_t team = 0; team < names.size(); ++team) {
                QVariantList points;
                for (const auto& point : sample.teams[team])
                    points.append(QVariantMap{{"id", point.id}, {"x", point.x}, {"y", point.y}});
                radarSnapshot_.insert(QLatin1String(names[team]), points);
            }
            simulationActive_ = true;
            state_.mode = localPlayback_ ? TerminalMode::Replay : TerminalMode::Simulation;
            state_.controlsArmed = false;
            emit stateChanged();
        });
    connect(simReceiver_, &rm27::sim::RefereeReceiver::radarBecameStale, this, [this] {
        radarSnapshot_.clear();
        emit stateChanged();
    });

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
    videoClock_.start();
    videoFrameStaleTimer_->setSingleShot(true);
    videoFrameStaleTimer_->setInterval(700);
    connect(videoFrameStaleTimer_, &QTimer::timeout, this, [this] {
        videoStaleAtMs_ = videoClock_.elapsed();
        state_.video.fresh = false;
        if (upstreamVideoActive_) {
            videoStatusLabel_ = QStringLiteral("RM2026 HEVC 画面已过期 · 等待关键帧恢复");
            if (pairingVideoSink_) pairingVideoSink_->setVideoFrame({});
        } else {
            videoStatusLabel_ = decodedFrames_ > 0
                ? QStringLiteral("本机 UDP 测试图传已过期")
                : QStringLiteral("本机 UDP 图传首帧超时 · 尚无有效视频画面");
        }
        emit stateChanged();
    });

    // GUI 线程心跳：解码若回到 GUI 线程，这里的抖动会立刻显现。
    // 「把解码交给最擅长的进程/线程」这条要求，用可观测指标守住，而不是只写在注释里。
    auto* heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(kHeartbeatIntervalMs);
    connect(heartbeatTimer, &QTimer::timeout, this, [this] {
        const qint64 now = heartbeatClock_.elapsed();
        const qint64 drift = now - heartbeatExpectedAtMs_;
        heartbeatExpectedAtMs_ = now + kHeartbeatIntervalMs;
        guiJitterMs_ = static_cast<int>(
            std::clamp<qint64>(drift, 0, 60000));
        maxGuiJitterMs_ = std::max(maxGuiJitterMs_, guiJitterMs_);
        const bool stalled = guiJitterMs_ > kGuiStallThresholdMs;
        const auto pairingToken = pairing_.token();
        pairing_.tick(videoClock_.elapsed());
        bool needsStateRefresh = stalled != guiThreadStalled_ || pairingToken != pairing_.token();
        if (stalled && !guiThreadStalled_) {
            // 计数：瞬时标志很快会被下一个心跳清零，累计次数才便于事后定位。
            ++guiStallCount_;
        }
        guiThreadStalled_ = stalled;

        // 新鲜度分级变化时刷新一次读数（分级只用于显示，不参与放行判断）。
        const rm27::FreshnessGrade grade = rm27::gradeForAge(
            lastRefereeSampleAtMs_ < 0 ? -1 : QDateTime::currentMSecsSinceEpoch() - lastRefereeSampleAtMs_);
        if (grade != refereeGrade_) {
            refereeGrade_ = grade;
            needsStateRefresh = true;
        }
        emit performanceChanged();
        if (needsStateRefresh) {
            emit stateChanged();
        }
    });
    heartbeatClock_.start();
    heartbeatExpectedAtMs_ = kHeartbeatIntervalMs;
    heartbeatTimer->start();

    // 出帧率：1 秒窗口内的解码帧数。
    auto* fpsTimer = new QTimer(this);
    fpsTimer->setInterval(1000);
    connect(fpsTimer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsed = std::max<qint64>(1, now - fpsWindowStartedAtMs_);
        presentFps_ = static_cast<double>(decodedFrames_ - fpsWindowFrames_) * 1000.0
            / static_cast<double>(elapsed);
        fpsWindowFrames_ = decodedFrames_;
        fpsWindowStartedAtMs_ = now;
        emit performanceChanged();
    });
    fpsWindowStartedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    fpsTimer->start();

    const bool refereeListening = historicalLocal_ || upstreamData_
        || (connection_->entryAllowed() && simReceiver_->start(refereePort));
    const bool videoListening = historicalLocal_ || upstreamData_
        || (connection_->entryAllowed() && videoIngress_->start(videoProbePort));

    if (upstreamData_) {
        connection_->enableRm2026Profile(false);
        state_.mode = TerminalMode::HeavyOperator;
        setAlert(QStringLiteral("RM2026 裁判数据 · 等待明确连接配置"), false);
#ifdef RM27_UPSTREAM_BACKEND
        upstreamBackend_ = std::make_unique<rm27::protocol::rm2026::UpstreamBackend>();
        auto* poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] {
            if (offlineDrill_ || localPlayback_) return;
            const auto view = upstreamBackend_->view();
            applyHistoricalSnapshot(view.snapshot, view.subscribed);
        });
        poll->start(50);
#endif
    } else if (historicalLocal_) {
        simulationActive_ = true;
        state_.mode = TerminalMode::Simulation;
        setAlert(QStringLiteral("RM2026 本机合成测试 · 等待历史报文 · 禁止下发"), false);
    } else if (demoMode) {
        state_.phase = MatchPhase::Match;
        state_.referee = {true, true};
        state_.video = {true, true};
        state_.controller = {true, true};
        videoStatusLabel_ = QStringLiteral("演示视频状态 · 解码器待接入");
        setAlert(QStringLiteral("演示数据已连接 · 控制仍处于锁定状态"), false);
    } else if (!connection_->entryAllowed()) {
        setAlert(connection_->validationMessage(), true);
    } else if (!refereeListening || !videoListening) {
        setAlert(QStringLiteral("无法监听本机端口 %1/%2").arg(refereePort).arg(videoProbePort), true);
    } else if (rm27::profileUsesOfflineSentinel(savedProfile)) {
        // 上次就停在离线演练：启动时直接以模拟模式呈现，等用户在启动面板确认。
        offlineDrill_ = true;
        state_.mode = TerminalMode::Simulation;
        setAlert(QStringLiteral("离线演练待确认 · 哨兵段 %1")
                     .arg(QString::fromUtf8(rm27::kOfflineSentinelCidr)), false);
    }
#ifdef RM27_UPSTREAM_VIDEO
    auto* videoPoll = new QTimer(this);
    connect(videoPoll, &QTimer::timeout, this, &TerminalController::pollUpstreamVideo);
    videoPoll->start(16);
#endif
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
    if (historicalLocal_ || upstreamData_) return historicalPhaseLabel_;
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

bool TerminalController::videoStale() const noexcept
{
    return (simulationVideoActive_ || upstreamVideoActive_) && videoStaleAtMs_ >= 0;
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

// 实际视频出帧与独立模拟 UDP 探针分开观测。
QString TerminalController::videoStage() const
{
    if (upstreamVideoActive_) return videoStale() ? QStringLiteral("HEVC 出帧过期")
        : decodedFrames_ ? QStringLiteral("HEVC 已出帧") : QStringLiteral("HEVC 等待关键帧");
    if (localPlayback_) {
        return QStringLiteral("本地回放（不经过探针）");
    }
    if (simulationVideoActive_) {
        return videoStale() ? (decodedFrames_ > 0 ? QStringLiteral("测试 TS 出帧过期")
                                                : QStringLiteral("测试 TS 首帧超时"))
            : decodedFrames_ > 0 ? QStringLiteral("测试 TS 已出帧（不经过探针）")
                                 : QStringLiteral("测试 TS 等待首帧");
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
    const bool probe = !upstreamData_ && !historicalLocal_ && !localPlayback_ && !simulationVideoActive_ && videoIngress_;

    QVariantMap map;
    map.insert(QStringLiteral("stage"), videoStage());
    map.insert(QStringLiteral("stagePending"), upstreamVideoActive_
        ? QStringLiteral("完整组帧 → FFmpeg 7.1.5 → 现有 VideoOutput")
        : localPlayback_ ? QStringLiteral("本地文件回放；不使用 UDP 探针")
        : simulationVideoActive_ ? QStringLiteral("本机 MPEG-TS 测试；不使用 UDP 探针")
        : QStringLiteral("组装/解码（待官方 UDP 分包格式）"));
    map.insert(QStringLiteral("packetCountersSource"), upstreamVideoActive_ ? QStringLiteral("rm2026-hevc-udp")
        : probe ? QStringLiteral("simulation-udp-probe")
                                                           : QStringLiteral("unavailable"));
    map.insert(QStringLiteral("packets"), probe ? QVariant::fromValue(videoIngress_->packetCount()) : QVariant{});
    map.insert(QStringLiteral("bytes"), probe ? QVariant::fromValue(videoIngress_->byteCount()) : QVariant{});
    map.insert(QStringLiteral("silentMs"), probe ? QVariant::fromValue(videoIngress_->silentMs()) : QVariant{});
    map.insert(QStringLiteral("arrivalGaps"),
        probe ? QVariant::fromValue(videoIngress_->arrivalGapCount()) : QVariant{});
    map.insert(QStringLiteral("stalls"),
        probe ? QVariant::fromValue(videoIngress_->stallCount()) : QVariant{});
    map.insert(QStringLiteral("resyncs"),
        probe ? QVariant::fromValue(videoIngress_->resyncCount()) : QVariant{});
    map.insert(QStringLiteral("rejectedSources"),
        probe ? QVariant::fromValue(videoIngress_->rejectedSourceCount()) : QVariant{});
    map.insert(QStringLiteral("decodedFrames"), decodedFrames_);
    map.insert(QStringLiteral("frameAgeMs"), lastVideoFrameAtMs_ < 0
        ? qint64(-1) : videoClock_.elapsed() - lastVideoFrameAtMs_);
    map.insert(QStringLiteral("recoveryAfterStaleMs"), videoRecoveryMs_);
    map.insert(QStringLiteral("endToEndMeasured"), false);
    if (pairing_.targetId()) map.insert(QStringLiteral("pairing"), pairingDiagnostics());
    return map;
}

QString TerminalController::videoDiagnosticsLabel() const
{
    if (upstreamVideoActive_) {
        return QStringLiteral("HEVC 收包 %1 · 完整组帧 %2 · 解码 %3 / 呈现 %4 · 丢帧 %5 · 拒绝 %6"
                              " · 重同步 %7 · 解码 %8 ms · 待图像 %9 · 端到端未测")
            .arg(upstreamVideoStats_.value("packets").toULongLong())
            .arg(upstreamVideoStats_.value("assembledFrames").toULongLong())
            .arg(upstreamVideoStats_.value("decodedFrames").toULongLong()).arg(decodedFrames_)
            .arg(upstreamVideoStats_.value("droppedFrames").toULongLong())
            .arg(upstreamVideoStats_.value("rejectedPackets").toULongLong())
            .arg(upstreamVideoStats_.value("resyncs").toULongLong())
            .arg(upstreamVideoStats_.value("decodeMs").toDouble(), 0, 'f', 2)
            .arg(upstreamVideoStats_.value("imageQueueDepth").toInt());
    }
    const QVariantMap map = videoDiagnostics();
    if (map.value(QStringLiteral("packetCountersSource")) == QStringLiteral("unavailable")) {
        const auto age = map.value(QStringLiteral("frameAgeMs")).toLongLong();
        QString label = QStringLiteral("阶段=%1 · 观测有效帧 %2 · 帧龄 %3 · %4 · 出帧恢复 %5 · 端到端未测")
            .arg(map.value(QStringLiteral("stage")).toString()).arg(decodedFrames_)
            .arg(age < 0 ? QStringLiteral("--") : QStringLiteral("%1 ms").arg(age))
            .arg(map.value(QStringLiteral("stagePending")).toString())
            .arg(videoRecoveryMs_ < 0 ? QStringLiteral("--") : QStringLiteral("%1 ms").arg(videoRecoveryMs_));
        if (pairing_.targetId()) label += QStringLiteral(" · 对频：") + pairingDiagnostics().value("label").toString();
        return label;
    }
    const QString silent = map.value(QStringLiteral("silentMs")).toLongLong() < 0
        ? QStringLiteral("--")
        : QStringLiteral("%1 ms").arg(map.value(QStringLiteral("silentMs")).toLongLong());
    return QStringLiteral("UDP 探针 %1 包 / %2 KiB · 阶段=%3 · 缺口 %4 断流 %5 重同步 %6 来源拒绝 %7"
                          " · 静默 %8 · %9 · 出帧恢复 %10 · 端到端未测")
        .arg(map.value(QStringLiteral("packets")).toULongLong())
        .arg(map.value(QStringLiteral("bytes")).toULongLong() / 1024)
        .arg(map.value(QStringLiteral("stage")).toString())
        .arg(map.value(QStringLiteral("arrivalGaps")).toUInt())
        .arg(map.value(QStringLiteral("stalls")).toUInt())
        .arg(map.value(QStringLiteral("resyncs")).toUInt())
        .arg(map.value(QStringLiteral("rejectedSources")).toUInt())
        .arg(silent)
        .arg(map.value(QStringLiteral("stagePending")).toString())
        .arg(videoRecoveryMs_ < 0 ? QStringLiteral("--") : QStringLiteral("%1 ms").arg(videoRecoveryMs_));
}

QVariantMap TerminalController::hudSnapshot() const
{
    return hudSnapshot_;
}

QVariantMap TerminalController::mapSnapshot() const
{
    if (historicalLocal_ || upstreamData_) return {}; // World XYZ is not calibrated radar/map data.
    // 本机演练策略：新鲜雷达全量坐标优先；超时回退新鲜裁判坐标。
    // 不合并旧帧，不参与控制许可；正式来源优先级/处罚须等待官方协议。
    if (!radarSnapshot_.isEmpty()) {
        const QString team = state_.referee.fresh ? hudSnapshot_.value("ownTeam").toString()
                                                 : ownTeamPreference();
        if (team != QLatin1String("red") && team != QLatin1String("blue")) return {};
        return {{"friendlyPositions", radarSnapshot_.value(team)},
                {"enemyPositions", radarSnapshot_.value(team == QLatin1String("red") ? "blue" : "red")},
                {"source", "simulation-radar"}};
    }
    if (!state_.referee.fresh || hudSnapshot_.isEmpty()) return {};
    return {{"friendlyPositions", hudSnapshot_.value("friendlyPositions")},
            {"enemyPositions", hudSnapshot_.value("enemyPositions")}, {"source", "simulation-referee"}};
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

bool TerminalController::configureRm2026(const QString& host, quint16 port, unsigned robotId, bool testSource)
{
#ifdef RM27_UPSTREAM_BACKEND
    const QHostAddress address(host);
    const unsigned role = robotId > 100 ? robotId - 100 : robotId;
    const bool validId = role == 1 || role == 2 || role == 3 || role == 4 || role == 6 || role == 7;
    if (!upstreamBackend_ || !validId || !port
        || address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull()
        || address.toString() != host || (testSource && host != QStringLiteral("127.0.0.1"))) {
        setAlert(QStringLiteral("RM2026 需要明确 IPv4、端口和有效机器人 ID；协议测试仅限本机"), true);
        return false;
    }
    const bool unchanged = requestedRobotId_ == robotId && upstreamHost_ == host
        && upstreamPort_ == port && protocolTestSource_ == testSource;
    protocolTestSource_ = testSource;
    offlineDrill_ = false;
    localPlayback_ = false;
    requestedRobotId_ = robotId;
    upstreamHost_ = host;
    upstreamPort_ = port;
    connection_->setMqttHost(host);
    connection_->setMqttPort(port);
    connection_->setUseMqtt(true);
    connection_->enableRm2026Profile(testSource);
    connection_->setOwnTeam(robotId > 100 ? QStringLiteral("blue") : QStringLiteral("red"));
    const QMap<unsigned, QString> roles{{1,"hero"}, {2,"engineer"}, {3,"infantry3"},
        {4,"infantry4"}, {6,"aerial"}, {7,"sentry"}};
    connection_->setOwnRobot(roles.value(role));
    if (!unchanged) {
        if (pairing_.targetId()) cancelPairing();
        stopUpstreamVideo();
        applyHistoricalSnapshot({}, false);
        videoFrameStaleTimer_->stop();
        state_.video = {false, false};
        decodedFrames_ = 0;
        videoSource_ = rm27::VideoSource::None;
        emit frameCountChanged();
        upstreamBackend_->start(host, port, robotId);
    }
    emit connectionProfileApplied();
    return true;
#else
    Q_UNUSED(host); Q_UNUSED(port); Q_UNUSED(robotId); Q_UNUSED(testSource);
    return false;
#endif
}

bool TerminalController::configureRm2026Video(const QHostAddress& bind, quint16 port,
    const QHostAddress& source, unsigned base, bool testSource)
{
#ifdef RM27_UPSTREAM_VIDEO
    if ((!upstreamData_ && !historicalLocal_) || offlineDrill_ || localPlayback_ || base > 1
        || (historicalLocal_ && !testSource)
        || (testSource && (bind != QHostAddress::LocalHost || source != QHostAddress::LocalHost))) return false;
    if (pairing_.targetId()) cancelPairing();
    stopUpstreamVideo();
    upstreamVideo_ = std::make_unique<rm27::video::UpstreamVideo>();
    if (!upstreamVideo_->start(bind, port, source, base)) {
        upstreamVideo_.reset();
        videoStatusLabel_ = QStringLiteral("RM2026 HEVC 监听/解码初始化失败");
        emit stateChanged(); return false;
    }
    upstreamVideoActive_ = true;
    upstreamVideoTest_ = testSource;
    if (upstreamHost_.isEmpty()) protocolTestSource_ = testSource;
    decodedFrames_ = 0; upstreamVideoSerial_ = 0;
    upstreamVideoReportedId_ = 0;
    lastVideoFrameAtMs_ = videoStaleAtMs_ = videoRecoveryMs_ = -1;
    simulationVideoActive_ = false;
    videoSource_ = rm27::VideoSource::Rm2026Hevc;
    videoStatusLabel_ = QStringLiteral("RM2026 HEVC 等待首帧 · 未证明硬件配对");
    state_.video = {false, false}; state_.controlsArmed = false;
    videoFrameStaleTimer_->start();
    emit frameCountChanged(); emit stateChanged(); return true;
#else
    Q_UNUSED(bind); Q_UNUSED(port); Q_UNUSED(source); Q_UNUSED(base); Q_UNUSED(testSource);
    return false;
#endif
}

void TerminalController::stopUpstreamVideo()
{
#ifdef RM27_UPSTREAM_VIDEO
    if (!upstreamVideoActive_ && !upstreamVideo_) return;
    upstreamVideo_.reset();
#else
    if (!upstreamVideoActive_) return;
#endif
    upstreamVideoActive_ = false; upstreamVideoStats_.clear(); upstreamVideoSerial_ = 0;
    upstreamVideoReportedId_ = 0;
    videoFrameStaleTimer_->stop(); state_.video = {false, false};
    if (pairingVideoSink_) pairingVideoSink_->setVideoFrame({});
    videoSource_ = rm27::VideoSource::None;
    emit stateChanged();
}

void TerminalController::pollUpstreamVideo()
{
#ifdef RM27_UPSTREAM_VIDEO
    if (!upstreamVideo_ || !upstreamVideoActive_) return;
    const auto view = upstreamVideo_->view();
    upstreamVideoStats_ = view.stats;
    if (view.serial <= upstreamVideoSerial_ || view.image.isNull() || !pairingVideoSink_) return;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (view.decodedAtMs < 0 || now - view.decodedAtMs >= 700) return;
    upstreamVideoSerial_ = view.serial;
    upstreamVideoStats_.insert("decodeToSubmitMs", now - view.decodedAtMs);
    pairingVideoSink_->setVideoFrame(QVideoFrame(view.image));
#endif
}

QString TerminalController::refereeDiagnosticsLabel() const
{
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_ && !localPlayback_ && !offlineDrill_) {
        const auto view = upstreamBackend_->view();
        const auto &status = view.status;
        const auto reports = status.value("reports").toMap();
        const auto identity = reports.value("RobotStaticStatus").toMap().value("fields").toMap().value("robot_id");
        const QString requested = status.value("client_id").toString();
        QString text = QStringLiteral("%1 · %2\n请求 ID %3 · 报告 ID %4（仅新鲜报告，非授权）\n全部订阅确认 %5 · 新鲜主题 %6/24 · 接受 %7 / 拒绝 %8")
            .arg(protocolTestSource_ ? QStringLiteral("本机协议测试") : QStringLiteral("RM2026 MQTT"),
                 view.subscribed ? QStringLiteral("订阅已建立") : status.value("mqtt_connected").toBool()
                 ? QStringLiteral("连接已建立，等待全部订阅确认") : requested.isEmpty()
                 ? QStringLiteral("等待连接配置") : QStringLiteral("未连接 / 重试中"),
                 requested.isEmpty() ? QStringLiteral("--") : requested,
                 identity.isValid() ? identity.toString() : QStringLiteral("--"))
            .arg(view.subscribed ? QStringLiteral("24/24") : QStringLiteral("未完成"))
            .arg(reports.size()).arg(status.value("accepted").toULongLong()).arg(status.value("rejected").toULongLong());
        const auto missing = status.value("missing_or_stale_topics").toStringList();
        if (!missing.isEmpty()) text += QStringLiteral("\n未收到或已过期：") + missing.join(QStringLiteral("、"));
        text += QStringLiteral("\n未订阅：RadarInfoToClient（外层定义待核验）");
        const auto error = status.value("error").toString();
        if (!error.isEmpty()) text += QStringLiteral("\n最近错误：") + error.left(256);
        return text;
    }
#endif
    return localPlayback_ ? QStringLiteral("本地回放 · 裁判连接已停止")
        : offlineDrill_ ? QStringLiteral("离线演练 · 裁判连接已停止")
        : QStringLiteral("本机模拟 / 历史参考 · 非正式裁判链路");
}

QString TerminalController::commandDeliveryLabel() const
{
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_ && !localPlayback_ && !offlineDrill_) {
        const auto commands = upstreamBackend_->view().status.value("commands").toMap();
        const auto status = commands.value("event_status").toString();
        QString event = QStringLiteral("暂无请求");
        if (status == "awaiting_worker") event = QStringLiteral("等待工作线程发送");
        else if (status == "awaiting_puback") event = QStringLiteral("已提交传输，等待 MQTT 确认");
        else if (status == "broker_acknowledged") event = QStringLiteral("MQTT 已确认收到，执行未确认");
        else if (status == "delivery_unknown") event = QStringLiteral("送达未知，不自动重发");
        else if (status == "event_expired") event = QStringLiteral("等待超时，未发送");
        else if (status == "event_cancelled") event = QStringLiteral("已取消，未发送");
        else if (!status.isEmpty()) event = QStringLiteral("未发送：") + status.left(128);
        return QStringLiteral("最近入队赛事请求：%1\n会话传输汇总（含键鼠）：已提交 %2 · MQTT 确认 %3 · 送达未知 %4 · 在途 %5\n传输确认不代表赛事接受或机器人执行。")
            .arg(event).arg(commands.value("admitted").toULongLong())
            .arg(commands.value("broker_acknowledged").toULongLong())
            .arg(commands.value("delivery_unknown").toULongLong())
            .arg(commands.value("in_flight").toUInt());
    }
#endif
    return QStringLiteral("当前来源不提供赛事发送反馈");
}

QVariantMap TerminalController::refereeDiagnostics() const
{
    QVariantMap result;
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_) result = upstreamBackend_->view().status;
#endif
    result.insert("test_source", protocolTestSource_);
    result.insert("gui_stalls", guiStallCount());
    if (upstreamVideoActive_) {
        auto video = videoDiagnostics();
        video.insert("transport", upstreamVideoStats_);
        video.insert("fresh", videoConnected());
        video.insert("stale", videoStale());
        video.insert("test_source", upstreamVideoTest_);
        result.insert("video", video);
    }
    result.insert("hud", QVariantMap{{"data", hudSnapshot()}, {"hp", hpLabel()},
        {"buffer_energy", bufferEnergyLabel()}, {"heat", barrelHeatLabel()},
        {"power", chassisPowerLabel()}, {"physical_ammo", ammoLabel()},
        {"fresh", refereeFresh()}, {"armed", controlsArmed()}, {"map", mapSnapshot()},
        {"messages", eventMessages()}, {"source", linkBannerText()}, {"alert", alertText()},
        {"referee_diagnostics", refereeDiagnosticsLabel()}, {"command_delivery", commandDeliveryLabel()}});
    return result;
}

void TerminalController::applyHistoricalSnapshot(
    const rm27::historical::rm2026::Snapshot& snapshot, const bool subscribed)
{
    if (!historicalLocal_ && !upstreamData_) return;
    const auto now = std::chrono::steady_clock::now();
    const bool identityFresh = subscribed && snapshot.selfConfig
        && now >= snapshot.selfConfig->at
        && now - snapshot.selfConfig->at < std::chrono::milliseconds(1500);
    if (pairing_.targetId()) pairing_.reportIdentity(pairing_.token(),
        identityFresh ? snapshot.selfConfig->value.robotId : std::nullopt, videoClock_.elapsed());
    if (upstreamVideoActive_ && identityFresh && snapshot.selfConfig->value.robotId) {
        const auto reportedId = *snapshot.selfConfig->value.robotId;
        if ((upstreamVideoReportedId_ && upstreamVideoReportedId_ != reportedId)
            || (requestedRobotId_ && requestedRobotId_ != reportedId)) {
            stopUpstreamVideo();
            videoStatusLabel_ = QStringLiteral("报告身份改变/不匹配 · 已停止旧 HEVC 会话");
        } else upstreamVideoReportedId_ = reportedId;
    }
    using namespace rm27::historical::rm2026;
    state_.mode = localPlayback_ ? TerminalMode::Replay : upstreamData_ && !protocolTestSource_
        ? TerminalMode::HeavyOperator : TerminalMode::Simulation;
    state_.controlsArmed = false;
    state_.controller = {false, false};
    state_.referee = {subscribed, false};
    mqttConnected_ = subscribed;
    hudSnapshot_.clear();
    hpLabel_ = QStringLiteral("-- / --");
    chassisPowerLabel_ = QStringLiteral("--");
    bufferEnergyLabel_ = QStringLiteral("--");
    ammoLabel_ = QStringLiteral("--");
    barrelHeatLabel_ = QStringLiteral("--");
    historicalPhaseLabel_ = QStringLiteral("未知");
    QString gameSummary = QStringLiteral("比赛数据未知 / 已过期");
    lastRefereeSampleAtMs_ = -1;
    eventMessages_.clear();
    const auto markFresh = [this](const auto& block) {
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - block.at).count();
        lastRefereeSampleAtMs_ = std::max(lastRefereeSampleAtMs_,
            QDateTime::currentMSecsSinceEpoch() - age);
        state_.referee.fresh = true;
    };
    if (subscribed) {
        constexpr std::array eventNames{"未知事件", "击杀", "前哨摧毁", "大能量机关结果", "能量机关激活",
            "己方英雄狙击伤害", "对方英雄狙击伤害", "对方空中支援", "对方空中支援被反制",
            "飞镖命中", "对方飞镖闸门开启", "基地受攻击", "对方前哨停转", "对方基地护甲展开",
            "对方装配请求导致退出缓冲", "装配结果"};
        // Replace the bounded history, never append a polled latest sample repeatedly.
        const auto first = snapshot.recentEvents.size() > 24 ? snapshot.recentEvents.size() - 24 : 0;
        for (size_t i = first; i < snapshot.recentEvents.size(); ++i) {
            const auto& entry = snapshot.recentEvents[i];
            const auto& event = entry.value;
            const int kind = static_cast<int>(event.kind);
            QString text = (upstreamData_ && !protocolTestSource_ ? QStringLiteral("赛事报告 · %1 [%2]")
                                                                : QStringLiteral("历史测试 · %1 [%2]"))
                .arg(QString::fromUtf8(eventNames[kind >= 0 && kind < int(eventNames.size()) ? kind : 0]))
                .arg(event.id ? QString::number(*event.id) : QStringLiteral("未知编号"));
            if (event.param) {
                const auto parameter = QString::fromUtf8(event.param->data(), qsizetype(event.param->size()));
                text += QStringLiteral(" · 参数：") + parameter.left(512);
                if (parameter.size() > 512) text += QStringLiteral("…");
            }
            eventMessages_.append(QVariantMap{{"id", QString::number(entry.sequence)},
                {"category", "system"}, {"text", text}});
        }
        if (snapshot.selfConfig) {
            const auto& config = snapshot.selfConfig->value;
            markFresh(*snapshot.selfConfig);
            hudSnapshot_.insert("ownTeam", config.team == Team::Red ? "red"
                : config.team == Team::Blue ? "blue" : "unknown");
            // Existing photo order: sentry, aerial, infantry3, infantry4, engineer, hero.
            constexpr std::array<unsigned, 6> roles{7, 6, 3, 4, 2, 1};
            int index = -1;
            if (config.robotId) {
                const unsigned role = *config.robotId > 100 ? *config.robotId - 100 : *config.robotId;
                for (size_t i = 0; i < roles.size(); ++i)
                    if (roles[i] == role) index = static_cast<int>(i);
            }
            hudSnapshot_.insert("selfIndex", index);
            if (config.level) hudSnapshot_.insert("level", *config.level);
        }
        if (snapshot.selfDynamic) {
            const auto& dynamic = snapshot.selfDynamic->value;
            markFresh(*snapshot.selfDynamic);
            if (dynamic.health) {
                const auto maximum = snapshot.selfConfig ? snapshot.selfConfig->value.maxHealth
                                                         : std::optional<std::uint32_t>{};
                hpLabel_ = QString::number(*dynamic.health) + QStringLiteral(" / ")
                    + (maximum ? QString::number(*maximum) : QStringLiteral("--"));
            }
            if (dynamic.heat) barrelHeatLabel_ = QString::number(*dynamic.heat);
            if (dynamic.bufferEnergy)
                bufferEnergyLabel_ = QString::number(*dynamic.bufferEnergy) + QStringLiteral(" J");
            if (dynamic.permittedShots) hudSnapshot_.insert("allowedShots", *dynamic.permittedShots);
            // No physical ammo, gimbal angle, action flags or inferred video freshness.
        }
        if (snapshot.units) {
            markFresh(*snapshot.units);
            QVariantMap structures;
            const auto side = [&](const SideUnits& units, const QString& prefix) {
                QVariantList health(6, QVariant{});
                for (const auto& robot : units.robots) {
                    const int index = robot.roleId == 7 ? 0 : robot.roleId == 3 ? 2
                        : robot.roleId == 4 ? 3 : robot.roleId == 2 ? 4 : robot.roleId == 1 ? 5 : -1;
                    if (index >= 0 && robot.health) health[index] = *robot.health;
                }
                hudSnapshot_.insert(prefix + "Hp", health);
                QVariantMap base;
                if (units.base.health) base.insert("hp", *units.base.health);
                // The historical report has no base maximum; never invent a fill ratio.
                structures.insert(prefix + "_base", base);
                structures.insert(prefix + "_outpost", QVariantMap{{"life",
                    units.outpost.life == OutpostLife::Alive ? "alive"
                    : units.outpost.life == OutpostLife::Destroyed ? "destroyed" : "unknown"}});
            };
            side(snapshot.units->value.ally, "friendly");
            side(snapshot.units->value.enemy, "enemy");
            hudSnapshot_.insert("structures", structures);
        }
        if (snapshot.economy) {
            markFresh(*snapshot.economy);
            if (snapshot.economy->value.remaining)
                hudSnapshot_.insert("friendlyCoins", *snapshot.economy->value.remaining);
        }
        if (snapshot.game) {
            markFresh(*snapshot.game);
            const auto& game = snapshot.game->value;
            const auto number = [](const auto& value) {
                return value ? QString::number(*value) : QStringLiteral("--");
            };
            gameSummary = QStringLiteral("局 %1/%2 · 红 %3:%4 蓝 · 剩余 %5 s · 已过 %6 s · %7")
                .arg(number(game.currentRound), number(game.totalRounds),
                     number(game.redScore), number(game.blueScore),
                     number(game.countdownSec), number(game.elapsedSec),
                     game.paused ? (*game.paused ? QStringLiteral("已暂停") : QStringLiteral("未暂停"))
                                 : QStringLiteral("暂停未知"));
            switch (game.phase) {
            case Phase::NotStarted: historicalPhaseLabel_ = QStringLiteral("未开始"); break;
            case Phase::Preparation: historicalPhaseLabel_ = QStringLiteral("准备阶段"); break;
            case Phase::SelfCheck: historicalPhaseLabel_ = QStringLiteral("自检阶段"); break;
            case Phase::Countdown: historicalPhaseLabel_ = QStringLiteral("倒计时"); break;
            case Phase::Playing: historicalPhaseLabel_ = QStringLiteral("比赛中"); break;
            case Phase::Settlement: historicalPhaseLabel_ = QStringLiteral("比赛结束"); break;
            case Phase::Unknown: break;
            }
        }
    }
    refereeGrade_ = rm27::gradeForAge(lastRefereeSampleAtMs_ < 0 ? -1
        : QDateTime::currentMSecsSinceEpoch() - lastRefereeSampleAtMs_);
    setAlert((upstreamData_ && !protocolTestSource_
        ? QStringLiteral("RM2026 裁判数据 · %1 · %2 · 控制权限待核验")
        : QStringLiteral("RM2026 本机合成测试 · %1 · %2 · 禁止下发")).arg(
        !subscribed ? QStringLiteral("链路未连接") : state_.referee.fresh
        ? QStringLiteral("只读数据新鲜") : QStringLiteral("无数据 / 已过期"), gameSummary), !state_.referee.fresh);
}

void TerminalController::applySimulationSample(const rm27::sim::RefereeSample& sample)
{
    if (upstreamData_) return;
    lastRefereeSampleAtMs_ = QDateTime::currentMSecsSinceEpoch();
    refereeGrade_ = rm27::FreshnessGrade::Fresh;
    const bool firstSample = !state_.referee.fresh;
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
        if (hud.structures) {
            QVariantMap structures;
            constexpr std::array names{"friendly_base", "enemy_base", "friendly_outpost", "enemy_outpost"};
            for (size_t index = 0; index < names.size(); ++index) {
                const auto& item = hud.structures->at(index);
                structures.insert(QLatin1String(names[index]), QVariantMap{
                    {QStringLiteral("hp"), item.hp}, {QStringLiteral("maxHp"), item.maxHp}});
            }
            hudSnapshot_.insert(QStringLiteral("structures"), structures);
        }
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
    if (upstreamData_) {
        setAlert(QStringLiteral("RM2026 指令链路已接入 · 操作权限与业务门待核验"), true);
        return;
    }
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

void TerminalController::observeInputWindow(QObject *window)
{
    if (!operatorInput_) {
        operatorInput_ = new rm27::app::OperatorInput(this);
        connect(operatorInput_, &rm27::app::OperatorInput::frameReady, this, [this] {
#ifdef RM27_UPSTREAM_BACKEND
            if (upstreamBackend_) upstreamBackend_->updateInput(operatorInput_->enabled()
                ? std::optional(operatorInput_->frame()) : std::nullopt, commandContext());
#endif
        });
        connect(operatorInput_, &rm27::app::OperatorInput::captureLost, this, [this] {
            state_.controlsArmed = false;
            emit stateChanged();
        });
        connect(this, &TerminalController::stateChanged, this, &TerminalController::syncOperatorInput);
    }
    operatorInput_->observe(qobject_cast<QWindow *>(window));
    syncOperatorInput();
}

void TerminalController::syncOperatorInput()
{
    if (!operatorInput_) return;
    operatorInput_->setMouseSensitivity(connection_->mouseSensitivity());
    bool allowed = false;
#ifdef RM27_UPSTREAM_BACKEND
    const auto context = commandContext();
    allowed = upstreamBackend_ && rm27::protocol::rm2026::commandBlock(
        rm27::historical::rm2026::KeyboardMouseCommand{}, context).isEmpty();
    if (!allowed && upstreamBackend_) upstreamBackend_->updateInput(std::nullopt);
#endif
    operatorInput_->setEnabled(allowed);
}

#ifdef RM27_UPSTREAM_BACKEND
rm27::historical::rm2026::CommandContext TerminalController::commandContext() const
{
    rm27::historical::rm2026::CommandContext context;
    context.terminal = state_;
    if (requestedRobotId_) context.requestedId = requestedRobotId_;
    if (upstreamBackend_) {
        const auto view = upstreamBackend_->view();
        if (view.subscribed && view.snapshot.selfConfig) {
            const auto &identity = *view.snapshot.selfConfig;
            const auto now = std::chrono::steady_clock::now();
            context.reportedId = identity.value.robotId;
            context.identityFresh = now >= identity.at
                && now - identity.at < std::chrono::milliseconds(1500);
        }
    }
    // Control freshness needs both actual identity and GameStatus, not any HUD topic.
    context.terminal.phase = MatchPhase::Boot;
    context.terminal.referee.fresh = false;
    if (upstreamBackend_) {
        const auto view = upstreamBackend_->view();
        const auto now = std::chrono::steady_clock::now();
        if (view.subscribed && context.identityFresh && view.snapshot.game
            && now >= view.snapshot.game->at
            && now - view.snapshot.game->at < std::chrono::milliseconds(1500)
            && view.snapshot.game->value.paused == std::optional<bool>(false)) {
            constexpr std::array phases{MatchPhase::Boot, MatchPhase::Preparation,
                MatchPhase::SelfCheck, MatchPhase::Countdown, MatchPhase::Match, MatchPhase::Ended};
            const int phase = int(view.snapshot.game->value.phase);
            if (phase >= 0 && phase < int(phases.size())) {
                context.terminal.phase = phases[phase];
                context.terminal.referee.fresh = true;
            }
        }
    }
    // Source, role, penalty and operation authority are not inferred from MQTT/video.
    // businessPolicyReady stays false until the corresponding business gate is verified.
    return context;
}

rm27::protocol::rm2026::CommandSendResult TerminalController::submitRm2026Command(
    const rm27::historical::rm2026::Command &command)
{
    if (!upstreamBackend_) return {rm27::protocol::rm2026::CommandSendStatus::Disconnected,
        QStringLiteral("upstream_backend_unavailable")};
    return upstreamBackend_->submitCommand(command, commandContext());
}
#endif

void TerminalController::inspectMotionGate()
{
    showDecision(rm27::evaluateCommand(state_, CommandKind::RobotMotion));
}

int TerminalController::officialActionRobotId() const
{
#ifdef RM27_UPSTREAM_BACKEND
    const auto context = commandContext();
    if (context.identityFresh && context.reportedId && context.reportedId == context.requestedId)
        return int(*context.reportedId);
#endif
    return 0;
}

bool TerminalController::requestOfficialCommand(
    const rm27::historical::rm2026::Command& command, const int expectedRobotId)
{
    bool queued = false;
    QString detail;
#ifdef RM27_UPSTREAM_BACKEND
    const auto encoded = rm27::protocol::rm2026::encodeCommand(command);
    if (!encoded.error.isEmpty()) detail = QStringLiteral("参数无效");
    else if (expectedRobotId <= 0 || expectedRobotId != officialActionRobotId())
        detail = QStringLiteral("报告身份未知、过期或已变化，请重新选择");
    else {
        const auto context = commandContext();
        const auto result = submitRm2026Command(command);
        queued = result.status == rm27::protocol::rm2026::CommandSendStatus::Queued;
        if (queued) detail = QStringLiteral("请求已排队，等待传输确认；尚未确认执行");
        else if (!context.businessPolicyReady) detail = QStringLiteral("操作权限尚未核验");
        else {
            const auto gate = rm27::evaluateCommand(context.terminal, CommandKind::EventOperation);
            detail = !gate.allowed ? blockReasonText(gate.block)
                : QStringLiteral("资格、费用或链路条件未满足");
        }
    }
#else
    detail = QStringLiteral("当前构建不支持 RM2026 赛事操作");
#endif
    lastInputDecision_ = (queued ? QStringLiteral("赛事操作 · ") : QStringLiteral("赛事操作 · 未发送：")) + detail;
    setAlert(lastInputDecision_, !queued);
    return queued;
}

bool TerminalController::requestPerformance(int shooter, int chassis, int sentryControl, int expectedRobotId)
{
    const auto optionalCode = [](int code) -> std::optional<std::uint32_t> {
        return code == 0 ? std::nullopt : std::optional<std::uint32_t>(code);
    };
    // UI uses -1 for explicitly unchanged sentry control; wire 0 remains present.
    return requestOfficialCommand(rm27::historical::rm2026::PerformanceCommand{
        optionalCode(shooter), optionalCode(chassis), sentryControl == -1 ? std::nullopt
            : std::optional<std::uint32_t>(sentryControl)}, expectedRobotId);
}
bool TerminalController::requestDeployment(int mode, int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::DeployCommand{std::uint32_t(mode)}, expectedRobotId);
}
bool TerminalController::requestAssembly(int operation, int difficulty, int expectedRobotId)
{
    if (difficulty < 0 || difficulty > 4) {
        lastInputDecision_ = QStringLiteral("赛事操作 · 未发送：装配难度须为0至4");
        setAlert(lastInputDecision_, true);
        return false;
    }
    return requestOfficialCommand(rm27::historical::rm2026::AssemblyCommand{
        std::uint32_t(operation), std::uint32_t(difficulty)}, expectedRobotId);
}
bool TerminalController::requestCommon(int type, int parameter, int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::CommonCommand{
        std::uint32_t(type), std::uint32_t(parameter)}, expectedRobotId);
}
bool TerminalController::requestRune(int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::RuneCommand{}, expectedRobotId);
}
bool TerminalController::requestDart(int target, bool open, bool confirm, int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::DartCommand{
        std::uint32_t(target), open, confirm}, expectedRobotId);
}
bool TerminalController::requestSentry(int commandId, int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::SentryCommand{std::uint32_t(commandId)}, expectedRobotId);
}
bool TerminalController::requestAirSupport(int commandId, int expectedRobotId)
{
    return requestOfficialCommand(rm27::historical::rm2026::AirSupportCommand{std::uint32_t(commandId)}, expectedRobotId);
}

void TerminalController::enterLocalPlayback()
{
    stopUpstreamVideo();
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_) {
        upstreamBackend_->stop();
        upstreamHost_.clear();
        applyHistoricalSnapshot({}, false);
    }
#endif
    if (pairing_.targetId()) cancelPairing();
    lastVideoFrameAtMs_ = videoStaleAtMs_ = videoRecoveryMs_ = -1;
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
    if (pairing_.targetId()) cancelPairing();
    stopUpstreamVideo();
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_) {
        upstreamBackend_->stop();
        upstreamHost_.clear();
        applyHistoricalSnapshot({}, false);
        offlineDrill_ = true;
    }
#endif
    lastVideoFrameAtMs_ = videoStaleAtMs_ = videoRecoveryMs_ = -1;
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
    videoStatusLabel_ = QStringLiteral("等待本机 UDP 测试图传 · 127.0.0.1:%1")
                           .arg(connection_->simulationVideoPort());
    // 首帧尚未到达也要超时；之后由每个有效帧重新开始计时。
    videoFrameStaleTimer_->start();
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
    if (upstreamData_ && localPlayback_) return QStringLiteral("本地视频回放 · 裁判连接已停止 · 控制锁定");
    if (upstreamData_ && offlineDrill_) return QStringLiteral("离线演练 · 裁判连接已停止 · 控制锁定");
    if (upstreamData_) return protocolTestSource_
        ? QStringLiteral("RM2026 本机协议测试 · 非现场数据 · 控制锁定")
        : QStringLiteral("RM2026 裁判数据 · VT03/VT13 · 控制锁定");
    if (historicalLocal_)
        return QStringLiteral("RM2026 本机合成测试 · 非真实裁判 · 指令永久禁用");
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
    if (upstreamData_) {
        unsigned robotId = requestedRobotId_;
        const QMap<QString, unsigned> roles{{"hero", 1}, {"engineer", 2}, {"infantry3", 3},
                                           {"infantry4", 4}, {"aerial", 6}, {"sentry", 7}};
        if (!connection_->ownRobot().isEmpty()) {
            if (!roles.contains(connection_->ownRobot())
                || (connection_->ownTeam() != "red" && connection_->ownTeam() != "blue")) {
                setAlert(QStringLiteral("请选择红方或蓝方，以及本车机型"), true);
                return false;
            }
            robotId = roles.value(connection_->ownRobot()) + (connection_->ownTeam() == "blue" ? 100 : 0);
        }
        if (!connection_->entryAllowed()) {
            setAlert(connection_->validationMessage(), true);
            return false;
        }
        const bool testSource = connection_->mqttHost() == QStringLiteral("127.0.0.1");
        const bool configured = configureRm2026(connection_->mqttHost(), quint16(connection_->mqttPort()), robotId, testSource);
        if (configured && !testSource) connection_->save();
        return configured;
    }
    if (historicalLocal_) {
        setAlert(QStringLiteral("RM2026 本机只读测试 · 不切换链路或保存正式配置"), true);
        return false;
    }
    if (!connection_) {
        return false;
    }
    const rm27::ConnectionProfile profile = connection_->profile();
    const rm27::ProfileIssue issue = rm27::validateProfile(profile);
    if (issue != rm27::ProfileIssue::Ok) {
        setAlert(QString::fromUtf8(rm27::profileIssueText(issue)), true);
        return false;
    }
    if (!connection_->entryAllowed()) {
        setAlert(connection_->validationMessage(), true);
        return false;
    }

    // MQTT 接收器按端口重建：只订阅本机模拟主题，不发布、不连接外部 broker。
    if (profile.useMqtt && (!simMqttReceiver_ || mqttPortInUse_ != profile.mqttPort)) {
        delete simMqttReceiver_;
        mqttConnected_ = false;
        simMqttReceiver_ = new rm27::sim::MqttReceiver(this, profile.mqttPort);
        connect(simMqttReceiver_, &rm27::sim::MqttReceiver::payloadReceived,
                simReceiver_, &rm27::sim::RefereeReceiver::acceptPayload);
        connect(simMqttReceiver_, &rm27::sim::MqttReceiver::linkChanged, this, [this](bool connected) {
            mqttConnected_ = connected;
            emit stateChanged();
        });
        simMqttReceiver_->start();
        mqttPortInUse_ = profile.mqttPort;
    } else if (!profile.useMqtt && simMqttReceiver_) {
        delete simMqttReceiver_;
        simMqttReceiver_ = nullptr;
        mqttPortInUse_ = 0;
        mqttConnected_ = false;
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
    emit connectionProfileApplied();
    return true;
}

void TerminalController::enterOfflineDrill()
{
    if (pairing_.targetId()) cancelPairing();
    stopUpstreamVideo();
#ifdef RM27_UPSTREAM_BACKEND
    if (upstreamBackend_) {
        upstreamBackend_->stop();
        upstreamHost_.clear();
        applyHistoricalSnapshot({}, false);
        offlineDrill_ = true;
        state_.mode = TerminalMode::Simulation;
        connection_->recordOfflineEntry();
        setAlert(QStringLiteral("离线演练 · 裁判连接已停止 · 指令禁用"), false);
        emit connectionProfileApplied();
        return;
    }
#endif
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
    const bool clearedPairingVideo = pairingVideoSink_ != sink
        && pairing_.videoUnavailable(pairing_.token(), videoClock_.elapsed());
    pairingVideoSink_ = sink;
    bindPairingVideoSink();
    if (clearedPairingVideo) emit stateChanged();
    disconnect(videoFrameConnection_);
    videoFrameConnection_ = connect(sink, &QVideoSink::videoFrameChanged, this, [this, sink](const QVideoFrame& frame) {
        if (sink != pairingVideoSink_ || !frame.isValid()) return;
        ++decodedFrames_;
        lastVideoFrameAtMs_ = videoClock_.elapsed();
        if (decodedFrames_ == 1) qInfo("LOCAL_VIDEO_FIRST_FRAME");
        if (simulationVideoActive_ || upstreamVideoActive_) {
            if (videoStaleAtMs_ >= 0) {
                videoRecoveryMs_ = lastVideoFrameAtMs_ - videoStaleAtMs_;
                videoStaleAtMs_ = -1;
            }
            state_.video = {true, true};
            videoStatusLabel_ = (upstreamVideoActive_ ? (upstreamVideoTest_
                ? QStringLiteral("本机 HEVC 测试 · 已呈现 %1 帧") : QStringLiteral("RM2026 HEVC · 已呈现 %1 帧"))
                : QStringLiteral("本机 UDP 图传 · 已解码 %1 帧")).arg(decodedFrames_);
            videoFrameStaleTimer_->start();
            emit stateChanged();
        }
        emit frameCountChanged();
    });
}

bool TerminalController::beginPairing(const QString& region, int robotId, int timeoutMs)
{
    if (localPlayback_ || robotId < 0
        || (region != "global" && region != "japan")
        || !upstreamVideoActive_ || upstreamVideoTest_ || historicalLocal_ || protocolTestSource_
        || (requestedRobotId_ && requestedRobotId_ != unsigned(robotId))) return false;
    if (!pairing_.begin(region == "global" ? rm27::PairingRegion::Global : rm27::PairingRegion::Japan,
        static_cast<std::uint32_t>(robotId), videoClock_.elapsed(), timeoutMs)) return false;
    bindPairingVideoSink();
    emit stateChanged();
    return true;
}

void TerminalController::bindPairingVideoSink()
{
    disconnect(pairingVideoConnection_);
    if (!pairingVideoSink_ || !pairing_.targetId()) return;
    const auto session = pairing_.token();
    pairingVideoConnection_ = connect(pairingVideoSink_, &QVideoSink::videoFrameChanged, this,
        [this, session](const QVideoFrame& frame) {
            const bool changed = frame.isValid() && upstreamVideoActive_ && !upstreamVideoTest_
                ? pairing_.observeVideo(session, videoClock_.elapsed())
                : pairing_.videoUnavailable(session, videoClock_.elapsed());
            if (changed) emit stateChanged();
        });
}

bool TerminalController::confirmExternalPairing(qulonglong session)
{
    const bool accepted = pairing_.confirmExternal(session, videoClock_.elapsed());
    emit stateChanged();
    return accepted;
}

void TerminalController::cancelPairing()
{
    pairing_.cancel();
    disconnect(pairingVideoConnection_);
    emit stateChanged();
}

bool TerminalController::reportPairingIdentity(qulonglong session, std::optional<std::uint32_t> id)
{
    const bool accepted = pairing_.reportIdentity(session, id, videoClock_.elapsed());
    emit stateChanged();
    return accepted;
}

bool TerminalController::pairingDeviceDisconnected(qulonglong session)
{
    const bool accepted = pairing_.deviceDisconnected(session);
    if (accepted) stopUpstreamVideo();
    emit stateChanged();
    return accepted;
}

QVariantMap TerminalController::pairingDiagnostics() const
{
    constexpr std::array phases{"idle", "external_required", "video_required", "video_review",
        "identity_mismatch", "timed_out", "cancelled", "disconnected"};
    constexpr std::array labels{"未开始", "等待官方对频确认", "等待有效视频", "已确认对频且有画面（身份未认证）",
        "报告身份不匹配", "配对流程超时", "已取消", "设备断开"};
    const auto phase = static_cast<int>(pairing_.phase(videoClock_.elapsed()));
    const bool japan = pairing_.region() == rm27::PairingRegion::Japan;
    return {{"session", QString::number(pairing_.token())}, {"phase", phases[phase]}, {"label", QString::fromUtf8(labels[phase])},
        {"device", "vt13"}, {"region", japan ? "japan" : "global"},
        {"targetRobotId", pairing_.targetId() ? QVariant::fromValue(*pairing_.targetId()) : QVariant{}},
        {"reportedRobotId", pairing_.reportedId() ? QVariant::fromValue(*pairing_.reportedId()) : QVariant{}},
        {"identityStatus", !pairing_.reportedId() ? "unknown" : pairing_.reportedId() == pairing_.targetId() ? "reported_match" : "mismatch"},
        {"operatorConfirmed", pairing_.operatorConfirmed()}, {"confirmationSource", pairing_.operatorConfirmed() ? "operator" : "unknown"},
        {"videoFresh", pairing_.videoFresh(videoClock_.elapsed())}, {"hardwareVerified", false}, {"controlAllowed", false},
        {"instructions", japan
            ? QStringLiteral("主控设置机器人 ID；接收端连接电脑，在官方客户端选择对应 ID 登录。断电重启后重新连接确认。")
            : QStringLiteral("主控：模块设置→图传对频；接收端：长按电源键约2秒，松开，再长按约2秒。发送端绿灯闪烁且接收端两声后静音时确认。")}};
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
            {QStringLiteral("isCommand"), action.kind == CommandKind::RobotMotion || action.refereeCommand},
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
    if (action->refereeCommand) {
        // Adapted from MIT RM26_Client e291f2f MainWindow::tryHandleRespawnShortcut.
        // Copyright (c) 2025-2026 Fudan University EGA Team; third_party/rm26_client/LICENSE.
        // Events are handled here, never emitted as a presentation-only action.
        bool queued = false;
        QString detail;
#ifdef RM27_UPSTREAM_BACKEND
        const auto context = commandContext();
        const auto gate = rm27::evaluateCommand(context.terminal, CommandKind::EventOperation);
        const auto result = submitRm2026Command(rm27::historical::rm2026::CommonCommand{
            actionId == QStringLiteral("event.paidRespawn") ? 4u : 3u, 0});
        queued = result.status == rm27::protocol::rm2026::CommandSendStatus::Queued;
        if (queued) detail = QStringLiteral("请求已进入发送队列 · 等待确认");
        else if (!gate.allowed) detail = blockReasonText(gate.block);
        else if (!context.businessPolicyReady) detail = QStringLiteral("操作权限尚未核验");
        else if (result.reason == QStringLiteral("insufficient_gold")) detail = QStringLiteral("金币不足");
        else if (result.reason == QStringLiteral("event_slot_busy")) detail = QStringLiteral("已有赛事请求等待处理");
        else detail = QStringLiteral("复活资格、费用或链路条件未满足");
#else
        detail = QStringLiteral("当前构建不支持 RM2026 赛事操作");
#endif
        lastInputDecision_ = hint + (queued ? QStringLiteral(" · ") : QStringLiteral(" · 未发送：")) + detail;
        setAlert(lastInputDecision_, !queued);
        return queued;
    }
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
    // 再选本地文件是打开选择器，不是自动重启当前解码；取消不改变播放。
    const bool reselectLocalFile = target == rm27::VideoSource::LocalPlayback && decision.sameSource;
    if (reselectLocalFile) {
        sourceSwitchHint_ = QStringLiteral("本地回放 · 重新选择文件，取消则保持当前播放");
    }
    if (!decision.allowed && !reselectLocalFile) {
        if (!decision.sameSource && lastInputDecision_.isEmpty()) {
            lastInputDecision_ = sourceSwitchHint_;
        }
        emit stateChanged();
        return false;
    }

    // 真正的切源由表现层完成（本地回放要用户选文件），完成时经 enter* 记录来源与时刻。
    if (target == rm27::VideoSource::None) {
        if (pairing_.targetId()) cancelPairing();
        stopUpstreamVideo();
        videoSource_ = target;
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
