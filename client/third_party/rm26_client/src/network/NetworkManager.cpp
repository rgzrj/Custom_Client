// SPDX-License-Identifier: MIT
/**
 * @file NetworkManager.cpp
 * @brief 网络通信管理器实现文件
 * @details 本文件实现了 NetworkManager 类的所有成员函数。
 *          主要包括 UDP 套接字的初始化、数据收发、以及数据解析分发逻辑。
 *
 * @author Clear
 * @date 2025-11-29
 * @copyright Copyright (c) 2025 Fudan University EGA Team (复旦大学星云 EGA 机器人战队).
 */

// --- 头文件包含 ---
#include "NetworkManager.h"
#include "../core/CustomDataTypes.h"  // 自定义数据结构定义
#include "robomaster.pb.h" // Protobuf 生成的消息类
#include <QCoreApplication>
#include <QThread>
#include <QDebug>                    // Qt 调试输出
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkProxy>
#include <cstring>
#include <QDateTime>
#include <algorithm>

namespace RM {

#ifdef RM_HAS_MQTT
namespace {
bool verboseVideoLogEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("RM_VERBOSE_VIDEO_LOG");
  return enabled;
}

bool isHighRateVideoMessage(const QString &topic, const QByteArray &payload) {
  return topic == QLatin1String("CustomByteBlock") && payload.size() > 100;
}

template <typename RepeatedFieldT>
QJsonArray repeatedFieldToJsonArray(const RepeatedFieldT &field) {
  QJsonArray array;
  for (const auto value : field) {
    array.append(static_cast<int>(value));
  }
  return array;
}

// Local adaptation: the application's bounded runtime logger owns diagnostics.
void dumpMqttEvent(const QString &, const QString &, const QByteArray &,
                   const GameData *, const QString & = QString(),
                   const QJsonObject & = QJsonObject()) {}
} // namespace
#endif

// --- 构造与析构 ---

/**
 * @brief 构造函数实现
 * @details 执行以下初始化步骤：
 *          1. 调用父类构造函数
 *          2. 保存 GameData 指针
 *          3. 创建 QUdpSocket 实例
 *          4. 连接 readyRead 信号到 onReadyRead 槽
 *
 * @param gameData 比赛数据中心指针
 * @param parent   Qt 父对象
 */
NetworkManager::NetworkManager(GameData *gameData, QObject *parent)
    : QObject(parent), m_gameData(gameData) {
  // 创建 UDP 套接字，设置 this 为父对象以便自动内存管理
  m_udpSocket = new QUdpSocket(this);
  // 环境里可能存在 HTTP/SOCKS 代理；UDP bind 在代理模式下会失败。
  m_udpSocket->setProxy(QNetworkProxy::NoProxy);

  // 连接信号槽：当有数据可读时调用 onReadyRead
  connect(m_udpSocket, &QUdpSocket::readyRead, this,
          &NetworkManager::onReadyRead);
}

/**
 * @brief 析构函数实现
 * @details 确保在对象销毁前调用 stopListening 关闭套接字。
 *          QUdpSocket 由于设置了父对象，会在父对象销毁时自动删除。
 */
NetworkManager::~NetworkManager() { stopListening(); }

// --- 连接控制函数 ---

/**
 * @brief 启动 UDP 监听
 * @details 尝试将 UDP 套接字绑定到指定端口。
 *
 *          绑定过程：
 *          1. 调用 QUdpSocket::bind(QHostAddress::Any, port)
 *          2. QHostAddress::Any 表示监听所有可用网络接口
 *          3. 成功则输出调试信息，失败则发送错误信号
 *
 * @param port 要监听的端口号
 * @return true 绑定成功，false 绑定失败
 */
bool NetworkManager::startListening(quint16 port) {
  if (!m_udpSocket) {
    qCritical() << "NetworkManager: UDP socket is null";
    emit errorOccurred(QStringLiteral("UDP socket is null"));
    return false;
  }

  if (m_udpSocket->state() != QAbstractSocket::UnconnectedState ||
      m_udpSocket->localPort() != 0) {
    qInfo() << "NetworkManager: closing stale UDP socket before rebinding"
            << "state=" << m_udpSocket->state()
            << "localPort=" << m_udpSocket->localPort();
    m_udpSocket->close();
  }

  // 尝试绑定端口
  if (m_udpSocket->bind(QHostAddress::Any, port,
                        QUdpSocket::ShareAddress |
                            QUdpSocket::ReuseAddressHint)) {
    qDebug() << "NetworkManager: Listening on port" << port;
    return true;
  } else {
    // 绑定失败，输出错误日志并发送信号
    qCritical() << "NetworkManager: Failed to bind port" << port
                << "error=" << m_udpSocket->errorString();
    emit errorOccurred(m_udpSocket->errorString());
    return false;
  }
}

/**
 * @brief 停止 UDP 监听
 * @details 如果套接字当前处于已连接状态，则关闭它。
 *          检查状态避免重复关闭导致的警告。
 */
void NetworkManager::stopListening() {
  // 只有在套接字未处于未连接状态时才需要关闭
  if (m_udpSocket->state() != QAbstractSocket::UnconnectedState) {
    m_udpSocket->close();
    qDebug() << "NetworkManager: Stopped listening";
  }
}

/**
bool NetworkManager::isUdpListening() const {
  return m_udpSocket && m_udpSocket->state() == QAbstractSocket::BoundState;
}

quint16 NetworkManager::listeningPort() const {
  return m_udpSocket ? m_udpSocket->localPort() : 0;
}
*/
bool NetworkManager::isUdpListening() const {
  return m_udpSocket && m_udpSocket->state() == QAbstractSocket::BoundState;
}

