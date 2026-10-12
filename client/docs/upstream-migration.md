# 固定开源后端迁移清单（M0–M3，2026-10-11）

方向：保留本项目 UI 设计，按 ClearWei/RM26_Client 的后端逐段迁移。M0 已完成源码固定和协议核验；M1 已接入普通客户端的 MQTT→GameData→现有 HUD。M2已接普通HEVC链路，M3已接异步输入/消息与必要业务约束，完整权限和硬件验收仍待后续。

## 1. 来源、回退与工具链

| 项目 | 固定值 / 证据 |
| --- | --- |
| 当前工作分支 | `codex/upstream-backend-migration` |
| 本项目回退提交 | `98f6448a6fa93a318ade1bef9ad61df31f5c0f4c` |
| 已上传的备份分支与标签 | `codex/checkpoint-before-upstream-20261011` / `checkpoint-before-upstream-20261011` |
| 开源仓库与提交 | [ClearWei/RM26_Client](https://github.com/ClearWei/RM26_Client/tree/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af)，`e291f2f65f5a5924509e4e2fe1b148ff9d4b24af` |
| 用户指定的源码目录 | `D:/vs/vscode/c++/huge mas/开源/RM26_Client-e291f2f` |
| 获取方式 | Git fetch 连接失败后，下载该提交的完整 codeload ZIP；不是浮动 main，不含 Git 历史 |
| 源码核验 | ZIP 完整性检查通过；1079 个文件逐一匹配该提交的 Git blob SHA-1 |
| ZIP SHA-256 | `44e9373438758b4167b02b8b407c35b6074555e72aa14f5c70c1f56e0a27f443` |
| 本机来源记录 | `D:/vs/vscode/c++/huge mas/开源/RM26_Client-e291f2f.source.json` |
| 许可证 | MIT；Copyright (c) 2025-2026 Fudan University EGA Team（复旦大学星云 EGA 机器人战队）；复制时保留 LICENSE 和文件版权声明 |
| 官方协议依据 | `规则/RoboMaster 2026 机甲大师高校系列赛通信协议 V2.0.0（20260626）.pdf`，§2、§2.1、§2.2；地图限频另见 §1.3.1 |
| 项目工具链 | Qt 6.11.2 / MinGW 13.1.0 / CMake 3.30.5 / Ninja 1.12.1，见 `toolchain.json` |
| 已有原生依赖 | Protobuf/protoc 29.3、Abseil 20240116.2、Paho C 1.3.14；固定下载 SHA-256 见 `contracts/rm2026-v2.0.0/native/CMakeLists.txt` |
| M2 依赖 | 已固定 FFmpeg7.1.5 LGPL公开头/匹配Qt运行库，详情见M2记录；不依赖DJI安装目录 |

完整源码只放在用户指定的开源目录；项目不提交这份约 584 MiB 的上游素材和依赖。迁移时只复制被实际调用的后端文件到项目内，发布与编译不能要求这个外部目录存在。

## 2. 协议逐项对照

官方 §2.1 有 **36 个主题：25 下行、11 上行**。本项目 canonical schema 从 17 个扩展为 **35 个已核验消息**，保留 singular optional；`RadarInfoToClient` 外层仍缺可确认的字段编号/类型。定义存在不代表已订阅、映射或允许发布。

逐字段编号、名称、类型、presence、方向、QoS、频率及开源字段差异保存在 `contracts/rm2026-v2.0.0/upstream-audit.json`。该文件由 protoc descriptor 对照生成；35 个消息的全部字段另与官方 PDF 示例核对，包括 PDF 排版产生的空格。下面列出每条消息的业务与 UI 落点。

下表“已有”指 M0 审计时的独立只读观察器，保留其基线含义；M1 运行接入状态见文末。所有上行仍须在 M3 接入单一指令门和本机接收端验证。

| §2.2 | 消息 / 方向 | QoS 上限 / 频率 | 当前入口、迁移落点与验证 |
| --- | --- | --- | --- |
| 1 | KeyboardMouseControl ↑ | 1 / 75Hz | 离线定义；提取 MainWindow 输入采集，接现有输入接口；核对键位、鼠标增量、释放和严格限频 |
| 2 | CustomControl ↑ | 1 / 75Hz，最多30B | 离线定义；有类型字节动作接口；核对长度、来源隔离，不擅增 UI |
| 3 | GameStatus ↓ | 1 / 5Hz | 已有；比赛条、局数/比分/暂停；零秒、负时间、缺字段、独立过期 |
| 4 | GlobalUnitStatus ↓ | 1 / 1Hz | 已有；双方五槽血量、基地/前哨；严格十项己方/敌方顺序，无无人机 HP，无基地 maxHP 推测 |
| 5 | GlobalLogisticsStatus ↓ | 1 / 1Hz | 已有；金币/经济；保留 uint64 总经济，不截断为 UI 小整数 |
| 6 | GlobalSpecialMechanism ↓ | 1 / 1Hz | 新定义；领域机制列表，展示入口登记；平行数组长度、负时间，不猜本地倒计时 |
| 7 | Event ↓ | 1 / 触发 | 已有；现有纯文本消息；限长、限条数、重连清历史、不重复追加 |
| 8 | RobotInjuryStat ↓ | 1 / 1Hz | 新定义；受伤统计；一次存活期含义、缺失项、不猜击杀或复活成功 |
| 9 | RobotRespawnStatus ↓ | 1 / 1Hz | 已有；复活资格与动作提示；读条不猜百分比，pending=false 不等于复活成功 |
| 10 | RobotStaticStatus ↓ | 1 / 1Hz | 已有；报告身份、配置/最大值；clientID=3 与报告 ID 分开，缺 ID 不认领本车 |
| 11 | RobotDynamicStatus ↓ | 1 / 10Hz | 已有；血量、热量、剩余允许发弹量；false/0、整包替换、非有限数 |
| 12 | RobotModuleStatus ↓ | 1 / 1Hz | 已有；模块诊断；原始0/1/2和未知码，图传在线不证明实际出帧 |
| 13 | RobotPosition ↓ | 1 / 1Hz | 已有；领域世界坐标；不直接接雷达/云台，坐标原点、轴向及地图标定待确认 |
| 14 | Buff ↓ | 1 / 获得时触发后1Hz | 已有；最新报告；归属ID、未知增益码、不推断持续生效或控制放行 |
| 15 | PenaltyInfo ↓ | 1 / 触发 | 已有；处罚诊断/规则门；解除与重连恢复仍待权威语义，零/缺失/过期不解除 |
| 16 | RobotPathPlanInfo ↓ | 1 / 触发 | 新定义；轨迹领域数据；分米、49项数组、-128..127偏移与地图标定，未确认前不绘制现场轨迹 |
| 17 | MapClickInfo ↓ | 1 / §1.3.1 | 新定义，上游漏订阅；接标记状态，有来源与过期；mode/type 分别按官方字段解释 |
| 18 | MapClickCmd ↑ | 1 / §1.3.1 | 新定义；现有地图动作在坐标确认后接入；云台手≥0.5s，半自动机器人操作手≥3s，角色未确认不发送 |
| 19 | RadarInfoToClient ↓ | 1 / 触发 | 阻塞其结构解码；官方 PDF66-67 示例外层不完整，开源自定嵌套 field1 不能当官方依据；不阻塞其他24条下行 |
| 20 | CustomByteBlock ↓ | 1 / 50Hz，最多300B | 已有；有界自定义字节诊断；上游私有 H.264 解码用途不等于官方 HEVC 图传，不启用第二套主画面 |
| 21 | AssemblyCommand ↑ | 1 / ≤10Hz | 新定义；工程装配动作接口；operation=0/1/2、difficulty 范围及回执；本 UI 无工程控件，登记缺口 |
| 22 | TechCoreMotionStateSync ↓ | 1 / 1Hz | 完整类型化8字段；最大难度/步骤用于必要检查，敌方核心及剩余时长保留原值/缺失和有效范围，不倒计时或推断请求结果 |
| 23 | RobotPerformanceSelectionCommand ↑ | 1 / ≤10Hz | 新定义；性能选择动作；机器人类型与取值校验，发布不等于选择生效 |
| 24 | RobotPerformanceSelectionSync ↓ | 1 / 1Hz | 已有；性能报告；与请求结果分开、缺字段保持未知 |
| 25 | CommonCommand ↑ | 1 / ≤10Hz | 离线定义；补弹/治疗/复活等有类型动作；逐 cmd_type/param 核验、身份/阶段/费用门 |
| 26 | HeroDeployModeEventCommand ↑ | 1 / ≤10Hz | 新定义；英雄部署动作；mode=0退出/1进入，其他类型不发送 |
| 27 | DeployModeStatusSync ↓ | 1 / 1Hz | 已有；部署状态；0/1与未知码，不错误遮挡地图或 UI |
| 28 | RuneActivateCommand ↑ | 1 / ≤10Hz | 新定义；激活动作；activate=1，不把发送成功当已激活 |
| 29 | RuneStatusSync ↓ | 1 / 1Hz | 类型化机关状态/灯臂/原始环数；平均环数只在大机关正在激活有效，灯臂不等于机会 |
| 30 | SentryStatusSync ↓ | 1 / 1Hz | 类型化哨兵姿态/弱化/强化；姿态1/2/3、未知码、两个bool的false与缺失 |
| 31 | DartCommand ↑ | 1 / ≤10Hz | 新定义；飞镖动作接口；目标1..5、开闸与确认分开，现有重装 UI 无入口 |
| 32 | DartSelectTargetStatusSync ↓ | 1 / 1Hz | 类型化目标/三态闸门报告；open=0/1/2，未知码不夹取，不能按bool解释 |
| 33 | SentryCtrlCommand ↑ | 1 / ≤10Hz | 新定义；哨兵动作；指令号白名单，旧编号和未知编号不猜 |
| 34 | SentryCtrlResult ↓ | 1 / 触发 | 新定义；M3.3类型化收到的哨兵结果；0与缺失分别保留，未唯一关联本地请求 |
| 35 | AirSupportCommand ↑ | 1 / 触发 | 新定义；空中支援动作；command_id=0..3含付费解除反制；官方本表无数值限频，上游1Hz是项目决策 |
| 36 | AirSupportStatusSync ↓ | 1 / 1Hz | 新定义；M3.3类型化状态/免费时间/已花金币/反制；费用必要检查不自动付款或放行 |

## 3. 不能原样复制的差异与处理顺序

| 证据（路径均相对开源目录） | 差异 / 后果 | 迁移处理与验收 |
| --- | --- | --- |
| `src/network/NetworkManager.cpp::subscribeToTopics` | 26次订阅、25个不同主题，Event重复；漏MapClickInfo、多CustomRobotData | 按已核验官方集合订阅；雷达只登记诊断，扩展主题不默认订阅；核对每个SUBACK |
| `src/network/NetworkManager.cpp::sendMapClickInfo`；官方修改日志PDF2 | 仍发布MapClickInfoNotify，官方V1.3.1已拆为MapClickInfo/MapClickCmd | 接收/发送主题分别修正；字段3 mode是标记类型，字段6 type是标记模式，开源注释相反；不能只改字符串 |
| `src/network/proto/robomaster.proto` | 多数标量implicit presence；既有GameStatus/Buff等少数已optional | 沿用本项目官方转录的optional；逐字段零值wire检查；状态处理也须按presence整包替换，不能只改proto |
| `src/core/GameData.cpp` 构造/initializeRobots | 默认机器人、基地5000/前哨1500、时间420等并非收到的样本 | 统一状态先未知；每项标接收时间；不把构造seed送进正式HUD，不搬进程内ProtocolSimulator |
| `src/core/GameData.cpp::updateRobotStaticStatus` | uint32降为quint8/quint16，float冷却速率降整数 | 原始报告保持32/64位与float；UI投影前明确校验，异常值未知；测试65536、uint64大值、小数 |
| `src/core/GameData.cpp::updateGlobalUnitStatus` | 兼容多种槽数并用数值>0判断敌方快照 | 2026官方十槽严格映射；不能把0HP/0状态判成缺失，不能推测数组版本 |
| `src/network/MqttManager.cpp` 后台connect与disconnect | connect闭包捕获this及可变m_client，disconnect销毁句柄；没有等待/会话代次屏障 | 连接/订阅/断开归同一工作线程生命周期；取消/身份切换丢弃旧回调，线程退出后销毁；拒绝迟到旧身份报文 |
| `src/network/MqttManager.cpp::subscribe/publish` | 使用同步MQTTClient订阅/可选等待发布；回调复制载荷后排入Qt事件队列 | 不在GUI线程等待；接收限额及有界队列放在复制/排队前，严格长主题处理；慢SUBACK/断线/洪泛验收 |
| `src/network/NetworkManager.cpp::sendKeyboardMouseControl` | 13ms间隔≈76.9Hz；中性释放帧可绕过节流 | 严格75Hz预算与释放优先队列；失焦立即清本地输入，发送仍遵守协议上限；不重放旧输入 |
| `src/network/NetworkManager.cpp::sendMapClickInfo` | 固定500ms未区分半自动操作手3秒限制 | 角色明确后选官方间隔；本项目红步兵3不自动等同云台手 |
| `src/network/VideoReceiver.cpp::onReadyRead` | 收包后就地调用decodeAssembledFrame，批量完成帧无本项目显示预算 | 迁移时明确工作线程、收包/组帧资源限额、显示只保留最新帧；不能仅凭上游类名断言异步或低延迟 |
| `src/network/VideoReceiver.h/.cpp` | 同时包含HEVC与MQTT私有H.264分支 | 当前仅VT03/VT13官方HEVC主链路；不恢复旧设备或复制无调用的H.264支线 |
| `src/ui/MainWindow.cpp` | 身份切换、键鼠采集、配置信号和后端启动集中在窗口；上游UI还有未覆盖的业务 | 只提取调用顺序与业务动作，接TerminalController；无入口的动作列缺口，保留本项目UI设计 |

目前这些是代码证据和迁移修正要求，并未全部在生产后端修复。MIT允许复用；全面对齐功能需要上述适配，直接把整个窗口/状态对象编进去不能满足本项目的数据有效性要求。

## 4. 后端编译范围与替换顺序

路径相对开源源码。以下是保留上游类结构时的依赖闭包；实施时删除已被裁剪功能的依赖，禁止为凑编译引入整套上游窗口。

| 阶段 | 需要复用/提取的源文件 | 本项目接线 / 待替换项 |
| --- | --- | --- |
| M1 MQTT | `network/MqttManager.{h,cpp}`、`MqttLogger.{h,cpp}`；Paho同步库或修正生命周期后选择现有异步库 | `main.cpp`、连接配置、TerminalController；完成后替换`protocol/rm2026/mqtt_observer.*`独立主入口，不同时更新HUD |
| M1 路由/状态 | `network/NetworkManager.{h,cpp}`、`core/GameData.{h,cpp}`、`GameConstants.h`、`CustomDataTypes.h`、`TimedEventRules.{h,cpp}`、`PopupStateMachine.{h,cpp}`、`PopupTypes.h` | 只保留官方MQTT路由与已核验业务；统一状态是唯一报告源，TerminalState保留指令策略/展示投影；替换`state_mapping.*`后才删旧映射 |
| M1 当前类耦合 | `network/Protocol.{h,cpp}`、`CRC.{h,cpp}`、`config/ConfigManager.{h,cpp}`、`ConfigLoader.{h,cpp}`、`ConfigValidator.{h,cpp}`、`core/LogFileNaming.{h,cpp}` | 这些由完整NetworkManager/GameData引用；提取MQTT后可去掉Legacy UDP、自定义顶层容器、配置单例，不能把它们当新官方协议 |
| M1 schema | `network/proto/robomaster.proto`中的已核验定义/业务名称 | 以本项目canonical字段为准；保留上游版权来源与补丁清单，不用官方冲突字段或内部容器填坑 |
| M2 HEVC | `network/VideoReceiver.{h,cpp}`的HEVC组帧分支、`HevcDecoder.{h,cpp}`、必要`VideoDatagramLogger.{h,cpp}` | 送`QVideoFrame → QVideoSink → 现有VideoOutput`；替换`historical_video_receiver.*`、私有Annex-B环回中转及孤立实验选项，验收后再删 |
| M3 动作 | `NetworkManager.cpp`对应已核验序列化函数、`MainWindow.cpp`身份/输入/释放逻辑 | 现有动作→唯一指令门→有界发送；发布回执与执行回执分别记录，不搬MainWindow UI |

M1最小Qt组件：Core、Network、Gui（GameData使用QColor/QImage），现有应用已经通过Quick/Multimedia引入Gui；不需要Widgets/QuickWidgets。Protobuf/Paho用当前MinGW源码构建，不链接上游MSVC库。M2只需实际调用的avcodec/avutil/swscale，版本和便携分发待核验。不复制上游main、QML、widgets、进程内simulator、资源包或下载器。

## 5. 第一条完整链路的验收安排

1. M1先做生命周期和统一状态修正，再接普通exe的显式`rm2026-v2.0.0`配置。无显式配置保持等待；不猜地址/扫描，不将现有39001等模拟端口当官方端口。
2. 本机MQTT对端核对CONNECT的数字clientID `3`、24条可解码下行的订阅/SUBACK、重订阅、各消息映射、未知/零值、主题单独过期、断线、身份切换、关闭。接到样本的HUD仍标清测试来源。
3. M2以人工HEVC和有来源样本验收乱序/重复/丢片、关键帧同步、重启/断流/退出、有限内存及显示队列；保持现有视频比例和UI。软件出帧不标记VT13配对成功。
4. M3逐动作核对payload和限频；覆盖未解锁、失焦、弹窗、处罚/阶段、断线、旧输入及模拟/回放隔离。处罚解除、现场地图坐标和未核验语义只阻塞相应动作。
5. M4等待VT13及官方服务：官方物理对频、红步兵3登记、实际视频源、赛事回执、1080p60、长时稳定性及端到端延迟。这些无法由本机替代验收。

2026 §2中的服务IP/端口和附录机器人ID仅作为该明确版本依据：红步兵3请求clientID为3；这不证明认证成功。自动USB配对没有已取得的公开SDK，不猜USB/串口/IPC指令。

## 6. M0复现与验证

### 用户指定雷达项目（2026-10-11补充）

路径：`D:/vs/vscode/c++/huge mas/mas_radar`。本次只读检查README、main.py、RM_serial_py/ser_api.py、标定相关引用和LICENSE；不运行main、不打开相机/串口、不修改雷达项目。

| 检查项 | 实际代码 / 与客户端的关系 |
| --- | --- |
| 当前来源/边界 | README为厦门理工PFA单目雷达方案，引用2024分享；项目LICENSE为GPL v2文本。保持独立进程，客户端不导入视觉代码/模型/相机SDK |
| 上报路径 | main.py:496–498调用build_data_radar_all和build_send_packet，命令0x0305；未发现MQTT/Protobuf或RadarInfoToClient定义 |
| 当前发送状态 | main.py:19串口打开注释；坐标write及其他主程序write注释；ser_receive读取注释，received_data=b''。存在发送示例，但不能当主程序已发送证据，不启动示例 |
| 当前坐标载荷 | ser_api.py:107起打包单边对方1/2/3/4/5/7，每个x/y为H；24B载荷、33B完整帧。R方发B槽，B方发R槽，seq及CRC由构包函数补齐；H格式使用主机字节序，不应默认为通用显式小端实现 |
| 官方2026差异 | V2.0.0 §1.3.2（PDF34–36）0x0305为48B，对方六槽后己方六槽，各1/2/3/4/6/7；第5槽是6号空中机器人，不能沿用旧5号步兵槽。双方不能用旧24B包直接冒充新版本 |
| 坐标依据 | get_coord_B/R及显示使用2800/1500边界、翻转与int，乘10再除10未形成实际单位换算；函数注释写M不足以证明输出米或厘米。需结合当前标定矩阵、场地版本、图像缩放和样本验证；官方0x0305单位cm且(0,0)表示未发送该机器人坐标 |
| 雷达与终端接线 | 预期按对应版本经“雷达→裁判串口→官方服务→客户端已核验主题→地图”接入；不将0x0305二进制当MQTT payload，也不在终端额外抢占雷达串口。若另做本机直接联调，必须明示独立测试来源与坐标契约，不能顶替官方数据 |
| 尚未解决 | 此项目没有RadarInfoToClient外层proto/已采样MQTT字节，无法消除PDF外层字段冲突。终端普通数据链路继续M1；雷达上报版本适配/标定另登记，不自动修改或激活雷达 |

离线验证仅加载无I/O的ser_api.py，用人工坐标分别检查R/B：均为24B载荷、33B帧；未导入main或相机/串口依赖。此结果证明当前编码长度，不证明现场链路、坐标准确性或2026兼容。

### 核验复现

从`client/`执行，Python和protoc仅用于开发：

```powershell
& 'D:/YOLOV8/python.exe' tools/audit_rm2026_upstream.py `
  --upstream 'D:/vs/vscode/c++/huge mas/开源/RM26_Client-e291f2f' `
  --protoc 'D:/YOLOV8/Library/bin/protoc.exe' `
  --output contracts/rm2026-v2.0.0/upstream-audit.json
& 'D:/YOLOV8/Library/bin/protoc.exe' --proto_path=contracts/rm2026-v2.0.0 `
  --descriptor_set_out=build-rm2026/client.desc contracts/rm2026-v2.0.0/client.proto
$env:PYTHONUTF8='1'
& 'D:/YOLOV8/python.exe' tests/test_rm2026_protocol.py --descriptor build-rm2026/client.desc -v
```

已完成：35消息descriptor生成、165个singular字段的零值/缺失wire检查、全部字段与官方PDF示例对照、原有8个离线协议测试、扩展schema的原生探针重新编译；原生MQTT/入站异常CTest 2/2通过（76.36秒）。运行测试需先加载项目`scripts/environment.ps1`的工具链环境，避免开发exe找不到Qt/MinGW DLL。来源哈希与机器清单重复生成结果一致。UI设计、普通exe启动行为本阶段未改变；M1–M4依上表继续验收。

## 7. M1：普通客户端的数据主链路

`cmake/rm26-upstream.cmake` 在普通 exe 链入项目内的固定开源后端，
`MqttManager → NetworkManager → GameData → UpstreamBackend → TerminalController → 原 QML`。
`third_party/rm26_client/SOURCE.json` 记录原始文件哈希，`PATCHES.md` 记录本地改动。
协议依赖共用 `cmake/rm2026-dependencies.cmake`，同一 MinGW 编译固定源码；
发布脚本携带 MIT、Paho、Protobuf、Abseil、utf8_range 的许可证/修改说明。
上游窗口、素材、模拟器、自定义视频解码器未引入本阶段。

| 运行层 | 当前行为 |
| --- | --- |
| 连接/身份 | 无参数启动等待现有面板确认；MQTT IPv4/端口、阵营/机型或 CLI 数字 ID 明确后连接。红步兵3=3，蓝步兵3=103，保持上游 clean session/20s keepalive。更换配置立即清 HUD；报告身份与请求身份分别记录 |
| 订阅 | 24 个已核验下行主题一次订阅，全部 SUBACK 成功后开始接收；拒绝、越级 QoS 或超时不进入就绪。移除重复 Event、未定义 CustomRobotData；新增 MapClickInfo。RadarInfoToClient 未订阅/接受 |
| 入站/恢复 | Paho 分配前防护继续使用；topic≤64B、payload≤64KiB、回调队列≤256，保留会话代号及单调接收时间；保留上游5s/最多3次自动重连。连接等待2s，订阅等待使用Paho实际支持的5s下限并检查返回值；取消会话后不继续其他订阅。retained 和订阅前消息不当作本会话数据 |
| 权威状态 | GameData 保存真实收到的 optional 字段，完整 uint32/uint64、int32、float；整包替换、每主题1500ms过期。报告身份变化清其他报告/历史，断线清空，重新订阅后只接受新报告。HUD 不用上游默认种子和窄化 getter |
| 现有 HUD | 本车报告身份、血量/最大血量、热量/许可弹量、双方血量/建筑、本方经济、比赛概要和有界事件历史；每个字段以报告有效性为准。没有功率/实弹/姿态的报告不填模拟值 |
| 暂仅领域/诊断 | 处罚、模块、复活、位置、Buff、性能、部署有类型状态；所有24主题的有效原字段/age可在本机 `reports` 树检查。机制、伤害、路径、地图点击、字节块、工程、机关、哨兵、飞镖、空支援的专用 UI 入口尚缺，保持原始报告，不推测玩法或新增控件 |
| UI/模式 | QML 仅调整来源/身份绑定、既有进入和视频切换回调，未改几何/素材/准星。环回面板配置明确标本机协议测试；离线演练/本地回放停止 MQTT 并清旧数据。所有 M1 模式仍无指令发布 |

本机复现（不会连接赛事服务，不启动通用 broker）：

```powershell
. ./scripts/environment.ps1
$toolchain = Get-Rm27Environment
& $toolchain.ctest --test-dir build-gui --output-on-failure
# 单独运行新主链路对端，自己申请环回临时端口并启动同一个普通 exe：
& 'D:/YOLOV8/python.exe' tests/test_rm26_upstream.py --exe build-gui/rm27_terminal.exe
```

主链路测试检查 CONNECT ID3、24主题/QoS、全部主题解码、HUD真实接收值、
显式0/缺失、大整数/小数/负偏移、畸形/NaN/retained拒绝、逐主题过期、
报告身份改变、断线清空及5s后重订阅恢复；订阅拒绝/越级/不答时 GUI 诊断继续输出，
控制始终禁用。Qt 用例检查现有面板红3→蓝3请求身份切换、没有报告时机型待确认、
真实离线按钮回调和回放退出；原回放/模拟/UI 回归继续执行。
这是软件 wire/HUD 验收，VT13 实物、官方登记/配对、实际数据与1080p60视频仍未验证。

下一步 **M2**：核验 FFmpeg 开发头/MinGW 导入库/DLL/许可证，复用开源收包和解码恢复，
把帧直接送原 VideoOutput；随后 **M3**：把既有动作接到命令门、规范限频与回执。
不要重新增加数据实验 exe 或把本文的本机测试报告表述为现场验收。

本轮结果：日常 `build-gui/rm27_terminal.exe` 重建成功，普通构建的 CTest
**18/18 通过（96.32s）**，包括4个新 wire 场景及真实启动面板回调；
35消息移植 descriptor 与 canonical 一致，165个零值/presence检查通过，
既有8个离线协议测试通过；独立入站/只读快照/原生 MQTT 回归3/3通过。
25个保留的上游文件原始哈希核验通过，6个实际补丁文件有记录。
三个修改的 QML 文件几何/颜色/字体属性与迁移前一致，原布局和回放回归通过。
Paho 1.3.14 拒绝小于5000ms的命令等待设置：已按其支持的5s配置并检查返回，
晚到 SUBACK 的取消测试确认退出后不继续订阅；退出测试允许当前等待与线程清理共8s，
不能把连接2s误写成全部操作都2s。

M2 预检已确认应复用 `src/network/VideoReceiver` + `HevcDecoder`；
`H264Decoder` 是另一条私有 H.264 路径，不能作为 VT03/VT13 HEVC 的替代。
本机 Qt bin 有 avcodec-61/avutil-59/swscale-8 DLL，但所检查的 Qt include/lib
未提供 FFmpeg 公开开发头/直接导入库。下一步补齐匹配的开发依赖并核验 ABI/许可，
不能只因 Qt 回放能出图就声称 FFmpeg 源码解码后端已具备。


## 8. M2 普通图传接线（2026-10-11）

复用同一固定提交的 MIT `HevcDecoder.h/.cpp`。上游 VideoReceiver 已读收包、组帧、解码和恢复；其不限分配/队列、共享绑定、缺片仍转交与私有H264支路不采用。现有严格 VideoReassembly/HevcSyncFilter 给开源parser/send/receive/sws/QImage提供完整有界Annex-B单元。原 VideoOutput 可见性与来源绑定接到C++，保留所有几何、字体、颜色和 PreserveAspectFit；Windows窗口实际色条像素/截图检查。

- `src/video/upstream_video.*`：独立线程、独占显式IPv4监听、固定源IP/首个有效源端口；每事件最多64包、一秒8192包、接收缓冲256KiB、单帧2MiB/2048片/150ms。最新待图像最多1，GUI16ms取最新；实际解码与呈现分开计数。丢片/序号缺口恢复、静默700ms允许发送端重启，参数/IRAP等待，出帧超时清旧画面。数字均为项目决策。
- `third_party/ffmpeg-7.1.5`：官方同版公开HEADERS，保留LGPL/版权；avconfig.h是本机x86_64生成设置。来源归档SHA256 `de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`，保存在 Huge Mas/开源/tmp。Qt实查 LGPL、无GPL/nonfree，avcodec61.19.101/avutil59.39.100/swscale8.3.100；编译直接读DLL导出，不混MSVC库，运行初始化检查ABI版本。
- 开源补丁：完整AU parser模式、FFmpeg要求的零padding、初始关键帧等待、短NAL/解析无进展保护、max_pixels1920x1080、坏帧丢弃/错误恢复、重置恢复parser标志。采纳原软件解码及slice-thread设置；未证明低延迟/1080p60能力。
- 普通CLI `--protocol rm2026-v2.0.0 --hevc-udp`：视频单独配置，或明确MQTT/身份与视频组合；固定来源/监听/端口/base必须填写。非环回需显式启用，测试模式只127.0.0.1。主启动脚本不再转独立实验exe。MQTT阻塞/断线不阻止视频，视频静默不阻止新裁判报告；配置/报告身份变化、离线/回放切换清旧视频，模拟TS切换停止MQTT。
- 诊断：收包/拒绝/重复/丢弃、组帧、解码、最新图像深度/覆盖、重同步、解码错误/耗时、接收到组帧、解码到提交、有效sink帧/帧龄/恢复与GUI抖动；没有源端时间戳，`endToEndMeasured=false`。本机320x180/30fps色条，无实物、1080p60/长时/端到端benchmark结论。
- 打包脚本已加同版DLL哈希检查、swscale部署、LGPL/源码来源/配置及校验源码归档；脚本语法已检查，本轮未生成Release包，未验干净机器。

M2 收尾：LOCAL_HUD 合成视频统一到普通入口的 UpstreamVideo/开源 HevcDecoder；已删除 HistoricalVideoReceiver、原始流第二 UDP 解码端口、独立 run-rm2026-video.ps1、VIDEO_HUD 构建分支和 historicalVideo 属性。人工配对 CLI 接普通 exe，新鲜 SelfConfig 自动提供报告身份，测试模式不能建立硬件会话，源/身份切换与无效帧清证据。默认OFF的历史数据探针/LOCAL_HUD 仍用于协议回归，未全部删除。**下一步 M3 指令、门控、限频及回执**。配对框架的目标/人工确认/报告/视频证据保留，不把网络收包当USB对频或硬件认证。实物VT03/VT13与官方服务联调是M4。


M2 软件验收结果：普通 build-gui 全套 CTest **19/19通过（114.18s）**，包括普通exe的5个wire场景（新增MQTT/视频独立断链）与Windows真实渲染/色条像素检查。最后仅调整HEVC诊断来源文案后重建普通exe，相关诊断/HEVC/UI测试 **3/3通过（9.50s）**。视频单独CLI烟测正常退出，监听成功、无伪造画面/数据/控制。FFmpeg122公开/生成头哈希及3个运行DLL哈希核验；3个QML文件几何、颜色、字体、比例属性与HEAD一致。发布脚本语法通过，未生成Release包。实物/1080p60/长时/端到端仍待M4。


M2 入口统一最终验收（2026-10-11）：普通 build-gui/rm27_terminal.exe 重新编译，最终全套 CTest **19/19通过（119.19s）**。LOCAL 数据/HEVC故障/TS开发回归 **3/3通过（72.86s）**；最新LOCAL重建后HEVC再验 **1/1通过（18.88s）**。组合wire用例现等 subscribed 再送状态，保留订阅完成前拒收策略；已记录初跑时序失败及修正。实际解码色条进入现有UI并推进配对视频证据，新鲜报告身份/过期未知/不匹配停旧流、切源/离线/回放清旧会话通过。旧视频中转及独立脚本已删除，历史数据探针默认OFF保留。下一项M3上行消息、既有动作、指令门、限频与结果报告；实物、1080p60、长时、端到端及新Release便携包尚待。


## 9. M3.1 上行线格式与发送入口（2026-10-11）

本轮完成M3第一段：11类有类型命令、开源发送流程统一、线格式/入站PUBACK核验。普通UI仍没有发布调用或业务许可，M3未整体完成；不是实际机器人操作验收。

- `src/core/rm2026_commands.hpp`：纯C++领域类型，不向QML暴露生成的Protobuf。`PerformanceCommand`允许字段缺失，显式0与缺失分别序列化。地图用固定7字节列表，`markingType→mode`、`markingMode→type`，不再发布旧MapClickInfoNotify。
- `src/protocol/rm2026/upstream_commands.*`：11个官方消息的生成/字段校验，共用terminal门；身份新鲜且匹配、businessPolicyReady、官方键鼠来源选择、地图角色/标定分别判断。该policy许可仍须M3.2落地逐角色/费用/复活/处罚业务，不由配对或收图自动赋予。
- `NetworkManager::sendCommand`：所有上行只经这一个入口；先过门，再重读实际订阅、CONNECT clientID、当前收到的身份与比赛/暂停报告、会话代次，再限频和发送。被替代的分散QML、裸包、旧UDP发送方法及其节流/键鼠缓存已删除，接收和状态路径保留。
- `MqttManager::publishCommand`：沿用Paho QoS1、retained=false，结果为TransportRejected / BrokerAcknowledged / DeliveryUnknown。PUBACK不是赛事接受或机器人执行；送达未知关闭/销毁会话，不自动重发命令。共享Paho防护允许严格2B/非零编号PUBACK，原来的只读防护拒收该报文是本轮首跑失败原因。

| 命令 | 当前发送间隔 | 已核验依据 / 业务缺口 |
| --- | --- | --- |
| KeyboardMouseControl / CustomControl | 13,333,334ns，含全释放帧，无例外 | 通信PDF45–51、16位键掩码/30B；M3.2键鼠连续输入与优先释放已接，来源授权/实机操作待验 |
| MapClickCmd | 云台手500ms / 半自动操作手3000ms，未知角色拒绝 | §1.3.1 PDF33、§2.2.18 PDF65–66；角色/地图标定/既有地图动作待确认接线 |
| Assembly / Performance / Common / Deploy / Rune / Dart / Sentry | 各主题100ms | PDF48–49、67–73，七类均≤10Hz；工程难度上限须新鲜报告，性能须机型，复活/兑换须资格/费用，英雄/飞镖/哨兵须角色门 |
| AirSupportCommand | 1000ms | 官方仅“触发”；1Hz沿用上游工程策略，不能称官方上限；付费及解除反制资格待M3.3 |

普通UI没有工程/飞镖/哨兵等控件，先保留有类型接口；本轮没有增加或改动QML。SentryCtrlResult等下行仍作为收到的报告，未伪造请求编号、唯一关联或成功执行状态。

验证：`rm26_uplink_tests` 为CTest测试程序，独立127.0.0.1协议对端核对11条实际PUBLISH字节、QoS1/no-retain/no-DUP、缺失/零值、负鼠标值、限频边界、未知/过期/变化身份、clientID不匹配、暂停、未订阅/锁定、模拟/回放、缺PUBACK无重发及畸形ACK拒收。它没有普通客户端的测试解锁参数，也不连接任何赛事端点。固定假时钟验证75Hz上限，未验连续75Hz吞吐。

下一步M3.2先替换当前worker最多1s的同步确认等待（连续75Hz不可沿用），做有界异步回执/超时及最新输入调度，再接现有动作、失焦/释放/遮挡/切源清理和实际业务门。最后才验证UI上行链路，M4等待实物/官方服务。


M3.1 软件验收：普通 build-gui 全套 CTest **20/20通过（139.26s）**；新增上行用例单独 **1/1通过（8.06s）**、全套中8.07s；历史原生MQTT/入站防护 **2/2通过（76.90s）**。普通exe已重建，UI/HEVC/配对/数据/模拟/回放回归通过。本轮未改QML，没有普通UI发布调用或测试解锁参数。M3.2异步确认、输入采集/释放/失焦/旧输入调度、现有动作业务门及业务回执仍待实现；未做实物/持续75Hz/1080p60/Release发布验收。

## 10. M3.2 异步确认与窗口输入（2026-10-11）

继续复用固定 MIT RM26_Client 的 deliveryCompleteCallback 与 MainWindow 键鼠位掩码、增量及清理逻辑；版权/许可证保留在实现头和 PATCHES.md。普通窗口已经接采集与有类型动作入口，但业务权限未实现，控制仍锁定。未复制开源 UI 或模拟器，未新增普通客户端解锁参数。

- MQTT：QoS1、不 retained，入队返回 Queued/token，PUBACK/送达未知通过工作线程异步反馈，替代 M3.1 最多1s的同步等待。最多8条在途，1s确认期限；核对代次与回调到达时间，晚到ACK不能变为成功。超时/取消销毁旧handle，连接丢失也先销毁再重连，隔离旧QoS状态；PUBACK不是赛事接受或机器人执行。
- 输入：`OperatorInput` 观察现有 QWindow，W/S/A/D/Shift/Ctrl/Q/E/R/F/G/Z/X/C/V/B 对应官方16位，三鼠标按钮、X/反向Y/滚轮增量进入领域帧。14ms周期，增量只消费一次；按下/释放立即替换待发状态。失焦、隐藏、销毁、占屏或关闭指令门清帧，重新启用时，原来按住的键/按钮必须先释放再重新按下；换窗口也保留这个约束。当前增益1，不声称完成无限相对鼠标捕获或实机灵敏度。
- 调度：`UpstreamBackend` 一个最新输入槽、一个优先释放槽、一个有类型事件槽，没有逐输入GUI排队。释放不能被紧接着的重按覆盖，仍走同一13,333,334ns限频；事件不阻塞持续输入。输入/待发事件100ms新鲜度，断线/身份或会话切换/关门清槽；只接受已订阅会话的输入。清理期间关闭活动或在途控制会话，不发送绕过原指令门的中性帧。已经进入传输的包无法撤回，断开不能当作实物停车确认。
- 接线：`TerminalController::observeInputWindow` 接现有窗口/占屏/状态变化，`submitRm2026Command` 是C++有类型动作入口。QML仅新增窗口观察调用，没有新控件或设计变化。普通context仍不给businessPolicyReady、输入来源或地图角色/标定许可；模拟/回放与请求/报告身份判断保留。
- 诊断：`refereeDiagnostics.commands` 分别记录admitted、broker_acknowledged、delivery_unknown、in_flight、最新输入/释放/事件深度及覆盖数；最新状态不能作为官方业务回执。所有周期、队列和期限为项目决策，正式字段依据仍是2026 V2.0.0 §2.2，尤其键鼠PDF49–51。

本机用例扩展在已有CTest程序内：十一主题独立黄金字节/异步ACK、250ms延迟时事件循环继续、8条在途上限、缺ACK/三种坏ACK、取消后的晚ACK和新会话不重发、释放后立刻重按、占屏清会话、增量不重发、GUI输入过期、500次输入覆盖、持续输入与赛事事件共存。窗口用例验证失焦/重新按键、鼠标按钮/轴/滚轮、窗口切换/销毁与旧窗口事件不再生效。仅隔离测试构造policy permit；普通exe的本机数据用例继续要求没有PUBLISH。

下一段M3.3：逐角色/来源/比赛/暂停/复活/费用/装配/处罚业务资格与既有动作，再核验结果报告的关联语义。地图标定、处罚权威解除和缺UI入口分别登记，不以本机fixture解除普通控制门。M3未整体完成；VT03/VT13实物、官方服务、严格75Hz/1080p60、长时/端到端和新Release包仍待M4。

M3.2 验收：普通exe重新编译，最终build-gui CTest **21/21通过（134.68s）**；专项 **2/2通过（14.79s）**。全套上行14.63s、窗口输入0.18s，普通wire/HUD、Windows实际HEVC色条、配对、UI布局、回放与模拟隔离均通过。500次输入更新未变成500包积压，14ms持续输入与一个赛事命令可并行；不把这个短时检查当作严格75Hz或硬件执行验收。本轮未改共享入站防护的格式限制，因此沿用M3.1的原生MQTT/入站防护回归证据；没有为未改动的历史实验重复扩大测试。


## 11. M3.3 发送时的必要业务约束（2026-10-11）

本轮在既有统一发送入口内增加业务检查，没有新增UI或操作解锁。依据固定MIT源码的PopupOverlayPolicy/MainWindow，并逐项核对通信V2.0.0（PDF58–59、67–75）与比赛规则V2.2.0（PDF75–80、88–94、102、112–115）。当前只验证可观测的必要条件；普通controller仍不给完整businessPolicyReady。

| 动作 | 新增必要条件 | 仍缺的放行依据 |
| --- | --- | --- |
| 所有上行 | 实际新鲜身份匹配、明确连接；性能以外明确上场；分别核验阶段/暂停与存活例外 | 操作权限、处罚持续约束/权威解除；不能把收图或订阅当权限 |
| 性能选择 | 准备/自检；英雄整机3/4、步兵独立1/2；普通比赛阶段拒绝 | 哨兵控制方式选择的实际操作者注册 |
| 免费/付费复活 | 明确死亡、pending、canFree/canPay；付费实际报告费用>0及足额余额 | 完整授权；PUBACK不代表成功复活 |
| 普通弹量兑换 | 17mm为10发倍数、1金币/发；42mm为10金币/发；角色及余额 | 兑换区域、队伍1000/100配额，故对应请求仍阻塞 |
| 远程兑换 | 角色、脱战及明确canRemoteAmmo/Heal | 参数语义仍不完整，不能照搬上游UI百分比/100发步长，继续阻塞 |
| 装配 | 工程、新鲜最大难度、难度1..4、开始/确认步骤；取消须实际进行中，可死亡时取消 | 实际选定难度/装配几何由服务器核验；临时激活资格不能从死亡单独推断 |
| 部署 | 英雄；退出须实际已部署，死亡仍可请求退出；进入须远程4/4 | 区域和8秒冷却缺少权威报告，进入仍阻塞 |
| 能量机关 | 步兵、新鲜未激活报告 | 剩余激活机会不能由当前状态推断；哨兵串口路径不是MQTT权限 |
| 空中支援 | 空中机器人/云台角色、状态、剩余免费时间、至少1金币启动付费；解除反制须shooter_status=2，按2×即时复活规则费用检查 | 操作者注册、持续付费/实际执行；cost_coins为已消耗，不是新请求费用 |
| 地图、哨兵、飞镖 | 地图同队接收者/非重复、敌方目标检查 | 地图角色/标定，哨兵/飞镖跨机器人操作者身份；不扩展普通CONNECT身份范围 |

取消装配、退出部署、取消空中支援不统一要求存活，但都检查对应实际状态。连接/上场/存活策略为保守项目决策；未来完整授权实现必须单独核验这些条件。卡牌/未知处罚报告拒绝；这不是跨过期/重连的持久处罚锁，更没有将零时长或过期当解除实现。缺失必要报告阻塞；全部可观测检查通过仍不能替代完整businessPolicyReady。

GameData增加TechCoreMotionStateSync、RuneStatusSync、AirSupportStatusSync、SentryCtrlResult领域映射，沿用逐主题1500ms、身份变化清空。RobotStaticStatus的连接/上场/存活及性能码、RobotDynamicStatus的脱战/远程资格均使用has_，不从robot_type猜枚举。诊断sentry_result_report保留command_id/result_code，显式结果0与缺失不同；协议没有唯一请求ID，不能宣称与本地命令关联或把broker ACK变成业务结果。未来接线保持三者区分。

测试将十一消息黄金PUBLISH字节和在途上限改为隔离MqttManager传输检查，明确不证明角色资格；NetworkManager用实际红步兵3的报告验证合法动作与拒绝路径，旧的持续输入事件改成步兵能量机关请求。普通exe本机数据用例继续要求没有PUBLISH。没有加入普通UI/CLI绕过开关，未连接真实服务。


M3.3普通软件验收：正常rm27_terminal重新编译；专项上行/窗口输入 **2/2通过（15.65s）**，全套build-gui **21/21通过（144.53s）**。全套上行15.24s、普通后端61.56s、输入0.19s；UI布局、实际Windows HEVC像素、HUD/数据/配对、模拟和回放隔离通过。普通exe143,772,381B。没有实物连接/操作解锁/Release包更新；不把必要约束检查当完整M3验收。

共享映射兼容性：build-rm2026-native只重建rm2026_mqtt_probe（canonical rm2026::v2分支编译通过），rm2026_native_mqtt_tests **1/1通过（59.85s，测试59.83s）**。只读探针仍为原14主题、无上行；共享入站格式限制本轮未改，未重复扩大入站防护回归。


## 12. M3.4 复活快捷键与动作派发（2026-10-11）

读取固定MIT MainWindow::tryHandleRespawnShortcut（Y免费/F1付费、自动重复与修饰键拦截）后，在既有InputAction/TerminalController/Shortcut路径接入复活事件，没有搬入开源窗口。F1已经用于本项目启动面板，保持其作用；付费改为Shift+F1（项目键位，不是官方协议）。Y为免费请求。Shortcut禁用自动重复，启动面板/对话框/选手展开等占屏时禁用；Ctrl/Alt+Y不匹配。

动作表明确refereeCommand，现有快捷键列表标为命令类。赛事请求在控制器内构造CommonCommand类型3/4参数0，调用唯一有类型发送入口；被拒绝时返回false并通过既有告警说明未发送，不触发inputActionRequested，不以“界面动作已派发”冒充执行。排队只表示等待工作线程/PUBACK，不能视为复活成功。未增加普通UI/CLI解锁，businessPolicyReady保持false。

UpstreamBackend.submitCommand在占用事件槽前检查最新实际报告的必要资格；NetworkManager发送时仍重读GameData，双重检查不把缓存当永久资格。已知无资格请求不会占事件槽或进入发送流程，现有模拟/回放/界面占用/权限/死亡/pending/canFree或canPay/新鲜费用及金币门保持。

验证增加到已有程序内：动作表无重复键位，F1用途不变；实际QML Y/Shift+F1、修饰键/自动重复、占屏与模式锁定；失败不派发界面信号。隔离MQTT fixture验证免费/付费CommonCommand黄金字节08031000/08041000，收到死亡/资格/金币不足报告后的排队拒绝、事件槽继续可用。仅隔离fixture提供许可，普通窗口拒绝，不称实机验收。来源：通信V2.0.0 PDF57–59/69–70；资格和费用沿用M3.3，参数0保持既有保守限制，不把开源robotIndex=0解释为官方目标索引。


M3.4验收：普通exe143,813,417B，重建时间2026-10-11 18:14:50。核心策略/上行/窗口输入专项 **3/3通过（17.58s，上行16.60s、输入0.92s）**；最终build-gui全套CTest **21/21通过（136.12s，上行16.70s、输入及复活快捷键0.82s）**。十一主题/异步回执/旧输入清理、免费/付费请求字节、事件预检和持续输入、实际QML快捷键与模拟回放阻塞通过。原三份QML几何/字体/颜色/视频比例属性逐行与HEAD一致；原UI/Windows HEVC/HUD/配对/回放通过。共享历史状态/入站格式未改，沿用M3.3兼容性证据，未扩大未改路径的回归。没有真实连接/普通解锁/自动复活/Release发布/推送。

## 13. M3.5 动作入口核对与排队事件隔离（2026-10-11）

| 动作 | 固定开源入口/参数来源 | 活动客户端核查 | 本轮处理 |
| --- | --- | --- | --- |
| 性能选择 | src/ui/MainWindow.cpp::onPerformanceSelectionChanged(shooter,chassis,sentryControl)，设置面板选择后传三项 | 现有performanceLabel、performanceReadout只有FPS/GUI抖动；没有射击/底盘/哨兵控制方式选择项 | 保留PerformanceCommand的optional参数与有类型submitCommand；不默认选择或照搬整套面板 |
| 英雄部署 | src/qml/DeployModePanel.qml调用sendHeroDeployMode(1/0)，MainWindow::shouldShowDeployModePanel筛选英雄及面板占用 | 没有部署/退出按钮或快捷键；部署实际状态已有类型化报告 | 保留DeployCommand；退出检查实际已部署，进入的区域/冷却仍未核验；不从本地选机型赋予英雄身份 |
| 工程装配 | src/qml/ExchangePanelEngineer.qml::selectedIndex、confirmAssemblySelection和sendAssemblyCommandToServer；MainWindow工程确认热键检查selection页面 | 没有装配选择页、所选难度或步骤确认入口；TechCore最大难度不等于所选难度 | 保留AssemblyCommand操作/难度和当前步骤检查；不自动选最高难度，不增加Enter直接发送 |

以上只读对照MIT固定提交e291f2f，不搬入UI或NetworkContext裸发送接口。UI设计保持；缺口是实际代码核查，不能把有类型接口称为已经可操作。

排队事件此前在发送时被业务检查拒绝，或100ms过期，会调用clearInput/cancelCommands；有效持续键鼠也因此失效。M3.5在事件本地拒绝时，使用最近输入上下文，通过NetworkManager.keyboardInputAllowed重读实际身份/比赛/连接/存活/处罚及输入来源和授权；仍合法则只丢事件。检查无发布/限频副作用，且必须在队列mutex外调用，因为会话代次检查回调会取同一锁。事件过期不清输入；输入/释放过期、失焦/占屏/切源、共享资格失效和传输故障仍取消会话，不重发旧命令。

commands.event_status保存最近一次工作线程事件入队到传输/拒绝/过期的结果，避免被键鼠ACK覆盖；events_rejected仅计工作线程拒绝/过期，不包括submitCommand入槽前拒绝。它们均为本地调度诊断，不能作为赛事接受/执行回执，也没有实现业务结果唯一关联。普通businessPolicyReady保持false。

隔离回归先填满8条在途输入、暂缓ACK，再排队机关事件并改变报告。五场景通过（5.09s）：机关变为非inactive拒绝、事件超龄均零机关PUBLISH，释放ACK后继续键鼠；比赛暂停、报告身份变化及死亡仍停止控制。只用127.0.0.1和测试内许可，没有普通解锁或实物连接。事件影响范围、100ms和诊断是项目决策；报文/业务约束沿用官方2026通信V2.0.0与规则V2.2.0的M3.3核验。

M3.5最终软件验收：普通exe重新编译，144,000,129B（2026-10-11 18:40:27）。排队事件五场景专项通过（5.09s）；最终build-gui全套CTest **21/21通过（142.91s）**，上行21.99s、窗口输入0.79s、普通后端52.96s、UI2.59s、回放31.98s。普通本机对端仍要求零PUBLISH；HEVC/配对/模拟回放回归通过。本轮没有修改QML；共享历史映射/入站格式未改，未重复原生只读探针验收。git diff --check通过，Edit.md旧历史前缀哈希一致。未连接实物或官方服务，未开放普通控制、生成Release包或推送。

## 14. 业务回执边界与发布依赖

当前canonical消息定义及GameData::officialSnapshot/officialReports按下表核对。依据仍为M0/M3已核验的官方2026 V2.0.0；这里描述软件已接入的证据，不新增赛事权限或私有请求ID。

| 上行动作 | 可以观察的下行 | 当前落点及关联限制 |
| --- | --- | --- |
| KeyboardMouseControl、CustomControl | 没有逐帧机器人执行回执 | 仅记录本地入队、broker PUBACK或送达未知；不宣称实际动作或停止 |
| MapClickCmd | MapClickInfo | fresh reports诊断，尚无正式地图标定/既有动作入口；不是具有唯一请求ID的执行确认 |
| AssemblyCommand | TechCoreMotionStateSync | typed tech_core完整8字段及reports；敌方核心仅四级可用时有效，剩余时长仅己方四级装配时有效，没有当前所选难度或请求ID，不从零值/状态跳变确定某次请求成功 |
| RobotPerformanceSelectionCommand | RobotPerformanceSelectionSync | typed performance及reports；回报当前选择值，缺字段/零分别保留，不将相同值当某次请求ACK |
| CommonCommand复活 | RobotRespawnStatus、RobotStaticStatus | typed respawn/self_config；pending/canFree/canPay是资格，life是报告状态，不自动关联本地请求 |
| CommonCommand兑换/回血 | RobotDynamicStatus、GlobalLogisticsStatus等 | 接收后的当前读数；余额、弹量或血量变化不能唯一归因于本地请求，区域/配额/远程参数缺口保留 |
| HeroDeployModeEventCommand | DeployModeStatusSync | typed deployment及reports；状态为报告，不是本地操作回执 |
| RuneActivateCommand | RuneStatusSync | typed rune及reports；当前机关状态不能证明剩余机会或某次激活请求被接受 |
| DartCommand | DartSelectTargetStatusSync | typed dart_status及fresh reports；尚无既有UI入口，target_id/open无唯一请求ID，不是关联本地请求的动作结果 |
| SentryCtrlCommand | SentryCtrlResult、SentryStatusSync | typed sentry_status与sentry_result_report；command_id是命令类型，不能唯一关联重复请求 |
| AirSupportCommand | AirSupportStatusSync | typed air_support及reports；状态、时间、已消费金币及反制状态不等于某次请求执行ACK |

新鲜度、断线和身份切换沿用同一GameData，正式操作权限保持未开放。需要唯一业务确认时须官方协议证据，不以MQTT token充当赛事请求ID，不自动重发未知送达的事件。

发布脚本显式传MP4/HEVC/TS测试样本，省略时清掉对应CMake缓存，manifest.testFixtures记录覆盖范围。部署后复用check-windows-package的Directory检查；归档的Archive检查另核对ZIP/每文件哈希。两者使用Windows平台、无开发PATH/Qt插件路径、隔离配置和无关cwd，并核验Qt Quick、qwindows、avcodec/avutil/swscale从包内加载。FFmpeg运行DLL继续核对固定哈希，源归档/许可与RM26_Client SOURCE/PATCHES、Paho/Protobuf/Abseil许可随包保留。源码工作区未提交时manifest.sourceDirty如实标记，不能用sourceCommit单独复原迁移后的全部代码。

本机部署/解压验证不等于未安装Qt的另一台机器、实物配对、1080p60或现场控制验收，cleanMachineVerified保持false。完整构建/验收时间、数字、归档路径与失败修复过程只追加Edit.md，CLAUDE.md维护当前状态与待办。

## 15. 处罚持续约束与键鼠来源证据

官方2026通信V2.0.0 §2.2.15（PDF63–64）只提供penalty_type、penalty_effect_sec、total_penalty_num。类型1/2/3为黄牌/双方黄牌/红牌，4/5/6为超功率/超热量/超射速；没有定义类型0为解除，也没有权威解除、重复更新或重连补发规则。固定开源GameData::updatePenalty将报告转为RefereeWarningData并更新旧业务/提示，MainWindow订阅警告展示；这些本地提示、计时和回合重置不作为解除证据。

当前在唯一GameData的接收入口保存penaltyReleaseUnverified约束。只有通过主题、Protobuf、资源/结构与接收时间校验的报告能改变它；1/2/3及未知/缺失类型使其成立。新鲜报告仍正常过期并从penalty/reports移除，但约束保留。时长/数量归零、后来收到4/5/6、身份/阶段变化、断线重订阅、端点切换、停止后再启动都不解除。UpstreamBackend复用同一GameData，只清收到的报告和输入；指令入槽和实际worker发送前均读同一约束。诊断state.control_constraints.penalty_release_unverified明确为本地约束，不能解释为官方字段或无处罚证明。

上述是保守项目决策：没有按机器人分锁，也不从报告视角猜处罚归属；任一已接受的限制证据会阻塞该backend后续身份的控制。backend销毁/进程退出后的恢复、权威解除、完整视频/地图/自定义UI遮挡仍未实现。超功率/超热量/超射速单独出现不新建该卡牌约束，不宣称其全部处罚效果已实现。普通businessPolicyReady持续关闭，不能仅凭约束为false放行。

| 键鼠来源证据 | 核对结果 | 当前处理 |
| --- | --- | --- |
| 通信V2.0.0 §2.2.1，PDF49–51 | 明确要求提前选择官方端或自定义端，只能一个来源生成发送；该节未给选择/确认消息 | keyboardSourceSelected默认false，单纯本地选择不作为确认 |
| 35个canonical消息、36主题及固定开源NetworkManager/MainWindow | 未找到可核验的单一来源选择结果接口；CONNECT ID、订阅、机器人状态和PUBACK均不能证明来源排他 | 不新增推测主题、字段或控制解锁开关 |
| VT03/VT13使用说明书PDF17（印刷16） | 说明对频、连接电脑、登录对应机器人及UART键鼠数据，不提供自定义/官方输入来源确认接口 | 实物到位后按对应官方客户端/固件实际流程核验，记录选择及切换证据 |
| 当前普通TerminalController | 不提供businessPolicyReady、keyboardSourceSelected或mapCalibrated许可 | 测试许可仅存在于独立本机fixture，无UI/CLI许可通道 |

后续须先补官方版本对应的来源选择证据、会话失效/切源规则和处罚解除/恢复依据，再谈完整控制开放。未知事项不阻止继续核查其他动作的已有报告，但不能用实机之前的测试值填补授权。现有dist发布包保留为上一步的软件检查快照；本节源码变更未重新发布，详细验证只记Edit.md。

## 16. 剩余业务证据核对与必要边界

本节对照官方2026通信V2.0.0、比赛规则V2.2.0及固定MIT e291f2f。现有businessCommandBlock只实现必要约束，返回空不授予完整权限；普通businessPolicyReady继续false。没有从开源默认值或测试许可补出下列证据。

| 条件 | 官方依据、现有字段 | 固定开源与缺口 | 本轮落点 |
| --- | --- | --- | --- |
| 普通兑换区域 | 规则§5.3.2，PDF79：占领补给区/基地/前哨增益点；RobotStaticStatus.field_state只是上场状态，RobotModuleStatus.rfid只是模块在线状态 | ExchangeCommandPolicy验证本地请求步长，没有提供这些区域的权威占领证明；不能把在线或上场当占领 | 保留exchange_area_and_team_quota_unverified |
| 队伍已兑换弹量/剩余配额 | 规则表5-6，PDF78：17mm 1000发/队、42mm 100发/队；GlobalLogisticsStatus只含余额、累计经济、科技/加密等级（通信PDF54–55） | GameData.incrementAmmo17mm/42mmExchangeCount是本地累加100/10，不覆盖其他操作手/哨兵或重连前历史。RobotDynamicStatus.remaining_ammo/total_projectiles_fired也不是团队累计兑换量 | 新增单请求不超过整个队伍上限；未超过上限仍阻塞区域/剩余配额。不能把该静态检查称为配额跟踪 |
| Buff能否补出兑换额度 | 通信§2.2.14，PDF62–63：类型6为可兑换允许发弹量；buff_level只明确说明类型1/2/3/4/5的数值含义，没有说明类型6的额度单位和团队统计语义 | 仅保留报告原值；缺乏类型6与区域、已兑换量及全队上限之间的权威换算 | 不从正值、剩余时长或缺ID默认本车推导兑换许可 |
| 远程弹量兑换 | 规则§5.3.2/表5-6/5-9，PDF78–80：脱战，17mm每组100发、42mm每组10发，均150金币/组，6秒后生效；通信§2.2.11/25有can_remote_ammo，param仅称“对应的参数” | ExchangePanel.qml选择Common5/100或5/10，不能据此确认param就是弹数/组数；本机计数不补全队配额 | 复核角色、脱战、can_remote_ammo和至少一组150金币；余额足够仍返回remote_exchange_parameter_unverified |
| 远程回血 | 规则§5.2.1/表5-6，PDF75/78：脱战，6秒后增加当时上限血量60%；通信有can_remote_heal，但未定义Common6.param的具体数值语义 | 开源面板固定param60，ExchangeCommandPolicy对6允许1..100，均不能把规则里的60%变成已确认线参数 | 增加当前官方费用/新鲜余额必要检查；参数与执行结果仍未核验，不新增回血倒计时或推断成功 |
| 部署进入及冷却 | 规则§5.6.1，PDF112–113：远程优先、己方可部署区域、退出8秒后才能再进入；通信§2.2.26/27仅mode和status，PDF70 | MainWindow/DeployModePanel筛选角色及本地面板；GlobalSpecialMechanism只有堡垒占领计时，非部署冷却。RobotPosition仅云台手客户端归属信息，世界坐标/场地图形不能在未标定时直接判英雄区域 | 进入新增必须明确NotDeployed；已部署或未知状态先拒绝，明确未部署且4/4仍阻塞区域/冷却。死亡时退出实际部署继续允许必要资格检查 |
| 能量机关剩余机会 | 规则§5.5.2，PDF101–102：小机关在开始/1:30授予机会，大机关在3:00/4:15/5:30授予机会，可累积；通信RuneStatusSync只有状态、灯臂数、平均环数（PDF71） | TimedEventRules::RuneVoicePromptTracker按时间和本地观察状态跟踪提示，不能证明缺失报告、其他触发、重连以前的使用次数；灯臂数量不是激活机会 | 保持新鲜状态1及步兵的必要检查。返回空只说明已知必要检查通过，实际机会仍必须由完整业务授权建立；普通门关闭，未实现本地推算放行 |
| 云台/哨兵/飞镖身份与费用 | 规则§5.6.4，PDF115：云台手干预哨兵，半自动无需金币、自动每次50金币；通信§2.2.23/31–34及附录二（PDF68/72–73/80–81）区分选择码、消息、机器人ID和选手端ID | MainWindow的机器人/面板角色筛选不是实际操作者登记。clientID、sentry_control/姿态、SentryCtrlResult及DartSelectTargetStatusSync都不提供跨机器人操作者授权或可唯一关联请求的证明 | 保留sentry_operator_unverified/dart_operator_unverified及完整云台许可缺口，不把7/8、0x0106等ID直接替换为一个通用登录身份；普通支持范围不扩大 |

本轮费用校正仅针对远程回血的必要余额检查。规则表5-6括号是`50 + ceil(((420 - remainingSeconds) / 60) * 20)`，使用整数运算`50 + ((420 - remainingSeconds) * 20 + 59) / 60`。固定开源ExchangePanel.qml使用`50 + ceil((420 - remainingSeconds) / 60) * 20`，取整顺序不同：倒计时419秒时官方为51、开源为70；359秒时官方为71、开源为90。不能静默照抄。缺失、负数或大于420的倒计时拒绝计算，不夹取为边界；金币缺失/过期不当零余额。付费复活/解除反制沿用其独立公式，不套用该回血公式。

普通兑换仍按通信Common1的10发倍数和42mm的1发单位处理；不采纳开源ExchangeCommandPolicy对Common1/2使用100/10步长的额外限制。区域、累计兑换、远程参数、机关机会、部署冷却和跨机器人权限的缺口均已完成本轮字段核对，后续取得对应官方流程/接口再补；不重复把未知项写成“已可用”。本轮不改schema、QML或普通控制许可，详细软件验收只记Edit.md。

## 17. 哨兵、飞镖与能量机关的完整报告映射

依据通信V2.0.0 §2.2.29–32（PDF阅读器71–72／印刷70–71），复用唯一GameData接收、验证与逐主题过期。没有新增订阅、状态仓库或Protobuf定义；普通诊断state和原始reports读取同一有效样本。

| 报告 | 有类型状态与诊断字段 | 含义与边界 |
| --- | --- | --- |
| RuneStatusSync | rune.code、activated_arms、average_rings | 状态原码，灯臂数量与原始浮点环数；缺失不补0，非有限值在入站拒绝并保留前一个有效样本。平均环数仅大机关正在激活有效，而该报告无大/小机关标志，不生成“有效成绩”或剩余机会 |
| SentryStatusSync | sentry_status.posture_id/posture、weakened、powered | 1/2/3为进攻/防御/移动；缺失或未知原码的posture为unknown。两个可选布尔独立保留，false不是缺失；姿态不等于自动/半自动控制方式或操作者授权 |
| DartSelectTargetStatusSync | dart_status.target_id/target、gate_code/gate | 目标1..5为前哨站、基地固定、随机固定、随机移动、末端移动；闸门0/1/2为关闭/开启中/已开启。未知码原样保留并映射unknown，缺失不夹取为默认目标或已关闭。gate_code是报告的uint32，与上行bool open区分 |

固定MIT e291f2f的NetworkManager订阅SentryStatusSync，但其processMqttMessage无对应分支；本地迁移补齐映射，未复制另一个哨兵状态所有者。开源飞镖updateSiloStatusFromSync用qBound夹取目标1..5和闸门0..2，且把相同报告当作结束“切换中”的依据；本地不采纳未知码夹取或请求确认推断。开源updateRuneStatusSync使用默认getter更新灯臂/环数、把非有限值归零并维护本地提示/队伍激活推断；本地只保留已验证字段，不合并默认值、提示计数或推导机会。

每条有效新报告完整替换该主题的旧样本，缺字段不会继承旧字段；各主题独立过期，断线、身份重置和会话重置清报告。处罚持续约束仍独立保留。哨兵姿态报告、sentry_result_report和飞镖状态均没有唯一请求ID，不转成具体本地命令ACK、不自动重发或开放跨机器人控制。UI设计、普通角色范围和business/source/map许可保持现状；软件验证细节只追加Edit.md。

历史只读探针继续使用同一snapshotJson，输出也包含control_constraints。其快照中的false只是本地默认，无控制授权含义；报告字段的精确比较单独核对这段元数据后，再比较实际报告，不把断线后的元数据误判成残留比赛报告。

## 18. 工程装配完整报告与剩余下行落点

TechCoreMotionStateSync依据官方通信V2.0.0 §2.2.22（PDF阅读器67–68／印刷66–67），同一GameData的tech_core现已保留全部8个可选uint32字段。沿用接收验证、逐主题新鲜度和完整样本替换，不增加订阅、状态所有者或工程面板。

| 线字段 | 领域/诊断字段 | 含义与限制 |
| --- | --- | --- |
| maximum_difficulty_level | maximumDifficulty / maximum_difficulty | 当前最高可选难度，不是当前所选难度 |
| basic_state | basicState / basic_state | 1初始位置、2运动中、3已到对应位姿；保留未知码，不自动确认装配 |
| putin_state、move_state、rotate_state | putIn、move、rotate / put_in、move、rotate | 0未执行、1已执行对应步骤，保持原码和缺失 |
| enemy_core_status | enemyCoreStatus / enemy_core_status | 仅己方四级装配可用时有效：0敌方未在装配、1敌方正在非四级装配、2敌方正在四级装配；未知码不夹取，未满足有效范围不生成敌方状态结论 |
| remain_time_all | remainTimeAll / remain_time_all | 己方装配总剩余时长，仅己方正在四级装配时有效 |
| remain_time_step | remainTimeStep / remain_time_step | 己方单个步骤装配间隔剩余时长，同样仅己方正在四级装配时有效 |

该节没有标明两类时长的单位，也没有当前所选难度或唯一请求ID。时长按原始uint32保存，不加秒后缀、换算、扣时或把0当超时/成功/可确认步骤。最高可选难度与基础位姿不能单独证明当前四级装配；即使收到不符合有效范围的字段也保留原始报告，不把它转换成有效业务结论。显式0、缺失和未知码分别保留，uint32上限不窄化到有符号int；无效/畸形/过期/未来报告不覆盖前一有效样本。

固定MIT e291f2f的GameData::updateTechCoreMotionStateSync直接读getter并覆盖默认初始化为0的结构；本地沿用字段对应关系，使用has_避免把缺失补0。上游ExchangePanelEngineer.qml把remainTimeAll绑定为assemblyCountdownSeconds，以int读取时长并维护本地四级请求倒计时；不将该UI约定当官方线单位或确认依据。原有businessCommandBlock必要检查不改，普通完整业务/来源/地图许可仍关闭。

当前24个已订阅主题均有领域块（包括第19–23节）；第22节核查的18项字段已按第23节补齐，当前mapper逐字段引用检查没有漏项。这不表示全部字段条件有效、已进入HUD或实际可操作，展示与动作入口缺口仍按第22节登记。CustomByteBlock始终保持有界不透明字节，不采用开源私有H.264分支。

RadarInfoToClient是未订阅的另一个官方下行，外层schema冲突仍单独阻塞，不能计入上述已接收主题或从雷达项目补猜。字段完整性、UI入口缺口与操作权限分别登记；详细软件验证只追加Edit.md。

## 19. 全局特殊机制与一次存活期间伤害统计

GlobalSpecialMechanism依据官方通信V2.0.0 §2.2.6（PDF阅读器55／印刷54），同步正在生效的全局特殊机制。同一GameData的special_mechanisms.entries逐项配对mechanism_id（uint32）和mechanism_time_sec（int32秒）：

| ID | kind | 官方含义 |
| --- | --- | --- |
| 1 | ally_fortress_occupied | 己方堡垒被对方占领计时 |
| 2 | enemy_fortress_occupied | 对方堡垒被己方占领计时 |
| 其他，包括0 | unknown | 保留原始ID，不推断含义 |

先校验数组等长，再保留原顺序、重复ID、未知ID及完整有符号时间范围。time_sec是收到的秒值，不本地扣时，不按绝对红蓝推断阵营，不当部署冷却或机关机会。固定MIT e291f2f的GameData::updateGlobalSpecialMechanism按较短数组截取、负数归零、仅保留最后的已知机制并触发语音；本地不导入这些合并、夹取或告警。

此消息由重复字段组成，空列表没有has_标记。本地按“当前正在生效的完整列表”作出实现约定：合法的空消息（零字节或空packed数组）表示收到空列表，清除上一份机制条目，保留该报告的接收时间。未收到/过期则整个special_mechanisms缺失，两者不同。仅含未知字段的消息仍拒绝，其他全缺失的单值主题仍拒绝；畸形、数组不等长、过期、未来、retained及未完成订阅的报告不能清除旧数据。没有扩大普通操作权限。

RobotInjuryStat依据§2.2.8（PDF阅读器56–57／印刷55–56），报告机器人一次存活期间累计受伤统计。同一GameData的injury逐字段保留以下可选uint32：

| 线/诊断字段 | 领域字段 | 含义 |
| --- | --- | --- |
| total_damage | totalDamage | 总伤害 |
| collision_damage | collisionDamage | 撞击伤害 |
| small_projectile_damage | smallProjectileDamage | 17mm弹丸伤害 |
| large_projectile_damage | largeProjectileDamage | 42mm弹丸伤害 |
| dart_splash_damage | dartSplashDamage | 飞镖溅射伤害 |
| module_offline_damage | moduleOfflineDamage | 模块离线伤害 |
| offline_damage | offlineDamage | 异常离线伤害 |
| penalty_damage | penaltyDamage | 判罚伤害 |
| server_kill_damage | serverKillDamage | 服务器杀死伤害 |
| killer_id | killerId | 击杀者ID；未检测到击杀者时为0 |

缺失、显式0和uint32上限分别保留，完整新样本替换旧样本，不补0、不窄化、不校验未规定的类别求和关系。固定开源把模块/异常离线合并为offline、把判罚/服务器杀死/飞镖溅射合并为warning，并未保留killer_id；这里保留十项，避免丢失分类。killer_id既不是自身身份，也不证明存活/死亡；不从统计变化生成击杀事件或扣当前血量，不在本地死亡、复活或回合变化时猜测统计归零。

两个主题均沿用同一GameData的逐主题新鲜度、身份切换及会话/断线清空；不增加订阅或第二状态源。诊断字段不增加HUD控件、地图绘制、请求确认或业务许可。软件验证和构建结果只追加Edit.md。

## 20. 哨兵路径规划与地图点击报告数据

依据官方通信V2.0.0 §2.2.16/17（PDF阅读器64–65／印刷63–64），复用同一GameData已接收的RobotPathPlanInfo和MapClickInfo，增加有类型报告与诊断，不增加订阅、显示入口或指令。各块沿用独立新鲜度、整份替换、身份/会话重置与断线清空。

| 路径线字段 | path_plan诊断 | 含义与边界 |
| --- | --- | --- |
| intention | intention | 1攻击、2防守、3移动；保留缺失、0和未知原码，不转成动作 |
| start_pos_x、start_pos_y | start_x_dm、start_y_dm | 起点原始uint32，官方单位分米；不窄化、不归一化，原点/方向及UI标定待核验 |
| offset_x、offset_y | offsets的x/y配对条目 | 相对起点的有符号增量，保持顺序及-128..127原值，不累加或裁剪；不生成UI路径点 |
| sender_id | sender_id | 原始发送者ID，不默认本车、己方哨兵或认证身份 |

官方数组定义为49项。沿用既有接收策略：X/Y等长、最多49对、逐项范围校验；短数组可作为报告数据保存，不能称为完整官方轨迹或可绘制路径。重复数组本身无presence，空数组与缺省编码等价；有其他已知字段的路径报告可替换为无偏移列表，但全缺失/未知字段独占报告仍拒绝。起点/发送者/意图各自保留可选性，报告不完整时不从旧样本补齐。

固定MIT e291f2f的NetworkManager将路径交给GameData::updateSentryPath，后者用280/150常量猜测0..1坐标或分米，夹取归一化值，负偏移因而可能丢失，且没有保留发送者/意图。本地保留原始数据，不导入场地常量、归一化猜测或路径绘制，不把起点1分米误当归一化1。

| 地图线字段 | map_click_report诊断 | 含义与边界 |
| --- | --- | --- |
| is_send_all | send_all | 0指定客户端、1除哨兵、2含哨兵；保留未知原码 |
| robot_id | robot_ids | 固定7字节目标列表，按字节保留全部顺序、0填充与未知ID；缺失不同于七个0 |
| mode | marking_type | 1攻击、2防御、3警戒、4自定义；不是标记模式 |
| enemy_id | enemy_id | 标定的对方ID，保留原值，不推算阵营或当前目标 |
| ascii | ascii | 自定义图标码，仅marking_type=4有效；官方可发送字母C..L、N、O、Q..Z，收到字段仍按原始uint32保存，不据无效范围生成图标 |
| type | marking_mode | 1地图、2对方机器人；不是标记类型 |
| map_x、map_y | x、y | 原始有限float，保留负值、0和单字段缺失；单位、原点及UI换算未核验，不裁剪或绘制 |

地图报告没有发送者字段；robot_id是接收目标，enemy_id是标定目标，两者不能当发送者、本车身份、来源认证或跨机器人权限。领域块的接收时间来自现有GameData会话，不是消息携带的发送时间。所有可选字段完整替换，收到未知码/条件不成立的ascii也仅保留数据，不生成业务结论。

固定开源漏订阅/处理该官方地图同步主题，仍以MapClickInfoNotify发送旧格式；本地沿用此前已核验的MapClickInfo下行与MapClickCmd上行区分，不导入旧主题或颠倒mode/type注释。报告无唯一请求ID，不能从坐标一致或重复报告确认本地地图指令；传输PUBACK亦不等于地图操作执行。普通地图角色/来源/标定许可保持现状，现有HUD地图不读取这些未标定值；软件验证只追加Edit.md。

## 21. 自定义不透明字节状态

官方通信V2.0.0 §2.2.20（PDF阅读器67／印刷66）仅定义optional bytes data，最大2.4kbit即300B；主题表（阅读器47／印刷46）注明机器人经图传链路上传、机器人端对应0x0310、最高QoS1/50Hz。它没有定义内部应用载荷、视频封装或机器人状态结构。

同一GameData的customBytes.data保存原始uint8字节，诊断custom_bytes.data_hex以小写十六进制无损表达，不作文本解码。data显式存在但为空时保存空字节并替换旧载荷，原始reports的bytes字段也显式转十六进制字符串，避免空QByteArray经QVariant JSON变成null；未收到/过期时整个块缺失。字段缺失、未知字段独占、畸形封装、超300B、过期或未来消息拒绝且不覆盖旧样本，沿用会话/身份重置及断线清空、retained与订阅防护。不接受无Protobuf封装的raw回退，不修改统一接收策略或频率限制。

固定MIT e291f2f的NetworkManager会把该载荷送customVideoPayloadReceived，303B且带0aac02前缀时剥离三字节，又对28..100B数据尝试memcpy为RobotCustomStatus，填摩擦轮、底盘、电容和角度；身份缺失时还取机器人列表第一项。本地不导入这些应用约定，不剥离内嵌前缀、不恢复私有H.264/第二图传，不按长度推断结构或归属。该报告不更新HUD动作标志、姿态、血量、事件或身份，也不生成控制确认。没有既有UI展示入口，诊断仅按显式已有诊断通道输出，不增加普通运行载荷日志。

## 22. 已订阅主题的字段与UI/动作落点核查

以canonical client.proto、state_mapping.cpp、GameData::officialSnapshot、TerminalController::applyHistoricalSnapshot及现有动作分派为依据，逐字段对照而非仅统计主题。下表是M3.13核查时的18项缺口（robot_bullets整条重复数组计一项），M3.14已按第23节补入领域状态，保留此表用于追踪对应字段；不从缺失值补默认数或提前接UI。

| 主题 | 已补齐的字段 |
| --- | --- |
| RobotStaticStatus | robot_type、max_heat、heat_cooldown_rate、max_power、max_buffer_energy、max_chassis_energy |
| RobotDynamicStatus | current_chassis_energy、current_buffer_energy、current_experience、experience_for_upgrade、total_projectiles_fired、out_of_combat_countdown |
| GlobalUnitStatus | robot_bullets、total_damage_ally、total_damage_enemy |
| GlobalLogisticsStatus | total_economy_obtained、tech_level、encryption_level |

其他已订阅主题的线字段均在对应mapper引用；这不表示条件有效、全部HUD可展示、完整操作权限或现场验收。映射后的UI/动作落点如下，缺入口按已有约束登记，不擅自增控件：

| 主题组 | 现有落点 | 仍未完成/禁止推断 |
| --- | --- | --- |
| RobotStaticStatus | ownTeam/selfIndex/level、HP上限、配对报告身份；连接/上场/存活/性能码参与必要业务检查 | 上表静态字段已进typed诊断；报告身份不是认证，缺角色选择入口不能自行设性能 |
| RobotDynamicStatus | HP、热量、允许发弹量；远程资格等参与必要检查；缓冲能量属性按第24节接线 | 热量/缓冲能量无现有可见QML控件；底盘能量不等于实时功率，不能误填W或物理余弹 |
| GlobalUnitStatus | 相对五槽血量、基地血量和前哨状态 | 原始有符号弹量数组/总伤害已进typed诊断，不猜弹量槽位；不补无人机血量或基地最大值，不拿数组作雷达点 |
| GlobalLogisticsStatus | 本方金币及必要余额检查 | 总收入/等级已进typed诊断，uint64总收入为无损十进制字符串；不据科技/加密等级放行工程/雷达 |
| GameStatus | 阶段、比赛概要；暂停/阶段参与门检查 | 不本地推进比赛；结果有效范围与请求确认分别处理 |
| Event | 有界纯文本历史消息和序号 | 不解析param驱动玩法，不把序号当官方请求ID或完整事件历史 |
| PenaltyInfo、RobotModuleStatus、RobotRespawnStatus | typed诊断；处罚持续约束、复活必要资格，Y/Shift+F1有类型动作入口 | 模块与复活没有新增展示；报告过期/模块在线/读条完成不解除处罚或确认复活 |
| RobotPosition、Buff | typed诊断 | 未接现场坐标/云台或汇总Buff展示，不能默认本车或推算增益时限 |
| RobotPerformanceSelectionSync、DeployModeStatusSync、TechCoreMotionStateSync | typed诊断和对应动作必要资格；Performance/Deploy/Assembly有类型入口 | 无既有选择/部署/装配控件，缺所选参数；状态变化不是唯一请求确认 |
| RuneStatusSync、SentryStatusSync、DartSelectTargetStatusSync、SentryCtrlResult、AirSupportStatusSync | typed诊断和部分必要业务检查；对应命令有类型入口 | 无既有操作/状态面板，机会、跨机器人权限和完整授权未核验 |
| GlobalSpecialMechanism、RobotInjuryStat | typed诊断 | 无既有机制/统计展示；不本地倒计时、推算血量或生成击杀事件 |
| RobotPathPlanInfo、MapClickInfo | typed诊断；MapClickCommand有类型入口 | 无现场轨迹/地图标定及点击动作入口，不绘图或确认执行 |
| CustomByteBlock | typed不透明字节诊断 | 无应用格式与UI入口，不按开源私有结构填现有动作/电容/角度 |

键鼠输入与复活快捷键已接统一入口；CustomControl、地图、兑换/远程补给、性能、部署、装配、机关、哨兵、飞镖及空支援均不能仅凭有类型Command称为已可操作。普通businessPolicyReady、键鼠独占来源和地图许可尚不提供，M3仍未整体完成。RadarInfoToClient不在24订阅主题内，外层schema冲突、地图标定、实物和官方服务验证仍独立待验。完整UI/动作开放应按具体入口及权威流程推进，不因补完领域字段自动解除这些边界。

## 23. 静态、动态、全局单位与经济完整字段映射

依据官方通信V2.0.0 §2.2.4/5（PDF阅读器53–55／印刷52–54）、§2.2.10/11（阅读器58–60／印刷57–59），同一GameData的既有self_config、self_dynamic、units、economy补齐第22节18项字段。单值字段保留has_，新样本完整替换；畸形、非有限float、过期或未来报告不覆盖旧样本，沿用独立过期、身份与会话/断线清空。

| 官方字段 | 领域/诊断字段 | 含义和边界 |
| --- | --- | --- |
| robot_type | robotType / robot_type | 原始uint32类型码；本节未给类型枚举，不由编号回退或拼出角色授权 |
| max_heat | maxHeat / max_heat | 最大热量；保留0、缺失和uint32上限，不推断实际当前热量 |
| heat_cooldown_rate | heatCooldownRate / heat_cooldown_rate | float冷却速率，每秒；保留小数及有限原值，不窄化或本地预测降温 |
| max_power | maxPower / max_power | 最大功率，不能替代实时功率；本节未标单位，不加W后缀 |
| max_buffer_energy、max_chassis_energy | maxBufferEnergy、maxChassisEnergy / max_buffer_energy、max_chassis_energy | 两类最大能量，原始uint32；本节未标单位，不默认J或百分比 |
| current_chassis_energy、current_buffer_energy | chassisEnergy、bufferEnergy / chassis_energy、buffer_energy | 当前剩余底盘能量和缓冲能量；能量不是实时功率，保留原值不夹取到最大值 |
| current_experience | experience / experience | 当前经验uint32，不窄化或由其推进等级 |
| experience_for_upgrade | experienceForUpgrade / experience_for_upgrade | 距下一次升级仍需获得的经验，不是经验总门槛，不求和或生成经验进度比例；0不证明升级完成 |
| total_projectiles_fired | totalProjectilesFired / total_projectiles_fired | 累计已发弹量，不当物理余弹或兑换计数 |
| out_of_combat_countdown | outOfCombatCountdown / out_of_combat_countdown | 脱战倒计时原始uint32，本节未标单位，不命名为秒/本地扣时；0或缺失不证明脱战/远程资格 |
| robot_bullets | robotBullets / robot_bullets | 原始有符号int32数组，保留顺序、负数、0及上下限；无角色/阵营索引推断 |
| total_damage_ally、total_damage_enemy | totalDamageAlly、totalDamageEnemy / total_damage_ally、total_damage_enemy | 相对己方/对方累计总伤害，保留uint32，不与一次存活期InjuryStat合并或生成击杀 |
| total_economy_obtained | totalObtained / total_obtained | 己方累计总经济uint64，领域无损整数，诊断固定十进制字符串，包括0和2^64−1，避免double精度损失 |
| tech_level | techLevel / tech_level | 己方科技等级，工程曾装配的最高难度；不是当前选择或当前最高可选难度 |
| encryption_level | encryptionLevel / encryption_level | 己方加密等级，雷达解析信息波机制；保留未知码，不开放雷达/跨机器人权限 |

robot_bullets表述为“己方机器人剩余累计发弹量”，该节未给槽位数/编号顺序；不能套用robot_health的己方/敌方十槽定义。继续沿用既有接收策略：空或10项才接受，这是本地兼容检查，不宣称官方长度已确定。领域保存原始数组，空重复字段无presence且不输出该诊断键；含其他已知字段的新报告会清除前一数组。十个显式0与空/缺省不同，负数不归零；来源说明完善后才考虑槽位解释或调整接收长度。

固定MIT e291f2f的静态/动态更新将多项uint32窄化为uint8/16、float冷却速率转uint16，并将currentChassisEnergy赋给power；经验当前值与仍需值求和后夹取，弹量负数归零并按血量索引分配。经济结构虽有uint64总收入，但getter无presence且显示余额转int。本地只采纳官方字段对应关系，保留原始类型与有效性，不导入这些默认值、窄化或推断，不改已有必要业务门。

字段引用审计覆盖当前24个已订阅主题的每个线字段，没有剩余“仅原始reports而未被mapper引用”的项；这只是结构覆盖检查，不能代替上述条件/数组语义、UI绑定、完整控制授权或现场验证。本轮不修改HUD显示绑定或控件，实际能接的既有读数仍按第22节和CLAUDE下一步任务逐项核对。

## 24. 现有读数接线与实物交接

RobotDynamicStatus.current_buffer_energy接到已有bufferEnergyLabel，显式0显示“0 J”，保留uint32原值不夹取、不添加比例或本地耗能计算。单位依据通信V2.0.0 §2.2.10–11的缓冲能量字段和规则V2.2.0 §5.1.4（阅读器71页）的Z/Q焦耳定义；不把规则60J默认上限填进缺失报告。

current_heat继续接barrelHeatLabel，remaining_ammo仍只接已有“允许发弹量”，不填“弹丸余量”。底盘能量不是瞬时功率，chassisPowerLabel与物理ammoLabel保持未知。每次官方快照先清读数，缺失/逐主题过期/身份切换/断线不会继承旧值；显式诊断含buffer_energy、heat、power、physical_ammo，便于普通exe的wire验证，不新增默认载荷日志。

现有QML没有消费bufferEnergyLabel或barrelHeatLabel的可见控件。本轮只补原有属性/诊断及既有允许发弹量验证，未增加热量/缓冲显示、改变圆弧或UI设计；这两项展示仍待入口设计。既有普通告警改为“控制权限待核验”，对应当前有类型指令已接但普通权限仍关闭的状态。

剩余24主题/11命令、输入生命周期、业务必要条件和回执边界已按第14–23节梳理。缺少来源选择/完整业务权限/处罚解除及地图依据的部分不能由本机测试补出；缺UI入口也不是硬件到货后自动完成的功能。后续取得权威依据和入口设计后继续实现，再验实机操作，不能称整个M3完成。

到货顺序与普通EXE示例见[实物联调清单](hardware-integration.md)，随新便携包提供。配对、官方服务版本/远端端点、实际镜头与HEVC、断链/拔插、1080p60/长时/端到端、输入与真实指令执行分别验收；干净Windows部署另验。软件构建和回归明细只追加本机Edit.md。

## 25. 可见读数与赛事参数入口

用户列明读数、操作入口及Git快照的剩余任务后继续要求推进至实物联调。本轮在原Readout样式中增加热量/缓冲能量，在F1面板添加“赛事操作与参数”入口，以同一Theme的可关闭面板接已有有类型方法；未重做准星、圆弧、地图、视频比例或整套上游UI。第22/24节“无可见热量/缓冲控件”和“无性能/部署/装配入口”是此前状态，由本节更新。

面板含性能、部署、装配、17/42mm允许发弹量兑换、免费/付费复活、机关、飞镖、哨兵及空中支援选择，落到8个具名C++请求方法和唯一submitRm2026Command。名称/码据通信V2.0.0 §2.2.10、21、23、25–35（PDF58–59、67–74）；开源SettingsPanel/ExchangePanelEngineer仅对照窗口→有类型方法的职责，不导入默认选择、窄化getter、本机机会/配额或结果确认假设。

射击/底盘“不更改”省略optional，哨兵“不更改”与自动控制显式0分开；有效参数由既有encoder复核，负难度/超过4在UI边界拒绝。不默认选中性能/部署/装配等枚举；弹量数量明确显示初始10/1且可修改。参数不能提供businessPolicyReady、keyboardSourceSelected、mapCalibrated或注册操作者权限。缺新鲜匹配报告ID不能提交，提交携带打开面板时的目标ID，身份失效关闭并清空；关闭占屏后再进入相同指令门，旧 held 输入仍被隔离。排队只表示等待传输，未关联赛事/执行回执，送达未知不重发。

远程补给参数、私有CustomControl应用格式、地图/雷达及完整来源/处罚/业务授权仍缺权威依据，不提供猜值控件或普通解锁开关。公开资料补查未取得可核验的新schema或来源确认/处罚解除接口，不能据检索结果断言官方不存在这些接口。当前可以继续验配对/只读MQTT/HEVC与恢复，控制开放先补对应官方版本流程。
