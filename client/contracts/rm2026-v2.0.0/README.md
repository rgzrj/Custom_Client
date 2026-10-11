# RM2026 V2.0.0 通信参考：离线解析与本机只读订阅

## 本阶段做到了什么

这是按用户要求，以 **RoboMaster 2026 机甲大师高校系列赛通信协议 V2.0.0（20260626）** 为依据建立的独立历史版本参考，不是 RM2027 正式适配器。它不读取模拟器源码，不改变 `sim.v1` 契约，不接入 HUD、不连接外部网络、不发送操作指令、不修改 Windows 网卡。第二阶段新增的 MQTT 开发探针只连接字面量 `127.0.0.1`。

- `client.proto`：从明确的表格和消息定义整理出 14 个接收消息和 3 个仅供离线编码检查的发送消息。它是本项目整理文件，不是官方分发的 proto。字段号、类型和 optional 保留；修正 PDF 排版造成的空格/分号问题。本地 package 仅用于名称隔离，不改变 MQTT topic 或 Protobuf 二进制字段。
- `tools/rm2026_protocol.py`：使用 **protoc 编译的 descriptor + Google Protobuf 运行库**，不自行手写 Protobuf 解析器。检查版本选择、主题白名单、截断、资源限额、非有限坐标与重复数组长度；保留未收到的 optional 字段，不把缺失当成 0。
- `Snapshot`：各主题用单调接收时间独立过期；无有效已知字段、无效消息和 retained 消息不能刷新数据。断线/切换会话应调用 `reset()`。1500 ms 默认窗口为项目决策，可由调用方按消息频率指定；不是官方超时。
- `tools/rm2026_mqtt_observer.py`：独立 Python 只读开发探针，复用上述解析器与 Eclipse Paho MQTT；14 个精确主题全部获得 QoS0/1 SUBACK 才接受数据。支持后台收包、QoS1 PUBACK、断线立即清空、退避重连和重新订阅；没有公开发布接口，也不绑定 QML。它是协议行为的可执行参考，不是正式 Qt 客户端依赖或 C++ 适配器。
- `src/protocol/rm2026/mqtt_observer.*` 与 `tools/rm2026_mqtt_probe.cpp`：第三阶段原生 C++ 本机只读参考。Google Protobuf 生成代码/运行库解析，Paho C 后台连接，逐主题 SUBACK、5 秒订阅超时、断线清空和重连；运行时无需 Python。使用默认关闭的独立构建目标，尚未挂到终端/HUD。
- `src/core/rm2026_state.hpp` 与 `src/protocol/rm2026/state_mapping.*`：第四阶段的独立类型化只读状态。映射本车配置/动态、双方单位和本方经济；领域头文件不依赖 Qt/MQTT/Protobuf，各块携带单调接收时间，过期/断线后消失。没有写入现有 `TerminalState` 或模拟 HUD。
- 第五阶段在同一快照中追加比赛状态与判罚：阶段枚举、局号/得分/时间/暂停，结算时有效的胜者与结束原因，以及判罚种类/效果时长/数量。不将它们连接到指令放行或处罚遮挡逻辑。
- 第六阶段追加 11 个模块状态与 6 个复活报告字段：模块原始码和已知离线/在线/安装不规范离线状态，复活待复活标志、所需/当前读条、金币费用及报告资格。不把模块在线当作实时图传，不自动执行复活。
- 第七阶段追加最新事件与位置报告：识别 15 类已知事件并保留编号/原始参数；保留世界 XYZ、以度为单位的北向零朝向和报告坐标归属 ID。不执行事件、不生成完整历史、不推断地图坐标或缺失的本车身份。
- 第八阶段提供独立 `rm2026_mqtt_viewer` 本机只读监视窗口，复用原生探针的版本/地址校验、订阅器与类型化诊断快照；十类数据可展开查看。只用于开发联调，不修改正常操作手 HUD，也没有发布/登录/复活按钮。
- 第九阶段提供有界的合成本机报文演示入口，无需单独部署 broker：正常接收、静默过期、TCP 断线、自动重连及精简字段恢复。独立测试对端只服务自己启动的 viewer，不是另一套通用裁判模拟器。
- `src/protocol/rm2026/video_reassembly.*`：无 Qt 依赖的 C++ 分片重组参考，检查 8 字节大端头、长度和序号，支持帧内乱序、重复、帧号回绕，丢弃不完整旧帧及超时帧。最多保留一个在组帧，帧大小上限 2 MiB、组帧超时 150 ms 均为项目决策。

**未完成：正式网络入站资源防护及消息完整性策略、其余主题的领域映射与 HUD 接入、实物 HEVC 图传验收、真实键鼠或赛事操作发布。** 第十三阶段仅补独立本机 HEVC 组帧/解码/HUD 实验。原生只读参考已完成本机验收，但正常客户端及发布包没有启用这些历史协议路径，现有模拟/回放安全门没有变化。`Snapshot` 的接收新鲜度也不能证明发送端时间新鲜；该协议没有通用发送时间/序号字段，不能宣称已经测得端到端延迟。

## 已确认的 2026 信息与来源

下表页码为 **PDF 阅读器页码**，比正文印刷页码多 1。结论只适用于这份 2026 文件。

| 内容 | 来源 | 实现边界 |
| --- | --- | --- |
| MQTT 直接载荷为 Protobuf v3 二进制，topic 为指令名；支持 Protobuf 3.15 以上 | §2，PDF 45 | 使用 descriptor 编译/解析，不把 JSON 模拟包当成它 |
| 服务端 `192.168.12.1:3333`；自定义客户端 IP `192.168.12.2`；clientID 为连接机器人对应 ID | §2，PDF 45 | 只记录历史参数，不写入正式启动预设、不修改网卡 |
| 表 2-1 所选消息最高 QoS 为 1；GameStatus 5 Hz、动态状态 10 Hz、多数状态 1 Hz、事件触发发送 | §2.1，PDF 45-49 | 最高 QoS 不是实际 QoS/会话配置；附录示例的默认 QoS2 不作为依据 |
| UDP 3334；HEVC/H.265，1920×1080，60 帧，无重传 | §2，PDF 45 | 记录为 2026 条件；组帧完成不是成功解码，更不是实时画面 |
| 包头：帧号 uint16、分片号 uint16、帧总字节数 uint32，全为大端；包最大 1400 B，载荷最大 1392 B | §2，PDF 45 | 已实现包头检查；分片起始号与短片规则见下节 |
| 基地/前哨状态、双方血量数组 | §2.2.4，PDF 53-54 | 不沿用模拟六槽数组、不凭空添加最大血量 |
| 经济、本车静态/动态、模块、位置、事件、判罚等 | §2.2.5/7/9-13/15/20，PDF 54-67 | 保留原始字段语义，未绑定显示及判罚遮挡行为 |
| 键鼠、30 B 自定义控制、常用命令 | §2.2.1/2/25，PDF 49-51、69-70 | 仅 schema/离线序列化，不存在发布者或控制通路 |

未定义消息一律不自动解析；不采用社区 proto 填补官方文档的缺口。本文件不是对协议全章节的完整实现。

## 当前不能猜的部分

1. **雷达消息结构有冲突**：§2.2.19，PDF 66 的表格编号为 1，正文描述 12 台机器人结构；代码却写 `repeated uint32 RadarSingleRobotInfo;`，缺 `= 字段号`，且与 PDF 67 的独立 `RadarSingleRobotInfo` 消息定义不一致。因此不生成 `RadarInfoToClient`，解析器明确拒绝该主题。需要官方可编译 proto/更正才能确定是 repeated scalar 还是 repeated message，不能以“未来改动小”解决这一冲突。
2. **视频分片起始号未明确**：构造 `VideoReassembly(0)` 或 `(1)` 时必须显式选择，不能把其中一个描述为官方确认值。重组参考假设非末片恰好 1392 B、末片为剩余长度；原文明确最大包长，但没有完整定义短非末片/填充规则。该假设必须用有来源的发送端样本核验后才能进入 live adapter。
3. **帧重启与长间断**：16 位序号比较以半周期为界是项目决策；超过 32767 帧的跳变不能判断新旧。发送端重启或建立新会话须显式 reset，不能自动重放旧片。
4. **位置映射**：`RobotPosition` 描述世界 XYZ/yaw（正北为 0°），不能仅凭这些字段完成当前地图坐标标定；雷达坐标写 cm，但消息结构仍待确认。不同来源的单位、轴向和原点不做自动融合。
5. **会话信息**：文档没有完整说明认证、keepalive、会话恢复、retained 使用和机器人登录确认流程。这些不能从附录代码默认值外推成规则。
6. **版本判断**：`--protocol rm2026-v2.0.0` 是本地显式选择，不是从 wire 识别版本。未知字段可前向兼容，也意味着解析成功不能证明它是 2026 或 2027。

## 与当前 HUD 字段的区别

- `GlobalUnitStatus.robot_health` 顺序是 **己方 1、2、3、4、7；对方 1、2、3、4、7**，共 10 个；不是“哨兵、无人机、步兵3、步兵4、工程、英雄”两组六个槽位。该数组没有空中机器人血量，不能把下一项错当作无人机。
- `remaining_ammo` 是“剩余允许发弹量”，不是弹仓内实际弹丸数量。射速来自 `last_projectile_fire_rate`，不得换成模拟 `speedMps` 的猜测映射。
- 基地字段没有 maxHp；前哨当前 HP 与状态是不同信息。未知数据不等于摧毁，optional 缺失必须继续未知。
- protobuf 静态 ID 可辅助核实身份，但 preset 的红/蓝选择不是服务器登录成功。机器人 ID 参考附录二（PDF 80-81）：红方 1/2/3/4/6/7、蓝方 101/102/103/104/106/107 等，不能使用末页残留示例中的另一套编号。
- 没有明确对应的 `CLIMB / FIRE / SPEED UP` 三个字段；保留现有模拟状态提示，不伪造真实行为。
- 判罚消息解析成功不等于已经执行规则要求的遮挡/禁用，真实接入前须单独核对规则和安全门。