quint16 NetworkManager::listeningPort() const {
  return m_udpSocket ? m_udpSocket->localPort() : 0;
}

// One typed uplink path; no UDP fallback or QML/raw protobuf publisher.
rm27::protocol::rm2026::CommandSendResult NetworkManager::sendCommand(
    const rm27::historical::rm2026::Command &command,
    const rm27::historical::rm2026::CommandContext &context) {
  using namespace rm27::protocol::rm2026;
  Q_ASSERT(QThread::currentThread() == thread());
  const auto block = commandBlock(command, context);
  if (!block.isEmpty()) return {CommandSendStatus::Blocked, block};
  const auto encoded = encodeCommand(command, context.mapOperator);
  if (!encoded.error.isEmpty()) return {CommandSendStatus::Invalid, encoded.error};
#ifdef RM_HAS_MQTT
  if (!m_topicsSubscribed || !isMqttConnected())
    return {CommandSendStatus::Disconnected, "mqtt_unsubscribed"};
  if (m_requestedClientId != QString::number(*context.requestedId)
      || (m_subscriptionCurrent && !m_subscriptionCurrent()))
    return {CommandSendStatus::Blocked, "configured_identity_or_session_changed"};
  // Re-read received data at send time, not just the caller's cached view.
  const auto snapshot = m_gameData->officialSnapshot();
  const auto businessBlock = rm27::historical::rm2026::businessCommandBlock(
      command, snapshot, context, std::chrono::steady_clock::now());
  if (*businessBlock) return {CommandSendStatus::Blocked, QString::fromLatin1(businessBlock)};
  if (m_mqttManager->commandsInFlight() >= 8)
    return {CommandSendStatus::InFlightLimit, "in_flight_limit"};
  if (!m_commandRate.acquire(command.index(), encoded.interval,
      rm27::historical::rm2026::CommandRateLimiter::Clock::now()))
    return {CommandSendStatus::RateLimited, "topic_rate_limit"};
  const auto result = m_mqttManager->publishCommand(encoded.topic, encoded.payload);
  if (result.status == MqttManager::PublishResult::Rejected)
    return {CommandSendStatus::TransportRejected, "paho_publish_rejected"};
  if (result.status == MqttManager::PublishResult::InFlightLimit)
    return {CommandSendStatus::InFlightLimit, "in_flight_limit"};
  if (const auto *input = std::get_if<rm27::historical::rm2026::KeyboardMouseCommand>(&command))
    m_controlActive = input->keys || input->left || input->right || input->middle;
  emit dataSent(encoded.payload);
  return {CommandSendStatus::Queued, "awaiting_puback", result.token};
#else
  return {CommandSendStatus::Disconnected, "mqtt_disabled"};
#endif
}

bool NetworkManager::keyboardInputAllowed(
    const rm27::historical::rm2026::CommandContext &context) const {
  Q_ASSERT(QThread::currentThread() == thread());
  const rm27::historical::rm2026::KeyboardMouseCommand input;
  if (!rm27::protocol::rm2026::commandBlock(input, context).isEmpty()) return false;
#ifdef RM_HAS_MQTT
  if (!m_topicsSubscribed || !isMqttConnected()
      || m_requestedClientId != QString::number(*context.requestedId)
      || (m_subscriptionCurrent && !m_subscriptionCurrent())) return false;
  return !*rm27::historical::rm2026::businessCommandBlock(
      input, m_gameData->officialSnapshot(), context, std::chrono::steady_clock::now());
#else
  return false;
#endif
}

int NetworkManager::commandsInFlight() const {
#ifdef RM_HAS_MQTT
  return m_mqttManager ? m_mqttManager->commandsInFlight() : 0;
#else
  return 0;
#endif
}
void NetworkManager::cancelCommands() {
#ifdef RM_HAS_MQTT
  if (m_controlActive || commandsInFlight()) m_mqttManager->disconnect();
#endif
  m_controlActive = false;
}

// --- 数据接收与处理 ---

/**
 * @brief UDP 数据就绪处理槽
 * @details 当 QUdpSocket 发出 readyRead 信号时调用。
 *          循环读取所有挂起的数据报直到队列为空。
 *
 *          处理流程：
 *          1. 检查是否有待处理的数据报
 *          2. 读取数据报到 QByteArray
 *          3. 同时获取发送方地址和端口
 *          4. 调用 processData 处理数据
 *          5. 发送 dataReceived 信号
 *          6. 继续循环直到队列为空
 */
void NetworkManager::onReadyRead() {
  // 循环处理所有挂起的数据报
  while (m_udpSocket->hasPendingDatagrams()) {
    // 准备接收缓冲区
    QByteArray datagram;
    datagram.resize(m_udpSocket->pendingDatagramSize());

    // 发送方信息
    QHostAddress sender;
    quint16 senderPort;

    // 读取数据报
    m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender,
                              &senderPort);
    ++m_totalUdpPacketsReceived;

            // 采样日志：记录接收时间戳与数据大小，便于链路延时分析
            qInfo() << "[RespawnDebug] NetworkManager: onReadyRead ts="
              << QDateTime::currentMSecsSinceEpoch() << "size=" << datagram.size();

    // 处理接收到的数据
    processData(datagram);

    // 发送信号通知外部
    emit dataReceived(datagram);
  }
}

/**
 * @brief 处理接收到的数据
 * @details 使用 Protocol 类解析数据包，并将解析后的消息
 *          传递给 GameData 进行状态更新。
 *
 *          处理流程：
 *          1. 创建 RoboMasterMessage 对象
 *          2. 调用 Protocol::parsePacket 解析数据
 *          3. 如果解析成功且 m_gameData 有效，调用其更新方法
 *          4. 解析失败则静默丢弃（不输出日志避免刷屏）
 *
 * @param data 原始字节数据
 *
 * @note 解析失败的数据包不会触发任何操作，这是正常现象
 *       （可能是其他应用的数据或损坏的数据包）
 */
