// SPDX-License-Identifier: MIT
/**
 * @file MqttManager.cpp
 * @brief MQTT 客户端管理器实现
 * @details 封装 Eclipse Paho MQTT C 客户端，实现与裁判系统服务器的 MQTT 通信。
 * @author Clear
 * @date 2026-02-08
 * @copyright Copyright (c) 2026 Fudan University EGA Team (复旦大学星云 EGA 机器人战队).
 */

#include "MqttManager.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QDateTime>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>
#include <chrono>
#include <QUrl>

namespace {

bool verboseVideoLogEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("RM_VERBOSE_VIDEO_LOG");
  return enabled;
}

bool isHighRateVideoMessage(const QString &topic, const QByteArray &payload) {
  return topic == QLatin1String("CustomByteBlock") && payload.size() > 100;
}

} // namespace

// ============================================================================
// 构造函数 / 析构函数
// ============================================================================

MqttManager::MqttManager(QObject *parent) : QObject(parent) {
  m_client = nullptr;
  m_commandTimer = new QTimer(this);
  m_commandTimer->setInterval(10);
  connect(m_commandTimer, &QTimer::timeout, this, [this] {
    const auto now = std::chrono::steady_clock::now();
    for (auto deadline : m_commands) if (now >= deadline) {
      disconnect(); // Clear all in-flight QoS state; never reconnect with old commands.
      emit errorOccurred(QStringLiteral("Command delivery unknown; PUBACK timeout"));
      return;
    }
  });
  // 初始化重连定时器
  m_reconnectTimer = new QTimer(this);
  m_reconnectTimer->setInterval(m_reconnectInterval);
  connect(m_reconnectTimer, &QTimer::timeout, this, &MqttManager::tryReconnect);
}

MqttManager::~MqttManager() { disconnect(); }

// ============================================================================
// 连接管理
// ============================================================================