## 离线复现（从 client 目录执行）

本机已具备 `D:\YOLOV8\python.exe`（Protobuf 5.29.3）和 `D:\YOLOV8\Library\bin\protoc.exe`（29.3）。它们只用于开发检查，不是 Qt 发布运行依赖。其他电脑需要可用的 protoc（支持 proto3 optional）与匹配的 Google Protobuf Python 运行库。

```powershell
New-Item -ItemType Directory -Force build-rm2026 | Out-Null
& 'D:\YOLOV8\Library\bin\protoc.exe' --proto_path=contracts/rm2026-v2.0.0 --descriptor_set_out=build-rm2026/client.desc contracts/rm2026-v2.0.0/client.proto
& 'D:\YOLOV8\python.exe' tests/test_rm2026_protocol.py --descriptor build-rm2026/client.desc -v

. .\scripts\environment.ps1
$rm27Tools = Get-Rm27Environment
& $rm27Tools.cmake --build build-gui --target rm2026_video_tests
& $rm27Tools.ctest --test-dir build-gui -R rm2026_video_tests --output-on-failure
```

最后两行需要已有 CMake 构建目录；首次配置可按主 README 的构建说明执行。descriptor 放在忽略的 `build-rm2026/`，更改 proto 后重新运行 protoc，不能用旧 descriptor 验收新 schema。

检查已有本地二进制样本（无网络、无发送）：

```powershell
& 'D:\YOLOV8\python.exe' tools/rm2026_protocol.py --descriptor build-rm2026/client.desc --protocol rm2026-v2.0.0 --topic GameStatus --payload '本地样本.bin'
```

工具只输出已出现的字段；空包/仅未知字段输出空对象，不能据此点亮“实时”。测试包均由项目人工编写或 protobuf 生成，未复制第三方代码/赛事抓包。

## 本机只读 MQTT 验收（第二阶段）

**只用于开发，不用修改 Qt 安装或 CV 环境。** Paho 2.1.0 依赖固定版本及 wheel SHA-256，许可证为 EPL-2.0 OR BSD-3-Clause；安装到忽略的构建目录，不进入发布包。Protobuf/protoc 仍使用上节已有开发工具。[Paho 官方 API 文档](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)是连接、回调 API v2 与重连行为的工程依据，不是赛事协议来源。

```powershell
& 'D:\YOLOV8\python.exe' -m pip install --target build-rm2026/python-deps --upgrade --no-deps --only-binary=:all: --require-hashes --index-url https://pypi.org/simple -r contracts/rm2026-v2.0.0/requirements-probe.txt
$env:PYTHONPATH = (Resolve-Path build-rm2026/python-deps).Path
& 'D:\YOLOV8\python.exe' tests/test_rm2026_mqtt.py --descriptor build-rm2026/client.desc -v
```

该测试自己建立短生命周期的环回 TCP 对端、使用系统分配端口，发送人工构造的 Protobuf 包。覆盖连接拒绝/重连、SUBACK 拒绝/数量错误/超出 QoS1、订阅恢复、TCP 拆包、QoS0/1、PUBACK、MID 重用/重复、retained、空/截断/超限/未知主题、逐主题过期和断线清空，并检查线上没有客户端 PUBLISH。测试对端不作为通用 broker 或裁判模拟器提供服务，不读取另一个项目源码。

需要观察自己的**本机二进制测试 broker**时：

```powershell
& 'D:\YOLOV8\python.exe' tools/rm2026_mqtt_observer.py --descriptor build-rm2026/client.desc --protocol rm2026-v2.0.0 --host 127.0.0.1 --port 39005
```

按 Ctrl+C 停止。探针输出 JSON 行：`link` 是订阅链路状态，`fresh` 只含尚未过期的已知字段，`stale` 是过期主题，`counts` 是收包验收计数。`subscribed` 不代表有新鲜数据；断线时 `fresh`/`stale` 都清空。已有 `sim.v1` JSON broker（39004）**不兼容这些 Protobuf 主题**，不要切换它来冒充 2026 数据。

本机 profile 的限制与项目决策：

- 固定环回地址，39005 默认开发端口，MQTT 3.1.1、clean session、keepalive 10 秒、随机开发 clientID、重连间隔 1～8 秒。它们不是官方机器人登录参数或 2027 网络预设；DNS/外部 IP 会在连接前拒绝。
- 只有完整成功的 SUBACK 放行；拒绝后保持 `subscription-rejected`，不会把部分订阅当成功，也不会反复请求被拒绝的权限。修正 broker 配置后重启探针或让连接重新建立。等待 SUBACK 时没有数据放行，尚未实现单独的握手超时诊断。
- QoS1 PUBACK 表示收到 MQTT 消息，不表示 Protobuf 有效。无效、retained 和重复消息也确认，防止无限重传；它们不能刷新领域数据。
- 每会话最多缓存 256 个 MID/主题/载荷摘要。已知相同 DUP 计为重复；未知、被淘汰或载荷不一致的 DUP 也丢弃，不刷新新鲜度。**这是保守的探针策略：可能丢掉带 DUP 标记的首次有效送达，不保证应用层至少一次或事件完整性**。正常非 DUP 的 MID 重用和相同数值周期消息允许更新；不能仅按载荷相同就去重。
- 1500 ms 是统一的开发接收窗口，不适合作为所有事件型主题的比赛放行标准；发送端排队延迟和非 DUP 旧数据无法仅靠接收时刻判断。
- 64 KiB 校验发生在 Paho 完整接包以后；它**不等于 MQTT 分配前的资源上限**。未经原生入站长度限制、异常 broker/高频洪泛及正式网络验收，不接真实裁判系统；探针仅检验正常本机 broker 的 QoS0/1 行为，不把异常 QoS2 或恶意 MQTT 帧处理描述为已完成。
- 没有发送者、真实机器人身份确认、处罚遮挡实现或地图自动映射。Qt GUI、现有模拟器和 Release 不因运行这些测试而改变。

## 原生 C++ 本机验收（第三阶段）

`RM27_BUILD_RM2026_PROBE` 默认 OFF，只有明确开启才下载/构建原生依赖。沿用项目 MinGW 13.1.0、Qt 6.11.2、CMake/Ninja；**不能链接 YOLO 环境的 MSVC `.lib`**。protoc 29.3 仅在构建时生成 `client.pb.*`，这些生成文件和依赖源码都留在忽略的构建目录。

依赖固定到可验收版本，非“自动使用最新版”；官方 tag 归档的 SHA-256 写在 `native/CMakeLists.txt`，首次配置校验，后续复用缓存。源码许可证保留在构建依赖目录；本阶段没有打包/发布这些新二进制。

| 依赖 | 固定版本 | 许可证与作用 |
| --- | --- | --- |
| Google Protobuf | 29.3 | BSD-3-Clause，生成消息及官方 C++ 解析/JSON 转换 |
| Abseil | 20240116.2 | Apache-2.0，Protobuf 的原生依赖 |
| utf8_range | 随上述 Protobuf 归档 | MIT，保留其作者/Google 版权声明 |
| Eclipse Paho C | 1.3.14 | EPL-2.0 / EDL-1.0，后者为 BSD 风格；异步 MQTT 3.1.1，未启用 TLS |

从 client 目录构建：

```powershell
. .\scripts\environment.ps1
$rm26Tools = Get-Rm27Environment
& $rm26Tools.cmake -S . -B build-rm2026-native -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$($rm26Tools.ninja)" `
  "-DCMAKE_CXX_COMPILER=$($rm26Tools.compilerDirectory)/bin/g++.exe" `
  "-DCMAKE_C_COMPILER=$($rm26Tools.compilerDirectory)/bin/gcc.exe" `
  "-DCMAKE_PREFIX_PATH=$($rm26Tools.qtPrefix)" `
  -DCMAKE_BUILD_TYPE=Release -DRM27_BUILD_APP=OFF -DRM27_BUILD_RM2026_PROBE=ON `
  -DRM2026_PROTOC=D:/YOLOV8/Library/bin/protoc.exe