void NetworkManager::processData(const QByteArray &data) {
  // 创建消息容器
  robomaster::RoboMasterMessage message;

  // 尝试解析数据包
  if (Protocol::parsePacket(data, message)) {
    // 解析成功，将数据传递给 GameData 处理
    if (m_gameData) {
            // 解析到有效消息时记录时间戳和 payload 类型，用于端到端延时对比
            qInfo() << "[RespawnDebug] NetworkManager: parsePacket OK ts=" << QDateTime::currentMSecsSinceEpoch()
              << "payload_case=" << static_cast<int>(message.payload_case());
      if (message.has_game_info()) {
        auto *gameInfo = message.mutable_game_info();
#ifdef RM_HAS_MQTT
        if (m_hasMqttGameStatus) {
          // MQTT GameStatus 存在时以 MQTT 为准，否则允许 UDP 兜底
          // 自己驱动阶段/计时变化。
          gameInfo->set_stage(m_lastMqttStage);
          gameInfo->set_time_remaining(m_lastMqttTimeRemaining);
          gameInfo->set_is_paused(m_lastMqttPaused);
        }
#endif
      }
      m_gameData->processProtocolData(message);
    }
  } else {
    // 解析失败时静默丢弃。
  }
}

// ============================================================================
// MQTT 相关实现
// ============================================================================

#ifdef RM_HAS_MQTT

void NetworkManager::startMqtt(const QString &brokerUri,
                               const QString &clientId) {
  m_requestedClientId = clientId;
  // 创建 MQTT 管理器
  if (!m_mqttManager) {
    m_mqttManager = new MqttManager(this);

    connect(m_mqttManager, &MqttManager::commandDelivery, this, &NetworkManager::commandDelivery);
    // 连接信号
    connect(m_mqttManager, &MqttManager::messageObserved, this,
            &NetworkManager::mqttMessageObserved);
    connect(m_mqttManager, &MqttManager::messageReceived, this,
            &NetworkManager::processMqttMessage);
    connect(
        m_mqttManager, &MqttManager::connectionStateChanged, this,
        [this](bool connected) {
          m_topicsSubscribed = false;
          m_controlActive = false;
          m_gameData->resetOfficialReports();
          emit subscriptionStateChanged(false);
          if (connected) {
            qDebug()
                << "NetworkManager: MQTT connected, subscribing to topics...";
            subscribeToTopics();
            // 启动 MQTT 消息日志会话

          } else {

          }
        });
    connect(m_mqttManager, &MqttManager::errorOccurred, this,
            &NetworkManager::errorOccurred);
    // 转发异步连接结果
    connect(m_mqttManager, &MqttManager::connectCompleted, this,
            [this](bool success, const QString &error) {
              emit mqttConnectCompleted(success, error);
            });
  }

  // 异步连接到 Broker（非阻塞，结果通过 mqttConnectCompleted 信号通知）
  m_mqttManager->connectToBrokerAsync(brokerUri, clientId);
}

void NetworkManager::stopMqtt() {
  if (m_mqttManager) {
    m_mqttManager->disconnect();
  }
}

bool NetworkManager::isMqttConnected() const {
  return m_mqttManager && m_mqttManager->isConnected();
}

void NetworkManager::subscribeToTopics() {
  if (!m_mqttManager)
    return;

  const QStringList topics = GameData::officialTopics();

  for (const QString &topic : topics) {
    if (m_subscriptionCurrent && !m_subscriptionCurrent()) {
      m_mqttManager->disconnect();
      return;
    }
    if (!m_mqttManager->subscribe(topic, 1)) {
      m_mqttManager->disconnect();
      return;
    }
  }
  if (m_subscriptionCurrent && !m_subscriptionCurrent()) {
    m_mqttManager->disconnect();
    return;
  }
  m_topicsSubscribed = true;
  m_mqttManager->setAcceptMessages(true);
  emit subscriptionStateChanged(true);
}