void MqttManager::connectToBrokerAsync(const QString &serverUri,
                                       const QString &clientId) {
#ifdef RM_HAS_MQTT
  if (m_connecting) {
    qWarning() << "MqttManager: connectToBroker already in progress, skipping";
    emit connectCompleted(false, QStringLiteral("Connection already in progress"));
    return;
  }
  m_session.fetch_add(1);
  m_acceptMessages.store(false);
  m_overflowQueued.store(false);
  m_connecting = true;
  emit connectingStarted();

  m_serverUri = serverUri;
  m_clientId = clientId;

  // 创建 MQTT 客户端（指定 MQTT 3.1.1 版本）
  MQTTClient_createOptions opts = MQTTClient_createOptions_initializer;
  opts.MQTTVersion = MQTTVERSION_3_1_1;
  int rc = MQTTClient_createWithOptions(&m_client, m_serverUri.toUtf8().constData(),
                                        m_clientId.toUtf8().constData(),
                                        MQTTCLIENT_PERSISTENCE_NONE, nullptr, &opts);
  if (rc != MQTTCLIENT_SUCCESS) {
    qWarning() << "MqttManager: Failed to create MQTT client, rc=" << rc;
    emit errorOccurred(QString("Failed to create MQTT client: %1").arg(rc));
    m_connecting = false;
    emit connectCompleted(false, QString("Failed to create MQTT client: %1").arg(rc));
    return;
  }

  // Paho 1.3.14 rejects values below 5000 ms; never silently retain its 10s default.
  rc = MQTTClient_setCommandTimeout(m_client, 5000);
  if (rc != MQTTCLIENT_SUCCESS) {
    m_connecting = false;
    emit errorOccurred(QStringLiteral("MQTT subscription timeout configuration failed"));
    emit connectCompleted(false, QStringLiteral("MQTT timeout configuration failed"));
    return;
  }

  // 设置回调函数
  rc =
      MQTTClient_setCallbacks(m_client, this, connectionLostCallback,
                              messageArrivedCallback, deliveryCompleteCallback);
  if (rc != MQTTCLIENT_SUCCESS) {
    qWarning() << "MqttManager: Failed to set callbacks, rc=" << rc;
    emit errorOccurred(QString("Failed to set callbacks: %1").arg(rc));
    m_connecting = false;
    emit connectCompleted(false, QString("Failed to set callbacks: %1").arg(rc));
    return;
  }

  // 配置连接选项
  MQTTClient_connectOptions connOpts = MQTTClient_connectOptions_initializer;
  connOpts.keepAliveInterval = 20; // 心跳间隔 20秒
  connOpts.reliable = 0;
  connOpts.maxInflightMessages = 8;
  connOpts.cleansession = 1;       // 每次连接清除会话
  connOpts.connectTimeout = 2;     // 连接超时 2秒，避免 UI 在切换机器人时长时间卡顿
  connOpts.MQTTVersion = MQTTVERSION_3_1_1;  // 使用 MQTT 3.1.1（大多数 broker 要求）

  // 连接到 Broker
  qDebug() << "MqttManager: Connecting to" << m_serverUri;
  qInfo() << "MqttManager: Using clientId=" << m_clientId;

  // 预检：尝试解析 URI 并测试 TCP 连通性
  QUrl url(m_serverUri);
  if (!url.isValid()) {
    qWarning() << "MqttManager: [DIAGNOSIS] Invalid URI format:" << m_serverUri;
  } else {
    qDebug() << "MqttManager: Parsed host=" << url.host() << "port=" << url.port();

    // 切换机器人时会频繁切换 clientId，这里避免做 5s 的同步 TCP 预检，
    // 否则 UI 线程会明显卡顿；实际连通性由 MQTTClient_connect 返回值兜底。
    qDebug() << "MqttManager: Skip blocking TCP preflight check";
  }

  // 真正异步执行 MQTTClient_connect：后台线程执行 TCP 连接，
  // 主线程通过 onConnectCompleted 接收结果，完全无阻塞。
  // Local adaptation: this QObject belongs to the dedicated backend thread.
  // Serialize connect/subscribe/disconnect; never race a destroyed handle.
  Q_ASSERT(QThread::currentThread() == thread());
  onConnectCompleted(MQTTClient_connect(m_client, &connOpts));
#else
  Q_UNUSED(serverUri);
  Q_UNUSED(clientId);
  qWarning() << "MqttManager: MQTT disabled at compile time";
  emit connectCompleted(false, QStringLiteral("MQTT disabled at compile time"));
#endif
}

void MqttManager::disconnect() {
  m_session.fetch_add(1);
  m_acceptMessages.store(false);
  m_reconnectTimer->stop();
  m_commandTimer->stop();
  const auto pending = m_commands.keys();
  m_commands.clear();
  for (int token : pending) emit commandDelivery(token, false);

#ifdef RM_HAS_MQTT
  if (m_client && m_connected) {
    // 缩短断开等待，降低切换机器人时 UI 卡顿感
    MQTTClient_disconnect(m_client, 100);
    m_connected = false;
    emit connectionStateChanged(false);
  }

  if (m_client) {
    MQTTClient_destroy(&m_client);
    m_client = nullptr;
  }
#endif

  qDebug() << "MqttManager: Disconnected";
}

bool MqttManager::isConnected() const {
#ifdef RM_HAS_MQTT
  return m_connected && m_client && MQTTClient_isConnected(m_client);
#else
  return false;
#endif
}

// ============================================================================
// 订阅 / 发布
// ============================================================================

bool MqttManager::subscribe(const QString &topic, int qos) {
  if (!isConnected()) {
    qWarning() << "MqttManager: Cannot subscribe, not connected";
    return false;
  }

#ifdef RM_HAS_MQTT
  const auto encodedTopic = topic.toUtf8();
  char *requestedTopic = const_cast<char *>(encodedTopic.constData());
  int grantedQos = qos;
  int rc = MQTTClient_subscribeMany(m_client, 1, &requestedTopic, &grantedQos);
  if (rc == MQTTCLIENT_SUCCESS && (grantedQos < 0 || grantedQos > qos))
    rc = MQTTCLIENT_BAD_QOS;
  if (rc != MQTTCLIENT_SUCCESS) {
    qWarning() << "MqttManager: Failed to subscribe to" << topic
               << ", rc=" << rc;
    emit errorOccurred(
        QString("Failed to subscribe to %1: %2").arg(topic).arg(rc));
    return false;
  }

  qDebug() << "MqttManager: Subscribed to" << topic;
  return true;
#else
  Q_UNUSED(topic);
  Q_UNUSED(qos);
  return false;
#endif
}