& $rm26Tools.cmake --build build-rm2026-native --target rm2026_mqtt_probe --parallel 4
& 'D:\YOLOV8\python.exe' tests/test_rm2026_native.py --exe build-rm2026-native/contracts/rm2026-v2.0.0/native/rm2026_mqtt_probe.exe -v
```

源码自动从对应官方 GitHub 仓库的 codeload tag 归档获取，不依赖本机绝对源码路径；网络不通时配置会失败而不是改用未经核验库。完整 CTest 需要再构建默认测试目标；原生 MQTT 测试对端仅用 Python 标准库，**不要求 Paho Python、Google Python 或 descriptor**。Python 只用于测试，不是新 exe 的运行依赖。

运行原生探针（需另有发送精确 Protobuf 主题的本机 broker）：

```powershell
& .\build-rm2026-native\contracts\rm2026-v2.0.0\native\rm2026_mqtt_probe.exe --protocol rm2026-v2.0.0 --host 127.0.0.1 --port 39005
```

`Get-Rm27Environment` 只配置当前终端进程的 DLL 搜索路径。直接复制 exe 给别人仍不够：它依赖 Qt6Core 与固定 MinGW 运行库；Paho/Protobuf/Abseil 在本机该目标中静态链接。可用 `--duration-ms 3000` 做自动结束的有界开发检查；常规运行按 Ctrl+C 停止。

工程行为依据 [Paho C 异步 API](https://eclipse-paho.github.io/paho.mqtt.c/MQTTAsync/html/struct_m_q_t_t_async__response_options.html)，不作为赛事规则来源。此原生参考与 Python 探针的共同语义：仅本机、显式历史版本、无发布方法、14 个已确认主题、不接雷达；空/未知字段/无效/retained/DUP 不成为新鲜读数；默认 1500 ms 为本机接收窗口；断线清空，重连后全部重新订阅。历史合成 HUD 的显式开发投影见第十阶段及以后。

原生路径的特定边界：

- 一次订阅一个主题，全部成功才放行。Paho C 的 MQTT v3 批量 `qosList` 回调不提供列表长度，不能凭猜测读取主题数对应的项；此参考避免该接口。按正常本机 broker 的单主题 SUBACK 验收，未声称完成所有恶意 MQTT 包的结构校验。
- Paho 网络线程只做有界状态更新，主线程每 50 ms 读取副本/输出。收包不打印日志、不进入 GUI；最多保留 14 个主题样本，另有最近 24 条事件历史（见下文第十二阶段），不是完整事件流。
- 连接等待由库的 5 秒 connectTimeout 负责；全部 SUBACK 另设总计 5 秒截止，超时后不接受迟来的成功回调。订阅失败/超时保持诊断状态，需修正 broker 后重连/重启；不会反复请求被拒绝的权限。
- 原生探针对所有 DUP 保守丢弃，不再维护第二套摘要缓存；正常 MID 重用与相同值周期数据允许更新。可能漏掉带 DUP 的第一次有效送达，**不保证应用层至少一次或事件完整性**。
- 数值采用官方 Protobuf JSON 转换，uint64 为字符串、bytes 为 base64；Python 离线工具的 bytes 仍显示 hex。两种诊断输出不是新的 wire 契约，不应直接给 QML 做假定映射。
- 原生路径已在 Paho 包体读取/分配前增加类型与 Remaining Length 限制，并验收部分异常包和洪泛（第十二阶段）。回调内仍保留 64 KiB Protobuf 上限。这不是完整恶意 broker 审计或正式网络验收，**仍不能连真实裁判或开放外部端点**；Python 探针没有获得此原生保护。
- 正常启动/GUI/Release 不依赖这些目标；正式链接前还要补其余主题映射、处罚遮挡、身份确认、时效策略和依赖发布许可证检查。

## 类型化状态映射（第四至七阶段）

原生 `MqttObserver::snapshot()` 返回纯 C++ 值副本，不向领域暴露 Protobuf 或 JSON；每个已映射主题块用 `Received<T>` 保存 `steady_clock` 接收时间。只从已通过入站校验、非 retained/非 DUP、1500 ms 内的当前会话消息构造，缺失字段为 `std::nullopt`，过期块消失。每条消息整体替换该主题，不继承上一条消息的 optional 字段。原生探针 JSON 新增 `state`，便于本机测试；它只是类型化状态的诊断输出，不是给 QML 使用的网络契约。

| 主题 | 类型化块 | 已映射值与限制 |
| --- | --- | --- |
| RobotStaticStatus | self_config | robot_id、max_health、level；按附录二的机器人 ID 1..7/101..107 显示报告的红/蓝身份，其他 ID 为 unknown；不是认证或登录成功 |
| RobotDynamicStatus | self_dynamic | 当前 HP、热量、上一次弹丸射速原值、剩余允许发弹量 permitted_shots；不创建弹仓余量或 speedMps，不猜单位换算 |
| GlobalUnitStatus | units | ally/enemy 相对分组；各组 1/2/3/4/7 血量、基地 HP/状态/护盾、前哨 HP/状态；不添加空中血量或最大 HP |
| GlobalLogisticsStatus | economy | 本方 remaining_economy；不伪造敌方经济 |
| GameStatus | game | 局号、总局数、双方得分、阶段代码/已知阶段、剩余/已过秒数、暂停；原始结果/结束原因保留，但有效 result/end_reason 仅在结算阶段及已知范围内提供 |
| PenaltyInfo | penalty | 原始类型、已知判罚种类、效果时长、数量；无解除/允许操作标志，不推断遮挡或扣血行为 |
| RobotModuleStatus | modules | 11 个模块各自保留 code 与 state；0 offline、1 online、2 installation_offline，其他值 unknown；缺失模块不补离线 |
| RobotRespawnStatus | respawn | 待复活标志、所需/当前读条、金币费用、reported_can_free/pay 报告资格；缺失为未知，不创建复活权限或操作 |
| Event | latest_event | 事件原始 int32 编号、已知类别与原始 UTF-8 参数；缺失/未知编号为 unknown，仅保留最新收到的样本，不按参数变更游戏状态 |
| RobotPosition | position | 世界 x/y/z 原值、yaw_deg（度，正北为零）、reported_robot_id；缺失不补零或本车 ID，非有限浮点在入站拒绝，不自动映射到地图 |

前哨依据 §2.2.4（PDF 53-54）：状态 0..2 归为 alive，3..5 归为 destroyed，原始状态保留以区分无敌/旋转/停转/重建。仅有 HP 时按 0/正值归类；两者矛盾或状态超出已知枚举时为 unknown，不能把缺失当作死亡。

**项目决策**：收到的静态机器人 ID 改变（含首次确立、从有到缺失）会清空此前所有主题样本，再保存新静态消息；等待新单位/动态/经济包，而不是把旧包挂到新身份。身份未明确时单位数组仍只标 ally/enemy 相对角色，不推断绝对红/蓝或地图坐标。该防护无法识别发送端在同一连接内迟到且没有 ID/序号的旧包，不能作为认证/发送端新鲜度证明。

CustomByteBlock 和未选字段仍只做原始接收诊断；没有将比赛阶段转成可放行状态，未执行判罚遮挡，未做事件历史、坐标标定、模块/复活行为或三项动作推断。本阶段无发布器，仍不接 HUD/正式网络。测试使用人工编写的本机二进制包，不是官方赛事抓包。

### 比赛与判罚的有效性（第五阶段）

2026 通信 PDF §2.2.3（PDF 51-53）定义阶段 0 未开始、1 准备、2 十五秒自检、3 五秒倒计时、4 比赛中、5 比赛结算中；其他/缺失阶段归为 unknown，原始代码仍保留。这是独立历史 `Phase`，不强转为现有 `MatchPhase`。时间为收到的 int32 原值，不夹成 0、不依据本地计时推进阶段；缺失暂停值是未知，合法 false 保留为布尔值。

只有阶段 5 的已知结果 0/1/2（平局/红胜/蓝胜）和结束原因 1..9 才进入有效字段；原始 `reported_result/reported_end_reason` 始终保留，255 或未知值不作为有效结果。每个 GameStatus 整体替换，不把上一阶段或上一条包的胜者/暂停/倒计时继承到新包。

§2.2.15（PDF 63-64）定义判罚 1 黄牌、2 双方黄牌、3 红牌、4 超功率、5 超热量、6 超射速；0 不在此枚举中，不能自创“无处罚”。未知/缺失类型标 unknown，效果时长与数量的合法 0 保留。`effect_sec` 不自动递减，不解释为解除倒计时；1500 ms 接收窗口过后判罚块缺失，**仅表示没有新鲜判罚读数，不是处罚解除、取消遮挡或可以操作的证据**。消息不携带通用事件 ID/源端时间，当前仅保存最近样本，不保证完整判罚历史；正式接入必须另行核对处罚规则与解除条件，不能让读数过期解除处罚。

验收仍运行上一节的 `tests/test_rm2026_native.py`：新增类型化字段、合法零值、缺失替换、五槽次序、独立过期、身份变化、断线清空和前哨状态/冲突检查；Python `-S` 下也可运行，无额外依赖。

第五阶段新增六种比赛阶段、结算范围/255 哨兵、负时间/零值/布尔 false、缺失替换与未知阶段，以及六类判罚/未知类型、零时长/数量、retained/DUP 不刷新、独立过期与身份切换清空检查。所有数据只在本机测试对端送入独立探针；正常终端 exe 未增加协议入口。

### 模块与复活报告（第六阶段）

依据通信 PDF §2.2.12（PDF 60-61），电源、RFID、灯条、17mm/42mm 发射机构、定位、装甲、图传、电容、主控、激光检测共 11 个模块都采用 0 离线、1 在线、2 因安装不规范被视为离线。类型化 `ModuleState` 保留三种已知状态，并保留原始码；未知值不按“非零即在线”处理，缺失模块保持未知。`video_transmission=1` 只表示模块报告在线，不能替代有效解码帧的新鲜度；`main_controller=1` 也不是输入设备连接或控制放行证明。

§2.2.9（PDF 57-58）定义待复活、复活所需总读条、当前读条、能否免费复活、付费所需金币、是否允许付费复活。三个 bool 保留合法 false，读条/费用保留合法 0 和原值；不假设读条是秒或百分比，不夹紧当前读条、不生成本地倒计时。诊断 `reported_can_free/pay` 只是报告资格，不是安全门许可；pending=false 不推断存活，进度完成/费用为零不推断能复活，不发送 CommonCommand。

复用每条消息整体替换、逐主题 1500 ms 接收窗口和会话/身份清缓存策略。过期仅使读数缺失，不推断复活成功或模块恢复。新增本机 wire 测试覆盖全部模块的已知/未知码、缺失替换、复活 false/零值/最大 uint32、超出所需的原始读条、retained/DUP 不刷新、独立过期、断线和身份变化清空；仍可用 Python `-S` 运行。

### 最新事件与位置报告（第七阶段）

通信 PDF §2.2.7（PDF 55-56）定义 Event 的 int32 编号和随编号变化的字符串参数。已知类别按 1..15 映射：击杀、前哨摧毁、大能量机关激活结果、能量机关激活、己方/对方英雄狙击伤害、对方空中支援/被反制、飞镖命中、对方飞镖闸门开启、基地受攻击、对方前哨停转、对方基地护甲展开、对方四级装配请求导致强制退出缓冲、装配结果。编号 0、负数、其他未知值只保留原始 ID 并标 unknown；缺失编号不继承。参数保持原始 UTF-8 字符串，空字符串与缺失有区别；不拆参数、不校验玩法参数、不据事件修改建筑血量/阶段/权限。已知 kind 只表示识别编号，不表示参数合法或事件已执行。将来显示时应作为不可信纯文本，而非富文本/操作指令。

`latest_event` 是最新接收样本，不是完整事件流：同主题新包整体替换，1500 ms 本机窗口过后消失，这不表示历史事件被撤销。当前无通用事件唯一 ID/源端时间，DUP 保守丢弃仍可能漏第一次带 DUP 的送达；无事件顺序、完整历史或应用层至少一次保证，正式消息滚动区接入需另行实现有界交付/去重策略。

§2.2.13（PDF 61-62）定义世界 XYZ 和测速模块朝向（度，正北为 0），未给出本实现可用的 XYZ 单位/原点/地图变换。robot_id 是坐标归属机器人字段；文档注明仅云台手客户端生效，6/106 为空中机器人、7/107 为哨兵，不能把所有位置报告直接当成本车。原始 ID 保留，不替代静态身份/认证；缺失不补本车 ID。仅保留最新位置报告，不拼成全队坐标表或雷达数据；yaw 不自动包裹到某个范围，也不代替云台 yaw。沿用入站有限数检查、独立接收时间和断线/身份清缓存，不标定当前历史地图。

新增本机测试覆盖 15 类事件/未知与负编号、缺失替换、空字符串/中文/嵌入 NUL、未解释参数和非法 UTF-8 不刷新；位置零值/负值/超圈朝向、缺失轴/ID、报告 ID 不冒充本车、四个浮点字段的 NaN/正负无穷拒绝；两类主题的 retained/DUP 不刷新、独立过期、断线及身份变化清空。所有测试仅连接独立环回对端，正常 HUD 未改。

## 本机只读监视窗口（第八阶段）

这是独立 Qt Widgets 开发窗口，不是正常终端的另一种 HUD。`RM2026_BUILD_OBSERVER_VIEW` 默认 OFF，只有先开启历史探针目标才可构建；不进入正常 Release。现有 Qt 6.11.2 安装已含 Widgets，无需另装 Python 或 UI 库。收包仍由 Paho 后台处理，窗口每 50 ms 读取既有诊断报告，只展示 mapper 生成的 `state`，不解析 MQTT/Protobuf、原始 `fresh` 或自定义字节。十类块与来源主题保持显式对应，字段名保留诊断名称，不猜单位/权限。

已有本机原生构建目录时，从 `client` 执行：

```powershell
. .\scripts\environment.ps1
$rm26Tools = Get-Rm27Environment
& $rm26Tools.cmake -S . -B build-rm2026-native -DRM2026_BUILD_OBSERVER_VIEW=ON
& $rm26Tools.cmake --build build-rm2026-native --target rm2026_mqtt_viewer rm2026_observer_view_tests --parallel 4
.\scripts\run-rm2026-observer.ps1
```

首次构建先按第三阶段配置 `RM27_BUILD_RM2026_PROBE=ON`、`RM27_BUILD_APP=OFF`、固定 protoc 和 MinGW，再增加上述 viewer 选项；不要打开正常 `build-gui` 的历史目标。启动脚本仅设置当前 PowerShell 的 Qt DLL 路径，固定协议与字面量环回地址；可用 `-Port 39005` 指定本机测试 broker，不保存连接配置或修改网卡。

没有本机 Protobuf 测试 broker 时，窗口显示 connecting/offline 和 `--` 是预期行为，不会自行生成比赛数据。现有模拟器的 `sim.v1` JSON/39004 **不能直接喂给此窗口**。自动 wire 验收自己建立短生命周期的本机对端，无需人工启动模拟器或 broker：

```powershell
& $rm26Tools.ctest --test-dir build-rm2026-native -R 'rm2026_(observer_view|viewer_mqtt)_tests' --output-on-failure
```

窗口不把 `subscribed` 标为有数据；逐块显示“接收窗口内（非源端实时证明）”“过期 · 旧值已清空”或“无数据 / 会话已清空”。过期立即删除字段，断线清除所有读数与过期标签，不补零、不沿用旧身份。合法 0/false 原样显示，位置不补轴/ID，事件参数作为树单元格纯文本，不解释 HTML 或执行链接。原始 CustomByteBlock 不展示为领域状态；事件仍仅为最新样本，不保证完整历史。1500 ms 只是开发接收窗口，不能据此宣称处罚解除、实时图传、机器人认证或控制可用。

窗口版复用探针源码与 CLI；同一套 14 项 MQTT wire 测试同时验收 console/viewer 两个 exe，包括白名单、拒绝、独立过期、断线/重连和身份清缓存，测试检查无客户端 PUBLISH。另有离屏 Widgets 检查覆盖十块初始未知、零值/false、位置缺失替换、参数纯文本、逐块过期、断线清空与窗口渲染。关闭窗口结束本次订阅，不影响正常终端或模拟器。

## 一键合成报文演示（第九阶段）

从 `client` 目录执行，不必先开原有模拟器或 broker：

```powershell
.\scripts\run-rm2026-demo.ps1
```

第十阶段已将此入口默认目标更新为现有主 HUD 的独立本机测试构建；若尚未构建，脚本会先配置/编译它，不覆盖普通 build-gui 终端。依赖沿用第三阶段的固定原生源码和 protoc 29.3。本机已有 `D:\YOLOV8\python.exe`，测试对端只使用 Python 标准库并加 `-S`，不安装 Python Paho/Protobuf、不修改 CV 环境。其他 Python 路径可通过 `-Python '完整路径'` 指定。常驻“RM2026 本机合成测试 · 非真实裁判”标签；这不是正式客户端或真实赛事数据。开发检查可用 `-Executable 'viewer 完整路径'` 保留旧诊断树演示。

启动脚本兼容 Windows PowerShell 5.1 与 PowerShell 7。包含中文的 `.ps1` 保持 UTF-8 **带 BOM**，避免 5.1 按 GBK/ANSI 误读后出现“字符串缺少终止符”；不要只在 PowerShell 7 中做 AST 检查。回归检查同时校验入口/环境脚本编码，并在 Windows 上使用 5.1 的 `-File` 实际执行整套离屏演示。

默认约 20 秒自动演示并关闭自己启动的窗口：

1. **正常接收**：先完成 11 个精确主题的 SUBACK，再每 500 ms 发送十类人工构造的 Protobuf 样本；例如报告蓝方 ID 101、HP 850、合法 false，以及归属 ID 107 的位置原值（不冒充本车）。样本数值/发送频率是测试选择，不是官方最大血量、频率、坐标标定或比赛记录。
2. **过期**：保持 TCP 连接但停止送包，超过 1500 ms 接收窗口后，所有字段消失，显示过期；不把订阅成功当实时，不把判罚过期当解除。
3. **断线**：关闭本次已知连接；窗口清除旧值与旧过期列表。控制台输出 `offline` 的实际观察结果。
4. **恢复**：等待原生订阅器退避重连，再确认 11 个主题全部重新订阅。改送红方 ID 1 与仅含 HP 500 的动态样本，旧热量/旧允许发弹量必须消失；身份变更只是报告值，不是登录或控制权限。

控制台每个阶段输出一条 JSON 观察摘要（`synthetic=true`、阶段、环回端口、单调计时 `elapsed_ms`、当前类型化块与过期列表）。此计时是本机演示进程经过时间，不是源端时间或端到端网络延迟；摘要不是完整事件/报文录制回放契约。合成 Event 使用未知编号 0 和原始字符串标记，不伪造已确认玩法事件参数。位置故意缺失 z，展示缺失不补零。默认不加载视频；可选的独立测试 TS 视频见第十一阶段，不涉及雷达或控制动作。

演示由 `tests/rm2026_observer_demo.py` 和现有 `tests/mqtt_wire.py` 组成，只用于开发测试；它固定绑定 127.0.0.1 的系统分配端口，不占用 39004/39005，不修改启动设置或导入另一个 simulator 项目。对端只接受该探针的 CONNECT、精确订阅和 PUBACK/心跳；意外客户端 PUBLISH 会失败，不路由控制消息。CLI 只允许 2..6 秒阶段，连接/读包有超时，子程序最多运行 60 秒，成功/失败/中断都会关闭本次连接并回收自己启动的子程序。提前关窗口可能使演示报失败；可用 Ctrl+C 中止。脚本不关闭任何原来就运行的客户端/模拟器。

测试对端的包体限额在读取包体前检查，**不等于已经修复原生 Paho 的分配前限额**；原生适配器仍不得放宽环回限制。自动验收使用同一个演示脚本（离屏运行），并检查完整阶段、真实输出的 optional/零/false、旧字段不继承及参数拒绝：

```powershell
. .\scripts\environment.ps1
$rm26Tools = Get-Rm27Environment
& $rm26Tools.ctest --test-dir build-rm2026-native -R rm2026_demo_tests --output-on-failure
```

没有 `--synthetic-demo` 时，只读窗口仍只观察外部提供的本机测试 broker，不自行产生数据；演示标签只是声明数据来源，不改变协议解析、订阅/过期条件，也不提供模拟绕过入口。

## 本机历史只读 HUD（第十阶段）

目的：验证真正的**MQTT/Protobuf → 类型化状态 → 现有主界面**闭环，而不是只看另一扇诊断窗口。视觉布局保持不变，未知字段不补模拟数值。默认启动命令仍是：

```powershell
.\scripts\run-rm2026-demo.ps1
```

脚本默认使用 `build-rm2026-native/rm27_terminal.exe`，不是已打开的 `build-gui` 窗口。必要时构建；强制更新用 `-Build`。原客户端和独立 simulator 都不用关闭，演示只回收自己启动的窗口与环回连接，约 20 秒自动结束。不要直接双击这个开发 exe：它要求显式 `--protocol rm2026-v2.0.0 --synthetic-demo`，缺失标志、非字面量 127.0.0.1 或非法端口均拒绝启动。短时对端仍在启动脚本中，不在 GUI 内生成数据。

- 左下本车：蓝方英雄 HP850/1000、等级3；恢复为红方 HP500/1000、等级2。
- 顶部队伍：按历史角色 ID 映射到现有照片槽，不把五项数组直接挪到六张照片；无人机 HP 保持 `--`，本车仍从我方队友栏排除。
- 基地：我方5000、敌方4000只是人工样本。报文没有基地最大血量，所以只显示剩余数值与轮廓，不伪造填充比例；普通 sim.v1 的已知最大值血条不变。
- 前哨：展示已核对的 alive/destroyed/unknown，状态码与 HP 冲突保持未知，不单凭 HP 推断。
- 金币/发弹：仅本方剩余金币与允许发弹量；物理弹丸余量、敌方金币保持未知。
- 其他：阶段是只读标签，暂停/计时/处罚不参与放行；未确认单位的射速字段不转成 m/s，位置报告不变成云台角度、雷达或地图点。
- 逐块清值：某个主题过期只撤掉它的显示，其他新鲜块可以继续显示。全部过期和断线均不保留旧血量；恢复精简动态包后，旧允许发弹量/热量不能残留。
- 安全与来源：常驻合成来源横幅与测试标题，接收新鲜不是赛事实时；Simulation/Replay 永久锁定，不能应用正常链路配置或解锁控制。不订阅 sim.v1、不连接真实裁判，不保存正常客户端设置。

`RM27_RM2026_LOCAL_HUD` 默认 OFF。只有与 `RM27_BUILD_RM2026_PROBE=ON`、`RM27_BUILD_APP=ON` 一起开启才有上述开发构建。正常启动器和 Windows 打包显式关闭两项历史选项，发布不增加 Python/Protobuf/Paho 依赖。Python 仅是本次演示的标准库测试对端；原生基本入站防护见第十二阶段，仍不放宽环回限制。

验收使用实际 QML 标签/渲染与实际 wire 场景，而不是只检查诊断树：

```powershell
. .\scripts\environment.ps1
$rm26Tools = Get-Rm27Environment
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:/Windows/Fonts'
& $rm26Tools.ctest --test-dir build-rm2026-native -R 'rm27_historical_hud_tests|rm2026_hud_demo_tests' --output-on-failure
```

QML 检查覆盖照片槽位、血量/基地/前哨/金币标签、缺失/零值、独立清空、红蓝方向、配置/解锁拦截，输出 `pitcure/temp/rm27-preview-<会话>-historical-hud-1600x900.png`。主终端启动记录 `log/rm27-run-*.log`，合成演示另记 `log/rm27-demo-*.log` 的阶段与子程序 stderr；不改变 stdout JSON，也不是完整报文录制。wire 演示使用主 HUD 的同一适配器并校验显示快照，另实跑 Windows PowerShell 5.1 的 `-File` 入口。此构建不是 RM2027 正式兼容性、真实坐标/视频或规则执法的验收。

## 本机测试视频与历史数据并行（第十一阶段）

在客户端目录运行（本机已有原创 H.264 样本；首次更新开发 exe 可加 `-Build`）：

```powershell
.\scripts\run-rm2026-demo.ps1 -VideoTs .\video\console-h264-valid.ts
```

不需要另装 FFmpeg、broker 或 Python 库。发送器用 Python 标准库读取**本地 MPEG-TS 文件**，接收/解码/显示复用现有 Qt Multimedia 后端。文件须是 188 字节包、大小不超过 8 MiB，并足够长以完成测试；任意 MP4 或缺失文件会拒绝启动。TS 样本在 `video/` 本机忽略目录，不作为正式素材分发。本次已验收 H.264 Constrained Baseline / 960×540 / 30 fps，不能由此宣称 H.265、1080p60 或正式比赛图传已完成。

约二十秒自动执行，并只回收自己启动的窗口和端口：

1. `normal`：历史 MQTT 十类数据进入实际 HUD，同时测试 TS 经随机环回 UDP 端口解码，观察到有效 sink 帧。
2. `video_expired`：只停视频，MQTT 继续。最近有效出帧超出项目 700 ms 看门狗窗口后，画面标注过期、旧帧非实时；血量/裁判数据仍新鲜。
3. `video_recovered`：发送器继续文件的时间轴，等待后续关键帧/有效出帧；只有 sink 收到新的有效帧才撤销过期状态。不用“仍在线”或“已发 UDP”冒充解码恢复。
4. `expired`：停止 MQTT 发布，但视频继续出帧，裁判显示独立过期/清空。
5. 视频独立故障检查到此完成，明确停止视频发送；继续 `offline` → `recovered` → `complete`，验证 MQTT 断线清空和红方 HP500 恢复。此时视频没有新帧，仍显示过期，不随数据恢复变成实时。

带视频时启动脚本采用两秒展示阶段；不带参数保持原数据演示。发送线程按 500 kbps 测试速率读文件，不积累发送队列，暂停时不预读、不重播旧包，结束不回卷时间戳。样本过短/卡顿导致独立数据过期阶段无新鲜视频时会报错，不伪造通过。解码和发送均不在 GUI 线程中做阻塞循环。

主 HUD 开发 CLI 的 `--sim-video-port` 只覆盖本次进程的本机测试端口，不保存配置、不打开普通 sim.v1 裁判接收器；它仍要求历史版本和合成确认标志，非法端口拒绝。没有该参数就不启动视频。诊断 `video.stage` 区分测试 TS 出帧与独立 UDP 探针，探针包数为零不表示 TS 未解码。`frameAgeMs` 是最近有效 sink 帧年龄，`recoveryAfterStaleMs` 包含故障等待；**不是源端捕获年龄、恢复发送耗时或端到端延迟**，`endToEndMeasured=false`。原生入站限额未完成，仍不放宽环回限制。

这是项目测试格式，**没有把官方历史 8 字节视频分片重组接到 Qt 解码器**。正式 HEVC 接入仍需确认合法样本、分片起始编号/码流语义，再独立验收；不从这条 TS 路径补猜正式协议。

自动验收需要显式指定合法样本，以免未加载样本却宣称视频通过：

```powershell
. .\scripts\environment.ps1
$rm26Tools = Get-Rm27Environment
& $rm26Tools.cmake -S . -B build-rm2026-native "-DRM27_TEST_TS=$((Resolve-Path .\video\console-h264-valid.ts).Path)"
& $rm26Tools.ctest --test-dir build-rm2026-native -R 'rm27_udp_video_tests|rm2026_hud_video_tests' --output-on-failure
```

前一个用例读取实际 QML 过期提示、保留旧帧、首帧超时与恢复；后一个启动真实历史主 HUD，检查视频与裁判故障互不连带、解码计数递增和全程控制禁用。Windows PowerShell 5.1 的实际 `-VideoTs` 入口另行烟测。没有样本时这两项不登记，不把跳过测试当成解码验收。

## 后续顺序

1. 本机基本入站防护与有限事件消息区已接（第十二阶段）；继续确认生产事件交付、处罚遮挡/解除与阶段安全门，以及完整异常 broker 审计、认证和身份流程。不把当前开发工具直接当作正式比赛入口。
2. 取得合法来源的图传样本，确认分片假设，接 HEVC 解码；测试断流/恢复、首关键帧和旧帧标注。
3. 显式本机测试 HUD 映射已接；继续确认真实雷达 schema 和地图标定，再考虑正式接入及发送适配；所有操作继续经过统一安全门，模拟/回放禁止真实发送。
4. RM2027 最终版到位后逐字段比较，不覆盖 2026 参考文件，不假定兼容。

## 原生入站防护与历史消息区（第十二阶段）

以下上限都是 **项目决策**，不是官方速率或资源常量。只作用于显式启用的原生历史只读目标，环回地址与永久控制锁定保持不变：

- Paho 包体读取/分配前：只接受本配置需要的 MQTT 3.1.1 CONNACK、单主题 SUBACK、PINGRESP、QoS0/1 PUBLISH；拒绝 QoS2/3、无关类型与不合长度。PUBLISH Remaining Length 最大 65604 字节。
- 完整包进入库构造器前：主题 1–64 字节、载荷最多 65536 字节，主题不得含 NUL/通配符，QoS1 MID 不能为 0；控制包另查长度和取值。
- 完整包入站预算：每个固定一秒窗口最多 256 包、包体累计 1 MiB。当前为单历史适配器的进程级上限；部分读取重试不会重复记账。超限/畸形连接断开，旧值与事件历史清空；修正对端后可按既有退避重新订阅。
- `native/paho-ingress.cmake` 核对固定 Paho 1.3.14 `MQTTPacket.c` 的 SHA-256，仅生成构建目录内的有界副本，不改下载的原文件；版本/源码变更时配置失败，须重新审查补丁。保留上游版权及 EPL-2.0/EDL-1.0 声明，使用已核对的 EDL-1.0 许可，不复制依赖源码到本仓库或普通便携包。
- 最近事件最多 24 条；历史参数最多 4096 UTF-8 字节，HUD 最多显示 512 字符，截断处有省略标记。淘汰计数在诊断 `event_history_evicted` 中保留。未知编号原样显示，参数仅纯文本，不解释玩法、不执行指令。
- 轮询只替换同一历史列表，不重复追加；本地 receipt sequence 仅作显示键，不把事件类型当唯一事件 ID。两个相同的非 DUP 事件仍显示两条；retained/DUP 沿用保守拒绝策略，不能承诺完整交付。`latest_event` 过期不撤销已经明确标注的历史消息；身份变化、断线和会话重置清空历史。

验收 `rm2026_ingress_tests` 包含 14 个独立场景：只发送超大长度头而不发送包体、9 类畸形输入、包数/字节洪泛、合法最大载荷、截断断线、重订阅恢复及事件上限/清空。实际 Wire→HUD 演示与 QML 测试另查纯文本、轮询不增条、长参数和断线/恢复只见新事件。验收的不是整个 MQTT 库的安全性；未证明总进程 RSS 硬上限、慢速残缺包截止、认证/TLS、源端时间新鲜度或应用层至少一次交付。Python 参考不自动继承这段原生保护。

### 处罚与雷达为什么还不能接成正式行为

- **confirmed（仅 2026）**：规则 V2.2.0（20260807）§7.1.2，PDF 133–135，区分黄牌累加遮挡、与比赛机制独立计时、红牌后果；黄牌遮挡涉及视频、地图与自定义 UI。§5.6.5，PDF 117–118，飞镖命中遮挡属于另一路机制；§5.6.1，PDF 112–113，部署模式失去图传时小地图/自定义 UI 仍可用。
- **unknown**：通信 V2.0.0 §2.2.15，PDF 63–64 的 PenaltyInfo 只列类型、效果秒数和数量；未确认累加/剩余时长的更新、重复事件与跨重连恢复、权威解除信号。不能让本地倒计时、0、缺字段或 1500 ms 过期解除遮挡。本机历史构建继续只读、永久禁用指令，不宣称规则执法完成。
- **unknown**：§2.2.19，PDF 66–67 的雷达定义仍有 repeated scalar/message 冲突和缺字段号。需官方可编译 proto/更正，以及坐标原点、轴向与场地图标定；不拿 RobotPosition 或社区代码替代。现有 `sim.v1.radar` 的独立优先/600 ms 过期回退/双路过期清空仍是模拟契约，不是正式融合。
- **project decision**：用户本轮暂缓实物图传验收；不新增对 HEVC/正式图传的完成声明。普通预览包只更新现有 UI、日志和本机模拟功能，不包含历史测试目标。另一台未装 Qt/Python 的电脑与正式现场验收仍待执行。

## HEVC 组帧到现有 HUD（第十三阶段）

用户继续授权开发视频接入；本阶段只验证已知历史分片的本机解码链，不把旧硬件 USB 接口假定为 2026 UDP。`RM27_RM2026_VIDEO_HUD=ON` 在独立构建开启，必须显式传入 `--protocol rm2026-v2.0.0 --experimental-video --video-port <本机端口> --fragment-base 0或1`；正常启动/打包为 OFF。与 MQTT 历史 HUD 不同时启用。

`HistoricalVideoReceiver` 用 Qt 工作线程收包，复用 `VideoReassembly`，整帧输出到内部随机环回端口，Qt 自带 FFmpeg 后端解析原始 Annex-B HEVC，既有 QML `VideoOutput` 和透明 HUD 显示。没有外部播放器、硬件串口操作、订阅命令或控制发送；只监听 IPv4 环回。协议头依据 PDF §2（PDF 45），起始号/非末片恰好 1392 B 仍是上文未确认的实验假设，HEVC/Annex-B 样本来自原创测试生成，不是 VT12 抓包。

项目资源策略（非官方常量）：最多一个 2 MiB 在组帧，150 ms 接收截止、250 ms 定时清理/统计；每事件最多处理 256 包，每固定秒最多接受 8192 包，畸形/超大数据报在分配整帧前拒绝。当前会话锁定一个本机源端口，有效输入静默 700 ms 后下一包重置序号/允许新源；此静默重置不是远端身份认证。解码端 UDP FIFO 1024 单位、允许溢出丢弃，不承诺整个 Qt/驱动/内核的 RSS 上限或任意洪泛下的稳定性。

组帧数和解码数分开记录，只有有效 `QVideoFrame` 更新既有新鲜度。最后有效解码帧后 700 ms 标过期，保留旧帧并显示“旧帧非实时”；不是停止发包后精确 700 ms，解码器仍可能有缓存。静默后重新送关键帧可恢复；首次探测中途进入 GOP 可出现参考帧缺失警告，随后重复关键帧恢复，不把这种过程视为首帧/丢包完整性验收。原始流时间基准和帧率由 Qt/FFmpeg 推断，本机 30 fps 样本出现 `25 fps / 30 tbr`，须确认实际低延迟/60 fps 行为后才能考虑生产使用。

开发环境可以用已有、支持 libx265 的 FFmpeg **命令行工具**生成原创测试样本（仅用于测试生成，应用运行不调用它，不随源码/发布分发）：

```powershell
New-Item -ItemType Directory -Force build-video-fixtures | Out-Null
ffmpeg -f lavfi -i testsrc=size=320x180:rate=30 -t 3 -c:v libx265 -preset ultrafast -pix_fmt yuv420p -x265-params 'aud=1:repeat-headers=1:keyint=15:min-keyint=15:bframes=0:pools=1:frame-threads=1:log-level=error' -f hevc -n build-video-fixtures/rm27-test-pattern.hevc
. .\scripts\environment.ps1
$videoTools = Get-Rm27Environment
& $videoTools.cmake -S . -B build-rm2026-video -G Ninja "-DCMAKE_PREFIX_PATH=$($videoTools.qtPrefix)" "-DCMAKE_MAKE_PROGRAM=$($videoTools.ninja)" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DRM27_RM2026_VIDEO_HUD=ON -DRM27_RM2026_LOCAL_HUD=OFF -DRM27_BUILD_RM2026_PROBE=OFF "-DRM27_TEST_HEVC=$pwd/build-video-fixtures/rm27-test-pattern.hevc"
& $videoTools.cmake --build build-rm2026-video --parallel 4
& $videoTools.ctest --test-dir build-rm2026-video -R 'rm2026_(video|hevc)' --output-on-failure
```

HUD 用例需要正常 Windows 图形会话，检查窗口实际彩色像素，不能用无视频渲染的 offscreen/software 截图冒充显示验收；没有 HEVC 样本时专用出图测试不注册。测试检查显式 0/1 起始号、拒绝非法起始号、乱序/重复/超大/旧片/源端口/超时释放/停止收包/重启，以及实际 HEVC 解码、渲染像素、旧帧提示、恢复和永久控制锁定。可选摄像头实物用例仍按既有条件跳过，不属于本次验收。

实际验收范围仅生成的 320×180 / 30 fps 码流。VT02/VT12 直接接入、真实接口/会话、1080p60、长时资源测试、真实首关键帧时延及端到端延迟全部未完成。Qt 工程依据：[QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html)、[QVideoSink](https://doc.qt.io/qt-6/qvideosink.html)；没有复制第三方开源代码、官方二进制或赛事码流到源码。

## 面向公开网络接口的视频模块（第十四阶段）

本节更新第十三阶段的限制：当前实物虽是 VT02/VT12，模块以用户提供的 **2026 V2.0.0 官方网络视频接口**为目标，不等待旧 USB 接口资料再开展组帧/解码开发。未来 VT03/VT13 联调仍须确认官方输出端配置；没有把 2026 参数认定为 RM2027 最终协议，也没有给 VT12 USB 增加驱动。

### 开源核对与实现选择

以下为本轮实际阅读的具体入口；只参考设计，未复制代码或引入仓库依赖：

- [ClearWei/RM26_Client VideoReceiver.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/network/VideoReceiver.cpp) / [HevcDecoder.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/network/HevcDecoder.cpp)（MIT）：参考 UDP 分片重组、VPS/SPS/PPS 缓存、等待随机访问图像恢复。未照搬其直接 FFmpeg/QImage 解码显示；本项目继续使用既有 Qt Multimedia 和 HUD。组帧字节数或索引不完整时丢弃，不尝试解码半帧。
- [OpenRoboSouls/vtstream-ffplay main.rs](https://github.com/OpenRoboSouls/vtstream-ffplay/blob/master/src/main.rs)（Apache-2.0）：确认简易实现是 UDP 去掉 8 字节头后送 HEVC 解码器；本项目不采用直接逐包转发、外部 ffplay 子进程，先检查并组好帧。
- [stydxm/RMMock udp_client.go](https://github.com/stydxm/RMMock/blob/main/pkg/stream/udp_client.go) / [ffmpeg_encoder.go](https://github.com/stydxm/RMMock/blob/main/pkg/stream/ffmpeg_encoder.go)（AGPL-3.0）：参考 8 字节大端头、0 起始分片和 libx265 模拟输出。它是测试流生成端，不是硬件驱动，其分片起始号不能替代官方说明。
- HEVC NAL 类型核对 [FFmpeg n7.1 hevc.h](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/hevc/hevc.h) 中对 H.265 表7-1的枚举：IRAP 16–21，VPS/SPS/PPS 32/33/34，CRA 21，RASL 8/9，RADL 6/7。只引用标准编号，未复制 FFmpeg 实现。

### 接收、恢复与资源边界

公开常量仍来自本地通信 PDF §2（PDF45，印刷44）：UDP3334、HEVC、1080p60、最大1400字节包/1392字节载荷，以及2/2/4字节大端头。**最大载荷不是所有非末片必须恰好1392字节**；组帧已支持变长载荷，按连续分片索引排序拼接，并核对总长度。缺索引、冲突重复、总长度冲突和超时均不交给解码器。

项目决策：只保留最新一个在组帧，收到的载荷合计最多2MiB、最多2048片；不因报文声明2MiB就预先分配2MiB。150ms组帧截止、250ms统计/清理、每事件256包、固定秒8192包上限沿用。源IP必须明确且固定，当前会话还锁定源端口；合法来源静默700ms后下一包开启新会话，清除旧序号和参数集。源IP过滤不是身份认证，UDP没有重传。

`HevcSyncFilter` 检查 Annex-B 起始码和基础NAL头；首次接入、组帧丢失/超时、完成帧序号跳变或参数集改变后，等待完整参数集及IRAP图像，再向既有Qt解码入口补入参数集。CRA恢复时抑制依赖缺失参考的RASL，RADL不解除抑制。不同会话不继承参数集。单个参数NAL最多64KiB、单访问单元最多1024个NAL；恢复输出最多比输入多192KiB参数缓存。只支持层0、每类一个活动参数集；不是完整HEVC语法验证器，也不保证多参数集ID/多层码流兼容。分片起始号0/1仍需显式选择，Annex-B封装及每个协议帧对应一个访问单元也仍待真实输出样本核验。

日志把 `packets`、`assembledFrames`、`forwardedFrames`、`decodedFrames` 分开记录，另有 `waitingForKeyframe`、`droppedDecodeUnits`、`invalidHevcUnits`、重复/丢片和缓存字节数。不把参数包、组帧成功或UDP在线标为有实时画面；实际有效QVideoFrame才更新既有新鲜度。所有网络解析在工作线程，解码复用Qt后端，QML文件/布局/配色未改。

### 独立开发入口

本机无真实发送端测试（默认环回，不启动任何发送器）：

```powershell
.\scripts\run-rm2026-video.ps1 -Build -VideoPort 39006 -FragmentBase 0 -ExperimentalVideo
```

为后续实物网络联调预留的参数示例，**不是已经验证的设备配置**；先确认电脑的有线IPv4、官方实际发送IP，以及分片起始号。这里两个IP仅为占位示例，不要照抄为网卡设置：

```powershell
.\scripts\run-rm2026-video.ps1 -VideoPort 3334 -FragmentBase 0 -ExperimentalVideo `
    -BindAddress '192.168.10.2' -SourceAddress '192.168.10.1' -AllowNetworkVideo
```

