// SPDX-License-Identifier: MIT
/**
 * @file NetworkManager.h
 * @brief 网络通信管理器
 * @details 本文件定义了 NetworkManager 类，负责处理客户端与裁判系统服务器之间的
 *          UDP 与 MQTT 通信。主要功能包括：
 *          - 绑定端口监听传入 UDP 数据包
 *          - 发送原始字节数据和 Protobuf 消息
 *          - 连接官方自定义客户端 MQTT Broker 并处理 Topic 收发
 *          - 接收并解析服务器下发的比赛数据
 *          - 将解析后的数据分发到 GameData 进行状态更新
 *
 *          通信架构：
 *          ┌─────────────┐ UDP/MQTT ┌─────────────────┐
 *          │ 裁判系统服务器 │ ◄──────► │ NetworkManager  │
 *          └─────────────┘          └────────┬────────┘
 *                                            │
 *                                   ┌────────▼────────┐
 *                                   │    GameData     │
 *                                   │  (状态更新)      │
 *                                   └─────────────────┘
 *
 * @author Clear
 * @date 2025-11-29
 * @copyright Copyright (c) 2025 Fudan University EGA Team (复旦大学星云 EGA 机器人战队).
 */

#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

// --- 头文件包含 ---
#include "../core/GameData.h" // 比赛数据中心，用于存储解析后的比赛状态
#include "Protocol.h"         // 协议处理类，提供 Protobuf 序列化/反序列化
#include <QHostAddress>       // Qt 网络地址类
#include <QObject>            // Qt 基础对象类
#include <QUdpSocket>         // Qt UDP 套接字类
#include <functional>
#include "protocol/rm2026/upstream_commands.hpp"

#ifdef RM_HAS_MQTT
#include "MqttManager.h" // MQTT 客户端管理器
#endif

namespace RM {

/**
 * @class NetworkManager
 * @brief UDP 网络通信管理类
 * @details 封装了 QUdpSocket，提供简洁的数据收发接口。
 *
 *          使用方式：
 *          1. 创建实例并传入 GameData 指针
 *          2. 调用 startListening() 开始监听
 *          3. 使用 sendCommand() 发送有类型命令
 *          4. 接收的数据会自动解析并更新 GameData
 *
 *          线程安全性：
 *          - 该类在专用后端线程中使用
 *          - 信号槽机制确保数据处理的线程安全
 */
class NetworkManager : public QObject {
  Q_OBJECT

public:
  // --- 构造与析构 ---

  /**
   * @brief 构造函数
   * @details 初始化 UDP 套接字并建立信号槽连接。
   *          套接字的 readyRead 信号连接到 onReadyRead 槽函数。
   *
   * @param gameData 比赛数据指针，用于存储解析后的数据
   * @param parent   Qt 父对象，用于内存管理
   *
   * @note gameData 不能为 nullptr，否则数据无法更新
   */
  explicit NetworkManager(GameData *gameData, QObject *parent = nullptr);

  /**
   * @brief 析构函数
   * @details 调用 stopListening() 关闭套接字，释放资源
   */
  ~NetworkManager();

  // --- 连接控制 ---

  /**
   * @brief 启动 UDP 监听
   * @details 将 UDP 套接字绑定到指定端口，开始接收传入的数据包。
   *          使用 QHostAddress::Any 表示监听所有网络接口。
   *
   * @param port 监听端口号，默认 10000
   * @return bool true = 绑定成功，false = 绑定失败
   *
   * @note 如果端口已被占用，会发送 errorOccurred 信号
   *
   * @example
   * @code
   * NetworkManager manager(gameData);
   * if (manager.startListening(10000)) {
   *     qDebug() << "开始监听端口 10000";
   * }
   * @endcode
   */
  bool startListening(quint16 port = 10000);

  /**
   * @brief 停止 UDP 监听
   * @details 关闭 UDP 套接字，停止接收数据。
   *          如果套接字已经处于未连接状态，则不执行任何操作。
   */
  void stopListening();

  bool isUdpListening() const;
  quint16 listeningPort() const;
  quint64 totalUdpPacketsReceived() const { return m_totalUdpPacketsReceived; }
  // --- MQTT 连接控制 ---

#ifdef RM_HAS_MQTT
  /**
   * @brief 启动 MQTT 连接（异步，非阻塞）
   * @details 在后台线程连接 MQTT Broker，主线程不会阻塞。
   *          连接结果通过 mqttConnectCompleted 信号通知。
   * @param brokerUri Broker 地址，格式 "tcp://ip:port"
   * @param clientId 客户端 ID
   */
  void startMqtt(const QString &brokerUri, const QString &clientId);

  /**
   * @brief 停止 MQTT 连接
   */
  void stopMqtt();

  /**
   * @brief 检查 MQTT 是否已连接
   */
  bool isMqttConnected() const;
  bool topicsSubscribed() const { return m_topicsSubscribed; }
  void setSubscriptionCurrent(std::function<bool()> current) { m_subscriptionCurrent = std::move(current); }
#endif