MqttManager::CommandPublish MqttManager::publishCommand(
    const QString &topic, const QByteArray &payload) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isConnected()) return {PublishResult::Rejected};
  if (m_commands.size() >= 8) return {PublishResult::InFlightLimit};
#ifdef RM_HAS_MQTT
  MQTTClient_message message = MQTTClient_message_initializer;
  message.payload = const_cast<char *>(payload.constData());
  message.payloadlen = payload.size(); message.qos = 1; message.retained = 0;
  MQTTClient_deliveryToken token;
  if (MQTTClient_publishMessage(m_client, topic.toUtf8().constData(), &message, &token)
      != MQTTCLIENT_SUCCESS) return {PublishResult::Rejected};
  // The Paho callback queues to this worker; it cannot run before this insertion.
  m_commands.insert(token, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!m_commandTimer->isActive()) m_commandTimer->start();
  return {PublishResult::Queued, token};
#else
  Q_UNUSED(topic); Q_UNUSED(payload);
  return {PublishResult::Rejected};
#endif
}

// ============================================================================
// 回调函数
// ============================================================================

#ifdef RM_HAS_MQTT

int MqttManager::messageArrivedCallback(void *context, char *topicName,
                                        int topicLen,
                                        MQTTClient_message *message) {

  MqttManager *self = static_cast<MqttManager *>(context);
  if (!self) {
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
  }

  const quint64 session = self->m_session.load();
  int length = topicLen;
  if (length == 0) while (length <= 64 && topicName[length] != '\0') ++length;
  if (!self->m_acceptMessages.load() || message->retained) {
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
  }
  if (length < 1 || length > 64 || message->payloadlen < 0 || message->payloadlen > 65536
      || self->m_pendingMessages.load() >= 256) {
    if (!self->m_overflowQueued.exchange(true)) {
      self->m_acceptMessages.store(false);
      QMetaObject::invokeMethod(self, [self, session] {
        if (self->m_session.load() != session) return;
        self->disconnect();
        emit self->errorOccurred(QStringLiteral("MQTT receive limit exceeded"));
      }, Qt::QueuedConnection);
    }
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
  }
  self->m_pendingMessages.fetch_add(1);
  const qint64 receivedMonoMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  QString topic = QString::fromUtf8(topicName, length);
  QByteArray payload((const char *)message->payload, message->payloadlen);
  const qint64 receivedMs = QDateTime::currentMSecsSinceEpoch();
  const bool detailedTrace =
      !isHighRateVideoMessage(topic, payload) || verboseVideoLogEnabled();
  bool sampledVideoObservation = false;
  if (!detailedTrace) {
    qint64 lastObservedMs =
        self->m_lastVideoObservationMs.load(std::memory_order_relaxed);
    if (receivedMs - lastObservedMs >= 1000 &&
        self->m_lastVideoObservationMs.compare_exchange_strong(
            lastObservedMs, receivedMs, std::memory_order_relaxed)) {
      sampledVideoObservation = true;
    }
  }
  const bool observeMessage = detailedTrace || sampledVideoObservation;
  QString payloadSha1;
  if (observeMessage) {
    payloadSha1 = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    if (detailedTrace) {
      qDebug() << "MqttManager: Message received on topic:" << topic
               << ", size:" << payload.size();
    }
    qInfo().noquote()
        << QStringLiteral(
               "RM26_MQTT_RX_TIMING direction=rx topic=%1 received_ms=%2 "
               "bytes=%3 sha1=%4")
               .arg(topic)
               .arg(receivedMs)
               .arg(payload.size())
               .arg(payloadSha1);
  }

  // 记录完整的 MQTT 消息到日志文件（排除视频帧）
  // Do not write unbounded packet bodies to disk.

  QMetaObject::invokeMethod(
      self,
      [self, topic, payload, payloadSha1, receivedMs, receivedMonoMs, observeMessage, session]() {
        self->m_pendingMessages.fetch_sub(1);
        if (self->m_session.load() != session || !self->m_acceptMessages.load()) return;
        if (observeMessage) {
          emit self->messageObserved(topic, payload.size(), payloadSha1,
                                     receivedMs);
        }
        emit self->messageReceived(topic, payload, receivedMonoMs);
      },
      Qt::QueuedConnection);

  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);
  return 1;
}