等价开发CLI增加 `--video-bind`、`--video-source`、`--allow-network-video`。绑定必须是具体IPv4，拒绝0.0.0.0/广播/组播、IPv6及环回与非环回混用；非环回须显式允许。端口占用/非法配置失败退出，不关闭其他进程，不修改网卡、系统PATH、驱动、固件、防火墙、官方配置或start.bat。此入口只收视频，不发送订阅/登录/控制请求；仍是默认OFF的独立开发构建，历史MQTT和正常发布入口限制不变。

验证命令：`ctest --test-dir build-rm2026-video --output-on-failure`。包含纯组帧、HEVC同步筛选、真实本机UDP收发及既有HUD实际HEVC解码/渲染、断流/恢复和控制永久锁定。硬件接口、真实官方输出会话、1080p60、低延迟、分辨率切换和长时资源稳定性尚未验收；目前不是可上场客户端。

## 数字机器人身份的本机验收（第十四阶段）

对照开源的下一小步是 **MQTT CONNECT 的 clientID**，不是发送无线频率/配对码。原生 probe/viewer 新增 `--robot-id`，启动脚本提供 `-RobotId`；不传则保留原有随机开发 ID。依据本地通信 PDF §2（PDF45）的 clientID 要求及附录二（PDF80–81），此探针仅开放六个选手角色：红方 `1/2/3/4/6/7`、蓝方 `101/102/103/104/106/107`。这是当前入口的选择范围，不是完整设备 ID 列表。