void NetworkManager::processMqttMessage(const QString &topic,
                                        const QByteArray &payload, qint64 receivedMonoMs) {
  if (!m_gameData)
    return;

  const bool detailedVideoTrace =
      !isHighRateVideoMessage(topic, payload) || verboseVideoLogEnabled();
  if (detailedVideoTrace) {
    qDebug() << "NetworkManager: Processing MQTT message from topic:" << topic;
    emit dataReceived(QStringLiteral("MQTT RX topic=%1 bytes=%2")
                          .arg(topic)
                          .arg(payload.size())
                          .toUtf8());
  }
  dumpMqttEvent(QStringLiteral("received"), topic, payload, m_gameData);

  if (!m_topicsSubscribed || !m_gameData->acceptOfficialReport(topic, payload, receivedMonoMs)) return;

  // 根据 Topic 解析对应的 Protobuf 消息
  if (topic == "GameStatus") {
    robomaster::GameStatus msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      const auto stage =
          static_cast<robomaster::GameStage>(msg.current_stage());
      const quint16 timeRemaining = static_cast<quint16>(msg.stage_countdown_sec());
      const bool isPaused = msg.is_paused();

      m_hasMqttGameStatus = true;
      m_lastMqttStage = stage;
      m_lastMqttTimeRemaining = timeRemaining;
      m_lastMqttPaused = isPaused;

      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("GameStatus current_stage=%1 countdown=%2 paused=%3")
              .arg(msg.current_stage())
              .arg(msg.stage_countdown_sec())
              .arg(msg.is_paused()),
          QJsonObject{{QStringLiteral("current_stage"),
                       static_cast<int>(msg.current_stage())},
                      {QStringLiteral("stage_countdown_sec"),
                       static_cast<int>(msg.stage_countdown_sec())},
                      {QStringLiteral("stage_elapsed_sec"),
                       static_cast<int>(msg.stage_elapsed_sec())},
                      {QStringLiteral("is_paused"), msg.is_paused()}});
      m_gameData->updateGameStatus(msg);
    }
  } else if (topic == "GlobalUnitStatus") {
      robomaster::GlobalUnitStatus msg;
      if (msg.ParseFromArray(payload.data(), payload.size())) {
        dumpMqttEvent(
            QStringLiteral("parsed"), topic, payload, m_gameData,
            QStringLiteral("GlobalUnitStatus robot_health=%1 robot_bullets=%2")
                .arg(QString::fromUtf8(
                    QJsonDocument(repeatedFieldToJsonArray(msg.robot_health()))
                        .toJson(QJsonDocument::Compact)))
                .arg(QString::fromUtf8(
                    QJsonDocument(repeatedFieldToJsonArray(msg.robot_bullets()))
                        .toJson(QJsonDocument::Compact))),
            QJsonObject{{QStringLiteral("robot_health"),
                         repeatedFieldToJsonArray(msg.robot_health())},
                        {QStringLiteral("robot_bullets"),
                         repeatedFieldToJsonArray(msg.robot_bullets())},
                        {QStringLiteral("robot_health_size"),
                         msg.robot_health_size()},
                        {QStringLiteral("robot_bullets_size"),
                         msg.robot_bullets_size()},
                        {QStringLiteral("base_health"),
                         static_cast<int>(msg.base_health())},
                        {QStringLiteral("enemy_base_health"),
                         static_cast<int>(msg.enemy_base_health())},
                        {QStringLiteral("outpost_health"),
                         static_cast<int>(msg.outpost_health())},
                        {QStringLiteral("enemy_outpost_health"),
                         static_cast<int>(msg.enemy_outpost_health())},
                        {QStringLiteral("base_status"),
                         static_cast<int>(msg.base_status())},
                        {QStringLiteral("enemy_base_status"),
                         static_cast<int>(msg.enemy_base_status())},
                        {QStringLiteral("outpost_status"),
                         static_cast<int>(msg.outpost_status())},
                        {QStringLiteral("enemy_outpost_status"),
                         static_cast<int>(msg.enemy_outpost_status())},
                        {QStringLiteral("base_shield"),
                         static_cast<int>(msg.base_shield())},
                        {QStringLiteral("enemy_base_shield"),
                         static_cast<int>(msg.enemy_base_shield())},
                        {QStringLiteral("total_damage_ally"),
                         static_cast<int>(msg.total_damage_ally())},
                        {QStringLiteral("total_damage_enemy"),
                         static_cast<int>(msg.total_damage_enemy())}});
        // 详细明文日志：打印所有字段
        QStringList rHp, rBul;
        for (int i = 0; i < msg.robot_health_size(); ++i) rHp << QString::number(msg.robot_health(i));
        for (int i = 0; i < msg.robot_bullets_size(); ++i) rBul << QString::number(msg.robot_bullets(i));
        qInfo().noquote()
            << QStringLiteral("[GlobalUnitStatus] base_hp=%1 base_status=%2 base_shield=%3 "
                              "outpost_hp=%4 outpost_status=%5 "
                              "enemy_base_hp=%6 enemy_base_status=%7 enemy_base_shield=%8 "
                              "enemy_outpost_hp=%9 enemy_outpost_status=%10 "
                              "robot_hp=[%11] robot_bullets=[%12] "
                              "dmg_ally=%13 dmg_enemy=%14")
                   .arg(msg.base_health()).arg(msg.base_status())
                   .arg(msg.base_shield()).arg(msg.outpost_health())
                   .arg(msg.outpost_status()).arg(msg.enemy_base_health())
                   .arg(msg.enemy_base_status()).arg(msg.enemy_base_shield())
                   .arg(msg.enemy_outpost_health()).arg(msg.enemy_outpost_status())
                   .arg(rHp.join(QStringLiteral(",")))
                   .arg(rBul.join(QStringLiteral(",")))
                   .arg(msg.total_damage_ally()).arg(msg.total_damage_enemy());
        // 统一交给 GameData 的状态入口处理。
        m_gameData->updateGlobalUnitStatus(msg);
      }
  } else if (topic == "GlobalLogisticsStatus") {
    robomaster::GlobalLogisticsStatus msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("GlobalLogisticsStatus remaining=%1 tech=%2 encryption=%3")
              .arg(msg.remaining_economy())
              .arg(msg.tech_level())
              .arg(msg.encryption_level()),
          QJsonObject{{QStringLiteral("remaining_economy"),
                       static_cast<int>(msg.remaining_economy())},
                      {QStringLiteral("tech_level"),
                       static_cast<int>(msg.tech_level())},
                      {QStringLiteral("encryption_level"),
                       static_cast<int>(msg.encryption_level())}});
            robomaster::GameInfo gameInfo;
      const bool isBluePerspective = m_gameData->getMyRobotId() >= 100;
      gameInfo.set_stage(m_lastMqttStage);
      gameInfo.set_time_remaining(m_lastMqttTimeRemaining);
      gameInfo.set_is_paused(m_lastMqttPaused);
      gameInfo.set_current_round(static_cast<uint32_t>(m_gameData->currentRound()));
      gameInfo.set_red_score(static_cast<uint32_t>(m_gameData->redScore()));
      gameInfo.set_blue_score(static_cast<uint32_t>(m_gameData->blueScore()));
      gameInfo.set_red_economy(static_cast<uint32_t>(m_gameData->redEconomy()));
      gameInfo.set_blue_economy(static_cast<uint32_t>(m_gameData->blueEconomy()));
      if (isBluePerspective) {
        gameInfo.set_blue_economy(msg.remaining_economy());
      } else {
        gameInfo.set_red_economy(msg.remaining_economy());
      }
      m_gameData->updateGameState(gameInfo);
      m_gameData->updateGlobalLogisticsStatus(msg);
    }
  } else if (topic == "GlobalSpecialMechanism") {
    robomaster::GlobalSpecialMechanism msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      m_gameData->updateGlobalSpecialMechanism(msg);
    }
  } else if (topic == "RobotInjuryStat") {
    robomaster::RobotInjuryStat msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotInjuryStat killer_id=%1 total_damage=%2")
              .arg(msg.killer_id())
              .arg(msg.total_damage()),
          QJsonObject{{QStringLiteral("killer_id"),
                       static_cast<int>(msg.killer_id())},
                      {QStringLiteral("total_damage"),
                       static_cast<int>(msg.total_damage())}});
      m_gameData->updateRobotInjury(msg);
    }
  } else if (topic == "RobotStaticStatus") {
    robomaster::RobotStaticStatus msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotStaticStatus robot_id=%1 connection=%2 field=%3 alive=%4")
              .arg(msg.robot_id())
              .arg(msg.connection_state())
              .arg(msg.field_state())
              .arg(msg.alive_state()),
          QJsonObject{{QStringLiteral("robot_id"),
                       static_cast<int>(msg.robot_id())},
                      {QStringLiteral("robot_type"),
                       static_cast<int>(msg.robot_type())},
                      {QStringLiteral("connection_state"),
                       static_cast<int>(msg.connection_state())},
                      {QStringLiteral("field_state"),
                       static_cast<int>(msg.field_state())},
                      {QStringLiteral("alive_state"),
                       static_cast<int>(msg.alive_state())},
                      {QStringLiteral("level"),
                       static_cast<int>(msg.level())}});
      qInfo().noquote()
          << QStringLiteral("[RobotStaticStatus] robot_id=%1 type=%2 "
                            "conn=%3 field=%4 alive=%5 "
                            "perf_shooter=%6 perf_chassis=%7 "
                            "level=%8 max_hp=%9 max_heat=%10 cool_rate=%11 "
                            "max_power=%12 max_buf_energy=%13 max_chassis_energy=%14")
                 .arg(msg.robot_id()).arg(msg.robot_type())
                 .arg(msg.connection_state()).arg(msg.field_state())
                 .arg(msg.alive_state()).arg(msg.performance_system_shooter())
                 .arg(msg.performance_system_chassis()).arg(msg.level())
                 .arg(msg.max_health()).arg(msg.max_heat())
                 .arg(msg.heat_cooldown_rate()).arg(msg.max_power())
                 .arg(msg.max_buffer_energy()).arg(msg.max_chassis_energy());
      m_gameData->updateRobotStaticStatus(msg);
    }
  } else if (topic == "RobotDynamicStatus") {
    robomaster::RobotDynamicStatus msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotDynamicStatus has_no_robot_id current_health=%1 remaining_ammo=%2")
              .arg(msg.current_health())
              .arg(msg.remaining_ammo()),
          QJsonObject{{QStringLiteral("protocol_has_robot_id"), false},
                      {QStringLiteral("current_health"),
                       static_cast<int>(msg.current_health())},
                      {QStringLiteral("current_heat"), msg.current_heat()},
                      {QStringLiteral("remaining_ammo"),
                       static_cast<int>(msg.remaining_ammo())},
                      {QStringLiteral("can_remote_heal"),
                       msg.can_remote_heal()},
                      {QStringLiteral("can_remote_ammo"),
                       msg.can_remote_ammo()}});
      qInfo().noquote()
          << QStringLiteral("[RobotDynamicStatus] hp=%1 heat=%2 fire_rate=%3 "
                            "chassis_energy=%4 buf_energy=%5 "
                            "exp=%6 exp_up=%7 fired=%8 ammo=%9 "
                            "out_of_combat=%10 combat_countdown=%11 "
                            "remote_heal=%12 remote_ammo=%13")
                 .arg(msg.current_health()).arg(msg.current_heat())
                 .arg(msg.last_projectile_fire_rate())
                 .arg(msg.current_chassis_energy())
                 .arg(msg.current_buffer_energy())
                 .arg(msg.current_experience()).arg(msg.experience_for_upgrade())
                 .arg(msg.total_projectiles_fired()).arg(msg.remaining_ammo())
                 .arg(msg.is_out_of_combat())
                 .arg(msg.out_of_combat_countdown())
                 .arg(msg.can_remote_heal()).arg(msg.can_remote_ammo());
      m_gameData->updateRobotDynamicStatus(msg);
    }
  } else if (topic == "RobotPosition") {
    robomaster::RobotPosition msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotPosition robot_id=%1 x=%2 y=%3 yaw=%4")
              .arg(msg.robot_id())
              .arg(msg.x(), 0, 'f', 2)
              .arg(msg.y(), 0, 'f', 2)
              .arg(msg.yaw(), 0, 'f', 2),
          QJsonObject{{QStringLiteral("robot_id"),
                       static_cast<int>(msg.robot_id())},
                      {QStringLiteral("x"), msg.x()},
                      {QStringLiteral("y"), msg.y()},
                      {QStringLiteral("z"), msg.z()},
                      {QStringLiteral("yaw"), msg.yaw()}});
      m_gameData->updateRobotPosition(msg);
    }
  } else if (topic == "Event") {
    robomaster::Event msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("Event event_id=%1 param=%2")
              .arg(msg.event_id())
              .arg(QString::fromStdString(msg.param())),
          QJsonObject{{QStringLiteral("event_id"),
                       static_cast<int>(msg.event_id())},
                      {QStringLiteral("param"),
                       QString::fromStdString(msg.param())}});
      robomaster::RoboMasterMessage wrapper;
      *wrapper.mutable_event() = msg;
      m_gameData->processProtocolData(wrapper);
    }
  } else if (topic == "Buff") {
    robomaster::Buff msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("Buff robot_id=%1 buff_type=%2 buff_level=%3")
              .arg(msg.robot_id())
              .arg(msg.buff_type())
              .arg(msg.buff_level()),
          QJsonObject{{QStringLiteral("robot_id"),
                       static_cast<int>(msg.robot_id())},
                      {QStringLiteral("buff_type"),
                       static_cast<int>(msg.buff_type())},
                      {QStringLiteral("buff_level"),
                       static_cast<int>(msg.buff_level())}});
      m_gameData->updateBuff(msg);
    }
  } else if (topic == "RobotModuleStatus") {
    robomaster::RobotModuleStatus msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotModuleStatus main_controller=%1 video=%2")
              .arg(msg.main_controller())
              .arg(msg.video_transmission()),
          QJsonObject{{QStringLiteral("main_controller"),
                       static_cast<int>(msg.main_controller())},
                      {QStringLiteral("video_transmission"),
                       static_cast<int>(msg.video_transmission())},
                      {QStringLiteral("rfid"), static_cast<int>(msg.rfid())},
                      {QStringLiteral("uwb"), static_cast<int>(msg.uwb())}});
      m_gameData->updateRobotModuleStatus(msg);
    }
  } else if (topic == "RuneStatusSync") {
    robomaster::RuneStatusSync msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RuneStatusSync status=%1 arms=%2")
              .arg(msg.rune_status())
              .arg(msg.activated_arms()),
          QJsonObject{{QStringLiteral("rune_status"),
                       static_cast<int>(msg.rune_status())},
                      {QStringLiteral("activated_arms"),
                       static_cast<int>(msg.activated_arms())},
                      {QStringLiteral("average_rings"), msg.average_rings()}});
      m_gameData->updateRuneStatusSync(msg);
    }
  } else if (topic == "DartSelectTargetStatusSync") {
    robomaster::DartSelectTargetStatusSync msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("DartSelectTargetStatusSync target_id=%1 open=%2")
              .arg(msg.target_id())
              .arg(msg.open()),
          QJsonObject{{QStringLiteral("target_id"),
                       static_cast<int>(msg.target_id())},
                      {QStringLiteral("open"), static_cast<int>(msg.open())}});
      qInfo() << "[DartDebug] NetworkManager: RX DartSelectTargetStatusSync"
              << "targetId=" << msg.target_id()
              << "open=" << msg.open()
              << "bytes=" << payload.size();
      m_gameData->updateSiloStatusFromSync(static_cast<int>(msg.target_id()),
                                           static_cast<int>(msg.open()));
    } else {
      qWarning() << "[DartDebug] NetworkManager: Failed to parse DartSelectTargetStatusSync"
                 << "bytes=" << payload.size();
    }
  } else if (topic == "RadarInfoToClient") {
    robomaster::RadarInfoToClient msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RadarInfoToClient robot_info_size=%1")
              .arg(msg.robot_info_size()),
          QJsonObject{{QStringLiteral("robot_info_size"),
                       msg.robot_info_size()}});
      m_gameData->updateRadarInfo(msg);
    }
  } else if (topic == "TechCoreMotionStateSync") {
    robomaster::TechCoreMotionStateSync msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("TechCoreMotionStateSync max=%1 basic=%2 putin=%3 move=%4 rotate=%5 enemy=%6 remain_all=%7 remain_step=%8")
              .arg(msg.maximum_difficulty_level())
              .arg(msg.basic_state())
              .arg(msg.putin_state())
              .arg(msg.move_state())
              .arg(msg.rotate_state())
              .arg(msg.enemy_core_status())
              .arg(msg.remain_time_all())
              .arg(msg.remain_time_step()),
          QJsonObject{
              {QStringLiteral("maximum_difficulty_level"),
               static_cast<int>(msg.maximum_difficulty_level())},
              {QStringLiteral("basic_state"), static_cast<int>(msg.basic_state())},
              {QStringLiteral("putin_state"), static_cast<int>(msg.putin_state())},
              {QStringLiteral("move_state"), static_cast<int>(msg.move_state())},
              {QStringLiteral("rotate_state"), static_cast<int>(msg.rotate_state())},
              {QStringLiteral("enemy_core_status"),
               static_cast<int>(msg.enemy_core_status())},
              {QStringLiteral("remain_time_all"),
               static_cast<int>(msg.remain_time_all())},
              {QStringLiteral("remain_time_step"),
               static_cast<int>(msg.remain_time_step())}});
      emit dataReceived(QStringLiteral("MQTT TechCoreMotionStateSync max=%1 basic=%2 putin=%3 move=%4 rotate=%5 enemy=%6 remain_all=%7 remain_step=%8")
                            .arg(msg.maximum_difficulty_level())
                            .arg(msg.basic_state())
                            .arg(msg.putin_state())
                            .arg(msg.move_state())
                            .arg(msg.rotate_state())
                            .arg(msg.enemy_core_status())
                            .arg(msg.remain_time_all())
                            .arg(msg.remain_time_step())
                            .toUtf8());
      m_gameData->updateTechCoreMotionStateSync(msg);
    }
  } else if (topic == "CustomByteBlock") {
    // 简化处理：不做视频/非视频检测，始终将 payload 作为视频片段转发给解码链。
    // 如果 payload 为 Protobuf 封装（CustomByteBlock），则使用其中的 data 字段；否则直接使用原始 payload。
    robomaster::CustomByteBlock msg;
    QByteArray data;

    if (msg.ParseFromArray(payload.data(), payload.size())) {
      data = QByteArray(msg.data().data(), static_cast<int>(msg.data().size()));
      if (detailedVideoTrace) {
        dumpMqttEvent(
            QStringLiteral("parsed"), topic, payload, m_gameData,
            QStringLiteral("CustomByteBlock wrapper_bytes=%1 data_bytes=%2")
                .arg(payload.size())
                .arg(data.size()),
            QJsonObject{{QStringLiteral("wrapper_parsed"), true},
                        {QStringLiteral("data_bytes"), data.size()},
                        {QStringLiteral("data_prefix_hex"),
                         QString::fromLatin1(data.left(24).toHex())}});
        qDebug() << "[custombyte] NetworkManager: protobuf payload parsed, data size="
                 << data.size();
      }
    } else {
      dumpMqttEvent(
          QStringLiteral("parse_failed"), topic, payload, m_gameData,
          QStringLiteral("CustomByteBlock payload is not valid protobuf"));
      qWarning() << "[custombyte] NetworkManager: payload is not valid protobuf";
		  return;
    }

    // 发送端对 300B 视频片段额外包装 3 字节前缀 (0x0a 0xac 0x02)
    // 若接收到的 data 为 303B 且以该前缀开头，剥离前三字节后再交给解码链
    const QByteArray wrapperPrefix("\x0a\xac\x02", 3);
    if (data.size() == 303 && data.startsWith(wrapperPrefix)) {
      data = data.mid(3);
    }

    if (!data.isEmpty()) {
      if (detailedVideoTrace) {
        const QByteArray prefix = data.left(8).toHex();
        qDebug() << "[custombyte] NetworkManager: forwarding payload bytes="
                 << data.size() << "prefix(hex)=" << prefix;
        emit dataReceived(
            QStringLiteral("[custombyte] MQTT RX forwarded bytes=%1 prefix=%2")
                .arg(data.size())
                .arg(QString::fromLatin1(prefix))
                .toUtf8());
      }
      emit customVideoPayloadReceived(data);

      // 同时尝试解析为 0x0301 机器人自定义状态数据 (28字节, 非H264视频)
      // H264视频帧为300+字节，不会进入此分支
      if (data.size() >= static_cast<int>(sizeof(RobotCustomStatus)) &&
          data.size() <= 100) {
        parseCustomByteBlock(data);
      }
    } else {
      qWarning() << "[custombyte] NetworkManager: empty payload after unwrap";
    }
    return;
  } else if (topic == "RobotPathPlanInfo") {
    robomaster::RobotPathPlanInfo msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotPathPlanInfo sender_id=%1 intention=%2")
              .arg(msg.sender_id())
              .arg(msg.intention()),
          QJsonObject{{QStringLiteral("sender_id"),
                       static_cast<int>(msg.sender_id())},
                      {QStringLiteral("intention"),
                       static_cast<int>(msg.intention())}});
      m_gameData->updateSentryPath(msg);
    }
  } else if (topic == "PenaltyInfo") {
    robomaster::PenaltyInfo msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("PenaltyInfo type=%1 effect_sec=%2 total_penalty_num=%3")
              .arg(msg.penalty_type())
              .arg(msg.penalty_effect_sec())
              .arg(msg.total_penalty_num()),
          QJsonObject{{QStringLiteral("penalty_type"),
                       static_cast<int>(msg.penalty_type())},
                      {QStringLiteral("penalty_effect_sec"),
                       static_cast<int>(msg.penalty_effect_sec())},
                      {QStringLiteral("total_penalty_num"),
                       static_cast<int>(msg.total_penalty_num())}});
      const QByteArray debugLog =
          QStringLiteral("MQTT RX PenaltyInfo penaltyType=%1 "
                         "effectSec=%2 totalPenaltyNum=%3 currentRobotId=%4")
              .arg(msg.penalty_type())
              .arg(msg.penalty_effect_sec())
              .arg(msg.total_penalty_num())
              .arg(m_gameData ? m_gameData->getMyRobotId() : 0)
              .toUtf8();
      qInfo() << "NetworkManager:" << debugLog;
      emit dataReceived(debugLog);
      m_gameData->updatePenalty(msg);
    }
  } else if (topic == "AirSupportStatusSync") {
    robomaster::AirSupportStatusSync msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("AirSupportStatusSync status=%1 left_time=%2 cost=%3 "
                         "is_being_targeted=%4 shooter_status=%5")
              .arg(msg.airsupport_status())
              .arg(msg.left_time())
              .arg(msg.cost_coins())
              .arg(msg.is_being_targeted())
              .arg(msg.shooter_status()),
          QJsonObject{{QStringLiteral("airsupport_status"),
                       static_cast<int>(msg.airsupport_status())},
                      {QStringLiteral("left_time"),
                       static_cast<int>(msg.left_time())},
                      {QStringLiteral("cost_coins"),
                       static_cast<int>(msg.cost_coins())},
                      {QStringLiteral("is_being_targeted"),
                       static_cast<int>(msg.is_being_targeted())},
                      {QStringLiteral("shooter_status"),
                       static_cast<int>(msg.shooter_status())}});
      m_gameData->updateAirSupportStatusSync(msg);
    }
  } else if (topic == "DeployModeStatusSync") {
    robomaster::DeployModeStatusSync msg;
    if (msg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("DeployModeStatusSync status=%1").arg(msg.status()),
          QJsonObject{{QStringLiteral("status"),
                       static_cast<int>(msg.status())}});
      m_gameData->updateDeployModeStatusSync(msg);
    }
  }

  // 其他 Topic 可以后续添加处理逻辑

  // 处理 RobotRespawnStatus（单一 topic）
  if (topic.startsWith("RobotRespawnStatus")) {
    qDebug() << "NetworkManager: RobotRespawnStatus payload size" << payload.size() << "topic" << topic;

    robomaster::RobotRespawnStatus respMsg;
    if (respMsg.ParseFromArray(payload.data(), payload.size())) {
      dumpMqttEvent(
          QStringLiteral("parsed"), topic, payload, m_gameData,
          QStringLiteral("RobotRespawnStatus pending=%1 current=%2 total=%3")
              .arg(respMsg.is_pending_respawn())
              .arg(respMsg.current_respawn_progress())
              .arg(respMsg.total_respawn_progress()),
          QJsonObject{{QStringLiteral("is_pending_respawn"),
                       respMsg.is_pending_respawn()},
                      {QStringLiteral("current_respawn_progress"),
                       static_cast<int>(respMsg.current_respawn_progress())},
                      {QStringLiteral("total_respawn_progress"),
                       static_cast<int>(respMsg.total_respawn_progress())}});
      qDebug() << "NetworkManager: RobotRespawnStatus parsed as protobuf";
      QVariantMap map;
      // 始终传递 is_pending_respawn（用于显示/隐藏复活读条）
      map["is_pending_respawn"] = respMsg.is_pending_respawn();

      // 仅在 protobuf 中包含非零进度或处于 pending 状态时才传递进度字段，避免将缺省 0 值当作有效更新覆盖本地进度
      // 否则让 GameData::processRobotRespawnStatusMap 使用上次缓存的值
      if (respMsg.total_respawn_progress() > 0 || respMsg.is_pending_respawn()) {
        map["total_respawn_progress"] = static_cast<uint32_t>(respMsg.total_respawn_progress());
      }
      if (respMsg.current_respawn_progress() > 0 || respMsg.is_pending_respawn()) {
        map["current_respawn_progress"] = static_cast<uint32_t>(respMsg.current_respawn_progress());
      }

      // 布尔/费用字段始终透传，确保客户端状态严格跟随上游消息。
      map["can_free_respawn"] = respMsg.can_free_respawn();
      map["gold_cost_for_respawn"] = static_cast<uint32_t>(respMsg.gold_cost_for_respawn());
      map["can_pay_for_respawn"] = respMsg.can_pay_for_respawn();

      // robot_id 优先使用本地已知 myRobotId（避免 protobuf 默认 0 覆盖）
      int myRobotId = m_gameData ? m_gameData->getMyRobotId() : 0;
      if (myRobotId > 0) map["robot_id"] = QString::number(myRobotId);
      m_gameData->processRobotRespawnStatusMap(map);
    } else {
        dumpMqttEvent(
            QStringLiteral("parse_failed"), topic, payload, m_gameData,
            QStringLiteral("RobotRespawnStatus parse failed"));
        qWarning() << "NetworkManager: Failed to parse RobotRespawnStatus as protobuf";
    }
  }

  dumpMqttEvent(QStringLiteral("completed"), topic, payload, m_gameData,
                QStringLiteral("Topic processing finished"));
}