  // Worker-thread only. Current ordinary controller has no live control permit.
  rm27::protocol::rm2026::CommandSendResult sendCommand(
      const rm27::historical::rm2026::Command &command,
      const rm27::historical::rm2026::CommandContext &context);
  void cancelCommands();
  int commandsInFlight() const;
  // Read-only check on actual worker state; does not publish or consume a limit.
  bool keyboardInputAllowed(const rm27::historical::rm2026::CommandContext &context) const;

signals:
  void commandDelivery(int token, bool brokerAcknowledged);
  // --- 信号定义 ---

  /**
   * @brief 数据接收信号
   * @details 当收到 UDP 数据包时发出。
   *          可用于调试或原始数据记录。
   *
   * @param data 接收到的原始字节数据
   */
  void subscriptionStateChanged(bool subscribed);
  void dataReceived(const QByteArray &data);

  /**
   * @brief 错误发生信号
   * @details 当网络操作失败时发出（如端口绑定失败）。
   *
   * @param error 错误描述字符串
   */
  void errorOccurred(const QString &error);

  /**
   * @brief 数据发送信号
   * @details 当数据被发送后发出。
   *          主要用于调试和数据追踪。
   *
   * @param data 已发送的原始字节数据
   */
  void dataSent(const QByteArray &data);

  /**
   * @brief 自定义视频负载信号
   * @details 转发 MQTT CustomByteBlock 中提取出的视频负载。
   *          供视频接收链订阅，避免在 NetworkManager 内直接依赖解码器。
   *
   * @param data 需要交给视频解码链的原始负载
   */
  void customVideoPayloadReceived(const QByteArray &data);

#ifdef RM_HAS_MQTT
  /**
   * @brief MQTT 消息被客户端本地观测到
   * @details 该时间戳来自客户端本机 MQTT 回调，不代表赛事引擎发布时刻。
   *          高频 CustomByteBlock 视频默认最多每秒采样一次；普通 topic
   *          仍逐条转发。
   */
  void mqttMessageObserved(const QString &topic, int payloadSize,
                           const QString &payloadSha1, qint64 receivedMs);

  /**
   * @brief MQTT 异步连接完成信号
   * @param success 是否连接成功
   * @param error 失败时的错误描述
   */
  void mqttConnectCompleted(bool success, const QString &error);
#endif

private slots:
  // --- 私有槽函数 ---

  /**
   * @brief UDP 数据就绪处理槽
   * @details 当 QUdpSocket 有待读取的数据时自动调用。
   *          循环读取所有挂起的数据报并调用 processData 处理。
   */
  void onReadyRead();

private:
  bool m_topicsSubscribed = false;
  QString m_requestedClientId;
  std::function<bool()> m_subscriptionCurrent;
  // --- 私有成员 ---

  QUdpSocket *m_udpSocket; ///< UDP 套接字实例
  GameData *m_gameData;    ///< 比赛数据中心指针（不拥有所有权）

  /**
   * @brief 处理接收到的数据
   * @details 内部方法，使用 Protocol 类解析数据包，
   *          成功后将消息传递给 GameData 更新状态。
   *
   * @param data 接收到的原始字节数据
   *
   * @note 解析失败的数据包会被静默丢弃（不输出日志以避免刷屏）
   */
  void processData(const QByteArray &data);

#ifdef RM_HAS_MQTT
  // --- MQTT 相关私有方法 ---

  /**
   * @brief 处理 MQTT 消息
   * @param topic 主题名称
   * @param payload 消息载荷
   */
  void processMqttMessage(const QString &topic, const QByteArray &payload, qint64 receivedMonoMs);

  /**
   * @brief 解析 CustomByteBlock 数据
   * @param data 原始字节数据
   * @details 解析机器人自定义状态并更新 GameData
   */
  void parseCustomByteBlock(const QByteArray &data);

  /**
   * @brief 订阅所有需要的 MQTT Topic
   */
  void subscribeToTopics();

  // --- MQTT 成员变量 ---
  MqttManager *m_mqttManager = nullptr; ///< MQTT 客户端管理器
  bool m_hasMqttGameStatus = false; ///< 是否已有 MQTT 比赛全局状态
  robomaster::GameStage m_lastMqttStage =
      robomaster::STAGE_NOT_STARTED; ///< 最近一次 MQTT 比赛阶段
  quint16 m_lastMqttTimeRemaining = 420; ///< 最近一次 MQTT 阶段剩余时间 (秒)
  bool m_lastMqttPaused = false;         ///< 最近一次 MQTT 暂停状态
#endif

  rm27::historical::rm2026::CommandRateLimiter m_commandRate;
  bool m_controlActive = false;
  quint64 m_totalUdpPacketsReceived = 0; ///< UDP 已接收的数据报数量
};

} // namespace RM

#endif // NETWORKMANAGER_H