诊断 `client_id` 表示本次请求的 MQTT 身份；只有有效收到的 `RobotStaticStatus` 才能提供 `state.self_config`。连接/订阅成功不等于机器人登录授权、硬件对频或视频出帧。重连保留请求 ID，但清空旧状态并重新订阅全部11个主题。没有 PUBLISH/控制或 USB 命令，Python 探针、合成演示及普通 VT12 启动入口不因此改变。

从 client 目录执行以下本机验收，无需打开或退出官方程序，不占用3333/3334/8080、不改变网络配置：

```powershell
Set-Location -LiteralPath 'D:\vs\vscode\c++\huge mas\Custom_Client\client'
. .\scripts\environment.ps1
$rmIdentityTools = Get-Rm27Environment
& $rmIdentityTools.cmake --build build-rm2026-native --target rm2026_mqtt_probe rm2026_mqtt_viewer --parallel 4
& 'D:\YOLOV8\python.exe' -S .\tests\test_rm2026_native.py `
    --exe .\build-rm2026-native\contracts\rm2026-v2.0.0\native\rm2026_mqtt_probe.exe `
    NativeChecks.test_numeric_identity_reconnect_and_no_publish `
    NativeChecks.test_invalid_numeric_identity -v
```

测试使用自己创建的环回临时端口，对红3/蓝3检查 CONNECT 原始字节、拒绝连接、保持身份重连、全部SUBACK、断线清空和不发布；另拒绝无效/非规范身份。只有已部署自己的本机二进制测试 broker 时才运行 `scripts/run-rm2026-observer.ps1 -Port 39005 -RobotId 3`；此脚本不启动 broker，当前没有服务时只会等待/离线，不会产生赛事数据。

实现参考：[ClearWei/RM26_Client 固定版本 MqttManager.cpp](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/MqttManager.cpp)（MIT，Fudan University EGA Team）将传入的 clientID 交给 Paho，保存身份用于重连；同版本 `NetworkManager.cpp` 在连接成功后订阅主题。本项目只借鉴连接/订阅分工，不复制源码，不导入其控制发布或用开源填补协议缺口。

真实联调仍缺可用的官方 MQTT 服务及客户端身份登记证据。2026-10-07 本机检查到 RMServer 和 RoboMasterEngine 运行，但 TCP3333 没有监听；用户截图的主裁判12.0.0.137和服务器12.1.1.510版本不同，单凭版本差异不能判定兼容性，也不能将其他监听端口猜作 MQTT。须先按对应官方部署说明确认服务，再单独验收真实网络边界；本阶段保持字面量127.0.0.1限制。VT12的已验证H.264/8080视频和官方配对后台继续独立，不因 CONNECT ID 改动实现免官方配对。

## 无硬件的 MQTT + HEVC 只读闭环（第十五阶段）

身份连接、订阅、类型化快照和 HEVC 接收现在可接到**同一个原 HUD**；普通 VT12 自动接收和独立网络视频入口不变。第十一阶段 TS 演示仍保留；下面的 HEVC 演示才经过历史8字节分片重组。

本机已有原创320×180/30fps HEVC样本时：

```powershell
Set-Location -LiteralPath 'D:\vs\vscode\c++\huge mas\Custom_Client\client'
.\scripts\run-rm2026-demo.ps1 -Build -RobotId 3 `
    -HevcFile .\build-video-fixtures\rm27-test-pattern.hevc -FragmentBase 0
```