void MqttManager::connectionLostCallback(void *context, char *cause) {
  MqttManager *self = static_cast<MqttManager *>(context);
  if (!self)
    return;

  const quint64 session = self->m_session.load();
  self->m_acceptMessages.store(false);
  QString reason = cause ? QString::fromUtf8(cause) : "Unknown";
  QMetaObject::invokeMethod(
      self,
      [self, reason, session]() {
        if (self->m_session.load() != session) return;
        // Destroy the old handle before scheduling a clean reconnect. Paho's
        // automatic reconnect on that handle would resend its old QoS1 commands.
        self->disconnect();
        qWarning() << "MqttManager: Connection lost, reason:" << reason;

        emit self->errorOccurred(QString("Connection lost: %1").arg(reason));

        // 通过 QObject 所在线程启动重连定时器，避免 Paho 回调线程直接碰 Qt timer
        self->m_reconnectTimer->start();
      },
      Qt::QueuedConnection);
}

void MqttManager::deliveryCompleteCallback(void *context,
                                           MQTTClient_deliveryToken token) {
  auto *self = static_cast<MqttManager *>(context);
  if (!self) return;
  const auto session = self->m_session.load();
  const auto arrived = std::chrono::steady_clock::now();
  QMetaObject::invokeMethod(self, [self, session, token, arrived] {
    if (self->m_session.load() != session || !self->m_commands.contains(token)) return;
    if (arrived >= self->m_commands.value(token)) {
      self->disconnect();
      emit self->errorOccurred(QStringLiteral("Command delivery unknown; late PUBACK"));
      return;
    }
    self->m_commands.remove(token);
    if (self->m_commands.isEmpty()) self->m_commandTimer->stop();
    emit self->commandDelivery(token, true);
  }, Qt::QueuedConnection);
}

// ============================================================================
// 异步连接完成处理（在主线程执行）
// ============================================================================