// ==================== 自定义 UI 数据解析 (CustomByteBlock) ====================

void NetworkManager::parseCustomByteBlock(const QByteArray &data) {
    if (data.size() < sizeof(RobotCustomStatus)) {
        qWarning() << "NetworkManager: CustomByteBlock data too small:" << data.size()
                   << "expected at least" << sizeof(RobotCustomStatus);
        return;
    }

    RobotCustomStatus status;
    // 使用 memcpy 拷贝到本地结构以避免对齐/别名问题
    std::memcpy(&status, data.constData(), sizeof(RobotCustomStatus));

    if (!m_gameData) {
        return;
    }

    // 获取当前机器人ID
    quint8 robotId = m_gameData->getMyRobotId();
    if (robotId == 0 && !m_gameData->getAllRobots().isEmpty()) {
        robotId = m_gameData->getAllRobots().first().robotId;
    }
    if (robotId == 0) {
        return;
    }

    RobotData& robot = m_gameData->getRobotDataRef(robotId);

    // 解析摩擦轮/拨弹轮状态
    robot.fricEnabled = status.friction_status.bits.fric_enabled;
    robot.rammerEnabled = status.friction_status.bits.rammer_enabled;

    // 解析底盘状态
    robot.chassisMode = status.chassis_status.bits.chassis_mode;
    robot.spinMode = status.chassis_status.bits.spin_mode;
    robot.followMode = status.chassis_status.bits.follow_mode;
    robot.chassisProtect = status.chassis_status.bits.chassis_protect;
    robot.chassisWarning = status.chassis_status.bits.chassis_warning;

    // 解析超级电容 (0.1% 精度)
    if (status.super_cap_max > 0) {
      robot.superCapEnergyPercent =
        (status.super_cap_energy * 100.0f) / status.super_cap_max;
    } else {
        robot.superCapEnergyPercent = 0.0f;
    }

    // 解析云台底盘角度 (0.1度精度)
    robot.gimbalChassisAngle = status.gimbal_chassis_angle / 10.0f;

    // 解析目标数据
    robot.targetDistance = status.target_distance_dm;
    robot.ballisticCompensation = status.ballistic_compensation;

    // 发射信号通知 UI 更新
    emit m_gameData->robotCustomDataUpdated(robotId);

    qDebug() << "NetworkManager: Parsed custom data for robot" << robotId
             << "fric=" << robot.fricEnabled
             << "cap=" << robot.superCapEnergyPercent << "%"
             << "angle=" << robot.gimbalChassisAngle;
}

#endif // RM_HAS_MQTT

} // namespace RM