不需要硬件、官方客户端、赛事引擎或另开broker；约16–25秒自动回收本次窗口和连接。`-Build`更新独立开发构建，不覆盖普通build-gui。文件是本机生成物，不随发布分发；缺文件报错、不下载样本。只支持带AUD、VPS/SPS/PPS、IDR的原创Annex-B测试文件（最多8MiB、每单元最多1MiB），发送器按测试30fps循环单元；不是任意媒体播放器或设备模拟器。与 `-VideoTs` 互斥。样本生成见第十三阶段。

| 对标环节 | 本机已接通 | 未完成的实物部分 |
| --- | --- | --- |
| 身份连接 | 规范数字clientID，重连保留ID | 登录授权、USB接收角色设置 |
| MQTT/Protobuf | 11主题SUBACK、十类人工状态进入原快照/HUD | 真实赛事服务、开源全部主题/雷达/控制 |
| 分片HEVC | 大端2/2/4头、反序/重复分片、完整组帧、原播放器出帧 | VT13实物1080p60、物理延迟 |
| 独立恢复 | 视频停流/恢复时MQTT持续；MQTT过期/断线/恢复时视频持续 | 无线配对、官方视频输出会话 |
| 安全与来源 | 常驻本机合成标签、控制永久禁用、临时环回端口 | 比赛验收、处罚解除、真实网络许可 |