void MqttManager::onConnectCompleted(int rc) {
  m_connecting = false;

  if (rc != MQTTCLIENT_SUCCESS) {
    qWarning() << "MqttManager: Failed to connect, rc=" << rc;
    QString errorDetail =
        QStringLiteral("MQTT connect failed (rc=%1, clientId=%2, broker=%3)")
            .arg(rc)
            .arg(m_clientId, m_serverUri);
    bool shouldRetry = true;

    // CONNACK 错误码诊断 (Paho MQTT C 返回码 1-5 表示 broker 拒绝)
    if (rc == 1) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(1): Bad protocol version";
      errorDetail = QStringLiteral("MQTT CONNACK(1): bad protocol version");
      shouldRetry = false;
    } else if (rc == 2) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(2): Identifier rejected - clientId '"
                 << m_clientId << "' was rejected by broker";
      qWarning() << "MqttManager: [SUGGESTION] Override with RM_CLIENT_ROBOT_ID=<new_id>"
                 << "(PowerShell: $env:RM_CLIENT_ROBOT_ID=<new_id>)";
      qWarning() << "MqttManager: [SUGGESTION] If the selected robot ID is correct,"
                 << "check the engine custom-client registration/IP whitelist and"
                 << "verify RM_MQTT_BROKER/RM_MQTT_PORT point to the active engine broker";
      errorDetail =
          QStringLiteral("MQTT CONNACK(2): identifier rejected for clientId=%1 "
                         "(broker=%2). Check network.client_robot_id/RM_CLIENT_ROBOT_ID; "
                         "if the ID is correct, check engine custom-client registration, "
                         "client IP whitelist, or active broker address.")
              .arg(m_clientId, m_serverUri);
      shouldRetry = false;
    } else if (rc == 3) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(3): Server unavailable";
      errorDetail = QStringLiteral("MQTT CONNACK(3): server unavailable");
    } else if (rc == 4) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(4): Bad username or password";
      errorDetail = QStringLiteral("MQTT CONNACK(4): bad username or password");
      shouldRetry = false;
    } else if (rc == 5) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(5): Not authorized";
      errorDetail = QStringLiteral("MQTT CONNACK(5): not authorized");
      shouldRetry = false;
    } else if (rc == 133) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(133): Client identifier not valid - clientId '"
                 << m_clientId << "' was rejected by broker";
      qWarning() << "MqttManager: [SUGGESTION] Use the robot ID accepted by the current field broker"
                 << "(override with RM_CLIENT_ROBOT_ID=<id>)";
      errorDetail =
          QStringLiteral("MQTT CONNACK(133): clientId=%1 not valid for broker=%2. "
                         "Check network.client_robot_id or RM_CLIENT_ROBOT_ID.")
              .arg(m_clientId, m_serverUri);
      shouldRetry = false;
    } else if (rc == 134) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(134): Bad username or password";
      errorDetail = QStringLiteral("MQTT CONNACK(134): bad username or password");
      shouldRetry = false;
    } else if (rc == 135) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(135): Not authorized";
      errorDetail = QStringLiteral("MQTT CONNACK(135): not authorized");
      shouldRetry = false;
    } else if (rc == 136) {
      qWarning() << "MqttManager: [DIAGNOSIS] CONNACK(136): Server unavailable";
      errorDetail = QStringLiteral("MQTT CONNACK(136): server unavailable");
    } else if (rc == -1) {
      qWarning() << "MqttManager: [DIAGNOSIS] Network/transport error (code: -1)";
      qWarning() << "MqttManager: [SUGGESTION] This usually means:";
      qWarning() << "  - TCP connection refused (broker not running)";
      qWarning() << "  - Network unreachable";
      qWarning() << "  - SSL/TLS handshake failure (if using ssl://)";
      errorDetail = QStringLiteral("MQTT transport error (rc=-1): connection refused/unreachable");
    } else if (rc < 0) {
      qWarning() << "MqttManager: [DIAGNOSIS] Network/transport error (code:" << rc << ")";
      errorDetail = QStringLiteral("MQTT transport error (rc=%1)").arg(rc);
    }

    // 清理失败的客户端句柄，避免下次重连时创建新句柄导致旧句柄泄漏
    if (m_client) {
      MQTTClient_destroy(&m_client);
      m_client = nullptr;
    }

    if (shouldRetry && m_retryCount < 3) {
      m_retryCount++;
      m_reconnectTimer->start();
    } else if (m_retryCount >= 3) {
      qWarning() << "MqttManager: Max retry attempts reached, giving up";
      m_retryCount = 0;
    }

    emit errorOccurred(errorDetail);
    emit connectCompleted(false, errorDetail);
    return;
  }

  // 连接成功
  m_connected = true;
  m_retryCount = 0;
  m_reconnectTimer->stop();
  qDebug() << "MqttManager: Connected successfully";
  emit connectionStateChanged(true);
  emit connectCompleted(true, QString());
}

// ============================================================================
// 重连逻辑
// ============================================================================

void MqttManager::tryReconnect() {
  if (isConnected()) {
    m_reconnectTimer->stop();
    return;
  }

  if (m_connecting) {
    qDebug() << "MqttManager: Reconnect skipped, already connecting";
    return;
  }

  qDebug() << "MqttManager: Attempting to reconnect...";

#ifdef RM_HAS_MQTT
  // 销毁旧客户端
  if (m_client) {
    MQTTClient_destroy(&m_client);
    m_client = nullptr;
  }
#endif

  // 重新连接
  connectToBrokerAsync(m_serverUri, m_clientId);
}

#endif // RM_HAS_MQTT
