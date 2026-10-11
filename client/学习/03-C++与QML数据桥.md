# 第 3 阶段：裁判数据怎样变成界面

预计 3 次，每次 30～45 分钟。目标：追踪一个真实代码字段从模拟 JSON 到 HUD，而不是先学一整套网页前端。先完成第 1、2 阶段；本阶段不改正式通信或指令门。

## 第 1 次：你熟悉的 C++，怎样被界面看到

读 [Qt 官方中文：向 QML 暴露 C++ 类型的属性](https://doc.qt.io/qt-6/zh/qtqml-cppintegration-exposecppattributes.html)，重点看 QObject、属性与信号；注册复杂类型的部分先跳过。

看 [`src/app/main.cpp`](../src/app/main.cpp)：创建 TerminalController，通过 setContextProperty("terminal", &terminal) 让 QML 看到它。再看 [`qml/Main.qml`](../qml/Main.qml) 的 backend: terminal。这不是 JSON 解析，也不是网络请求，只是在界面引擎中提供一个 C++ 对象。

看 [`terminal_controller.hpp`](../src/app/terminal_controller.hpp) 的 Q_PROPERTY(QString hpLabel READ hpLabel NOTIFY stateChanged)：hpLabel 是界面读到的属性名，READ 指定 C++ getter，NOTIFY 指定变化通知。单独修改 C++ 成员变量不会自动告诉 QML；需要发出对应通知。

与 CV 程序比较：以前你修改 cv::Mat 然后 imshow；这里修改状态、发信号，QML 的绑定重新计算。界面有自己的事件循环，不应在它里面跑长时间视频读取或 sleep。

验收：找到 hpLabel 的声明、getter、给 hpLabel_ 赋值的位置，以及 stateChanged 的触发位置。说清楚每个位置的作用。

## 第 2 次：追踪一包数据

按这个顺序读，不必读完整个类：

1. [`src/sim/sim_referee_receiver.cpp`](../src/sim/sim_referee_receiver.cpp)：parseRefereeDatagram 校验字段；acceptPayload 拒绝重复/乱序序号；sampleReceived 发出已经验证的值。
2. [`terminal_controller.cpp`](../src/app/terminal_controller.cpp)：构造函数 connect 连接 sampleReceived 和 applySimulationSample；后者将 hp 变成 hpLabel，同时生成 hudSnapshot。
3. [`qml/HudOverlay.qml`](../qml/HudOverlay.qml)：查找 hpLabel、hudSnapshot，看界面绑定如何读数据。

| 层 | 负责什么 | 不该负责什么 |
| --- | --- | --- |
| 接收/解析 | 网络、类型和范围、顺序与过期 | 决定文字位置或颜色 |
| Controller | 转成界面可用状态、模式与安全限制 | 自己在 GUI 线程做阻塞解码 |
| QML | 展示、绑定、用户交互 | 自己解析网络包或下发底层指令 |

在模拟器控制台把本车血量改为 100，再发一个机器人升级事件。观察左下血量和左侧消息；停止裁判发送，约 600 ms 后 HUD/消息清空。这个过程能证明你找到的是动态数据链，不是写死的文本。

验收：描述 telemetry.hp 与 hud.friendly_hp[self_index] 为什么应该一致；谁负责这个一致性？提示：模拟器的统一状态产生载荷，客户端只校验并显示，不凭空修正赛事数据。

## 第 3 次：读更新通知，不急着新增字段

将 Q_PROPERTY 的 NOTIFY 与 QML 属性绑定联系起来。hudSnapshot 是一个给 UI 使用的 QVariantMap，eventMessages 是 QVariantList；它们不是官方 Protobuf，也不是让 QML 访问网络层的捷径。

小练习：先不改代码，追踪 own_team 如何进入 hudSnapshot，再找 ownTeamBadge 怎样变红/蓝；查清数据过期后会变成什么。接着比较 latencyLabel 与 videoDiagnostics：前者是模拟展示数字，后者包含真正观测到的帧年龄/恢复间隔，语义不同。

如要新增协议字段，先读 [`contracts/模拟接口-v1.md`](../contracts/模拟接口-v1.md)，然后同步双方样例、Python 生成/校验测试与 C++ 解析测试。不要只在 QML 里写一个默认数字假装字段已接入。

阶段通过标准：能不看答案讲清“解析 → 信号 → 状态 → 通知 → 绑定”，能解释为什么模拟数据再新鲜也不能解锁真实控制。下一阶段：[视频与低延迟](./04-视频与低延迟.md)。