测试CONNECT请求红3，但人工报告先蓝1、再红1，证明请求身份不伪造服务器报告。`client_id`是请求值，`state.self_config`是有效报文报告值；`video.transport`显示组帧/同步统计，`video.decodedFrames`来自有效sink帧。绿灯、绑定或包数不是实际出图，`endToEndMeasured=false`。

### 有包但无法解码：可选故障验收

已有上述构建与原创样本后运行：

```powershell
.\scripts\run-rm2026-demo.ps1 -RobotId 3 `
    -HevcFile .\build-video-fixtures\rm27-test-pattern.hevc -FragmentBase 0 -VideoFaults
```

仅测试对端注入故障，不改变正式接收器、硬件或UI。约20–30秒自动结束；省略`-VideoFaults`保留原流程。该开关只允许与`-HevcFile`一起使用，Python等价参数为`--video-faults`。

在原视频停流/恢复之后增加两个阶段：

- `video_damaged`：一次合法分片头包携带坏HEVC头，持续发送截断报文，并故意丢掉后续每个关键帧的一片；完整依赖帧仍持续到达。`packets/assembledFrames`继续增加，`rejectedPackets/invalidHevcUnits/droppedFrames`记录故障，`waitingForKeyframe=true`；有效sink帧不再增长、旧画面超过700ms后过期。MQTT人工状态保持新鲜。
- `video_keyframe_recovered`：重新发送完整参数集/IDR，等待新有效sink帧增长后才算恢复；不能仅凭收包数或解除同步等待宣布出图。

后续仍检查MQTT过期/断线/重订阅不会停止独立视频，全程禁止客户端PUBLISH及控制解锁。700ms阈值、故障注入及测试节奏为项目决策，不是官方重传或设备配对机制；本机样本不证明1080p60、实物无线链路或物理低延迟。组合HEVC自动测试也覆盖这两个阶段。

开发CLI新增 `--robot-id`、`--hevc-video-port`、`--fragment-base`，仍要求 `--protocol rm2026-v2.0.0 --synthetic-demo`，MQTT和视频仅环回；起始号0/1是显式实验选择，不推断硬件默认。合成对端只服务自己启动的进程，核对CONNECT实际字节与全部重订阅；意外客户端PUBLISH报错，不处理任何控制。不编进正式客户端，不是可部署裁判服务器。

自动验收（有合法样本才登记组合视频测试）：

```powershell
. .\scripts\environment.ps1
$rmCombinedTools = Get-Rm27Environment
& $rmCombinedTools.cmake -S . -B build-rm2026-native "-DRM27_TEST_HEVC=$((Resolve-Path .\build-video-fixtures\rm27-test-pattern.hevc).Path)"
& $rmCombinedTools.cmake --build build-rm2026-native --parallel 4
& $rmCombinedTools.ctest --test-dir build-rm2026-native --output-on-failure
```

参考 [RM26_Client MqttManager](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/MqttManager.cpp)、同版本NetworkManager和[VideoReceiver](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/VideoReceiver.cpp)（MIT，Fudan University EGA Team）：借鉴连接/订阅与独立视频、完整组帧/关键帧恢复的分工，复用本项目模块，未复制源码/UI或增加依赖。协议依据仍为本地2026通信V2.0.0 §2及各主题定义；测试速率、样本格式和故障阶段为项目决策。真实服务地址/登记、VT13输出和物理配对待有硬件及官方依据后另验，不猜USB/串口命令。

## 比赛概要的既有状态条投影（第十六阶段）

独立历史 HUD 现在将已解析的 `GameStatus` 局数、红蓝比分、阶段剩余/已过秒数与暂停投影为只读比赛概要。复用 `TerminalController::alertText` 的既有状态条和“状态”按钮悬浮提示；没有添加 QML 控件或改变布局。状态条空间不足时沿用原省略行为，收起状态条后将鼠标停在“状态”按钮可查看完整文本。

来源仍是 2026 通信 V2.0.0 §2.2.3（PDF 51–53）和第五阶段的类型化映射，字段的 wire 定义、订阅主题和 1500 ms 接收窗口不变。显示 `current_round/total_rounds`、`red_score/blue_score`、`stage_countdown_sec/stage_elapsed_sec`、`is_paused`；局数/得分不截成 8/16 位，时间保留收到的 int32 原值（包括零和负数），缺失用 `--`，暂停分别为“已暂停 / 未暂停 / 暂停未知”。没有本地递减、阶段推进、胜者推断或控制放行。

每次投影替换整个比赛概要，不继承上一条包的值。比赛块缺失/过期时显示“比赛数据未知 / 已过期”，即使其他主题仍新鲜也不保留旧比分；断线忽略残留快照，重连后的缺字段继续未知。阶段仍只进入历史标签，历史控制器保持 Simulation/Replay。模块/复活/处罚未接入此概要，仍不能根据模块在线、资格或读条推断权限。

合成 HUD 的诊断 JSON 新增 `hud.alert`，直接记录同一 C++ 文本，用于字节→类型化快照→显示投影的验收；QML 不读取这个 JSON。人工 GameStatus 样例增加显式红分 0、蓝分 2、已过 0，其他字段保留局 1/3、剩余 420 和暂停 false；重连后的 GameStatus 只报告阶段 4，检查旧比分/时间/暂停不被继承。这是假数据，不是规则限额或比赛记录。

验收复用已有目标：

```powershell
. .\scripts\environment.ps1
$rmGameTools = Get-Rm27Environment
& $rmGameTools.ctest --test-dir build-gui -R '^rm27_historical_hud_tests$' --output-on-failure
& $rmGameTools.ctest --test-dir build-rm2026-native -R '^rm2026_hud_demo_tests$' --output-on-failure
```

`historicalGamePresentation` 检查零/负时间/false/未知、完整整数宽度、不自动倒计时、逐块清值、断线/重连及既有状态条/悬浮文本绑定。原生鼠标悬浮弹出和截图在 Windows 后端单独检查；offscreen 检查数据与绑定，不能当作原生鼠标验收。合成 MQTT 验收检查实际报文解出正确概要、过期/断线清空，以及重连后没有继承旧比分/时间/暂停值。

参考 [RM26_Client GameData::updateGameStatus](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/core/GameData.cpp) 的赛事状态显示思路（MIT，Fudan University EGA Team）；未复制代码，也不采用其 uint8/uint16 收窄或将缺失标量视为零的做法。选择既有文本入口、测试和保留未知是本项目决策，不新增真实服务接入或依赖。

## 第十七阶段：增益、性能体系与部署下行报告（本机只读）

按开源对标 T03 补齐三个精确主题。依据本地通信 V2.0.0 §2.1（PDF 47、49）及 §2.2.14/24/27（PDF 62–63、69、70），三个主题均为服务器向客户端、最高 QoS1。仍默认关闭、显式选择历史版本、仅字面量 127.0.0.1。

| topic | 官方字段（编号 / 类型，均 optional） | 类型化诊断块 |
| --- | --- | --- |
| Buff | robot_id 1/uint32，buff_type 2/uint32，buff_level 3/int32，buff_max_time 4/uint32，buff_left_time 5/uint32 | latest_buff：原 ID、类型、带符号增益值、两项原时间及已知类型名称 |
| RobotPerformanceSelectionSync | shooter 1/uint32，chassis 2/uint32，sentry_control 3/uint32 | performance：三个原始报告码 |
| DeployModeStatusSync | status 1/uint32 | deployment：原码及 not_deployed / deployed / unknown |

Buff 仅保留最新报告，不代表所有机器人全部生效增益；ID 缺失或零不归到本车。类型 1–7 对应攻击、防御/易伤、冷却、底盘功率、回血、可兑换允许发弹量、地形跨越预备，其他码保留且名称 unknown。负增益/零完整保留，时间原值不本地递减、不补单位、不推断解除；字段 6 不在官方定义中，仍作为未知字段处理。性能体系保留原码，不产生权限或本地请求确认。部署只识别 0/1，其他码 unknown，不操作遮挡、地图或部署指令。

复用整条消息替换、逐主题 1500 ms 过期、身份变化/断线/重连清空。全部 14 个 SUBACK 后才接收，空包/未知字段包/损坏包/retained/DUP 不续期。缓存最多 14 个主题样本，原载荷 64 KiB、包体分配前限制和固定一秒入站预算不变。三个新块只接独立诊断树（10→13 块），不连接主 HUD。

`test_rm2026_protocol.py` 核对官方字段编号/类型/presence 与人工黄金字节；原生 MQTT 检查三类映射、全部已知 Buff 类型、未知码、负值、整数宽度、缺失替换、无效包不续期、独立过期、身份变化和重连。合成恢复阶段只报告 Buff 增益零、sentry_control 99、部署 99，旧字段不继承。probe/viewer/入站资源/合成 HUD/HEVC 演示沿用既有验收目标，样本均为人工数据。

参考固定 [RM26_Client NetworkManager](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/network/NetworkManager.cpp) 的订阅/分派和 [GameData](https://github.com/ClearWei/RM26_Client/blob/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af/src/core/GameData.cpp) 的 Buff/部署展示思路（MIT，Fudan University EGA Team），未复制源码。该版本性能同步已列入订阅，但 NetworkManager 未见相应分派；本项目按官方字段实现。T03 仍待完成多机器人/多类型 Buff 汇总、主 HUD 展示及特殊机制/伤害等其他下行主题。
