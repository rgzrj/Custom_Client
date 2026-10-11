# RM27 项目模拟器

## 独立项目结构

本目录是独立 Python 项目，可单独打开和运行，不依赖父目录的启动器或客户端源码。
**下文所有相对路径与命令均在本目录执行。**

- `simulator/`：Python 包，包含控制台、状态、发送、场景回放与测试。
- `run-simulator.cmd`：双击启动控制台。
- `学习/`：模拟器与联调教程。
- `CLAUDE.md`、`AGENTS.md`、`Edit.md`：本项目规则、协作入口与本机日志。
- `video/`：本机视频测试样本（不上传）；普通数据模拟不需要样本。
- `.agents/skills/`：本项目模拟与回归规范。

外层 simulator 是项目目录，内层 simulator 是 Python 包名；这样保留现有 `from simulator...` 导入，不依赖客户端。
客户端是单独安装/运行的接收端，两者仅通过本机网络交互。当前目录布局下，客户端启动器为 `..\client\run-terminal.cmd`；如果只拷贝本项目，另行选择你的客户端安装路径即可。

## 可视化本机联调控制台（推荐）

在本目录双击 `run-simulator.cmd`，或在任意目录运行：

```powershell
D:\YOLOV8\python.exe "D:\vs\vscode\c++\huge mas\Custom_Client\simulator\simulator\control_console.py"
```

只使用 Python 标准库和 Tkinter。当前 `D:\YOLOV8\python.exe` 已验证 Tk 8.6 可用，不需要装 PySide、网页框架或外部 MQTT broker。
启动器优先使用环境变量 `RM27_PYTHON`，其次 `D:\YOLOV8\python.exe`，最后 PATH 中的 `python`。
开发运行要求 Python 3.10+ 与可用 Tk；执行 `.\run-simulator.cmd --check` 可只检查版本、Tk 和默认端口。检测到占用只提示，不关闭其他进程、不修改网卡或防火墙。当前验证基线为 Python 3.13.9 / Tk 8.6.15。
这是独立开发窗口，**不是客户端 HUD 的一部分，不会连接真实裁判系统或发送控制指令**。

“比赛与消息”页下方可编辑双方 **基地 / 前哨** 当前血量与最大血量，点“应用编辑值”发送更新。默认基地 5000、前哨 1500 **仅用于项目演示，不是 RM2027 官方上限**；血量 0 是有效值，不自动推导比赛结果。规范状态按红/蓝保存，切换我方不会交换建筑实际状态。旧场景无此字段时保持未知，编辑器显示空白；全部留空可发送未知状态，不能部分填写或超出上限。断线或省略字段时，客户端不会保留旧建筑血量。

## Windows 便携模拟器包

普通用户解压完整 `RM27-Simulator-*.zip` 后双击 `rm27_simulator.exe`；无需另外安装 Python、Tk、Qt 或 MQTT broker。启动只展示编辑器，按“启动联调”才开始发包；视频联调仍需自己选择合法 TS。客户端是另一个独立发布包，两个包不混在一起。

开发者首次建立**项目内隔离构建环境**（只增加打包工具，不动视觉用的 Python 全局包）：

```powershell
D:\YOLOV8\python.exe -m venv .build-tools\venv
.\.build-tools\venv\Scripts\python.exe -m pip install -r requirements-build.txt
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -ReferenceProject "客户端项目路径"
```

固定 PyInstaller 6.22.3 及其构建依赖；当前发布脚本的运行库/许可基线是本机 Anaconda Python 3.13.9、Tk/Tcl 8.6.15，对应缓存许可必须存在；固定版本见 [toolchain.json](toolchain.json)。它不是任意 Python 安装的通用打包器；换基线先核对 DLL、许可再修改脚本。普通源码运行不需要 PyInstaller，OpenCV/NumPy 仍只供可选样本生成。

脚本先跑单测，再创建带时间戳的新包，保留旧包；生成版本清单、依赖哈希与 ZIP SHA-256，附带 [第三方许可说明](packaging/THIRD_PARTY.md)，不包含 Edit.md。检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-windows-package.ps1 -Archive "发布 ZIP 完整路径"
```

检查在新临时目录验证哈希、隔离 PATH/Python/Tk，运行隐藏窗口离线自检，确认冻结程序可创建控制台且未启动网络/摄像头。**仍需另一台未安装 Python 的 Windows x64 电脑验收**，manifest 的 cleanMachineVerified 保持 false；不把本机检查冒充干净机器通过。

### 先跑通数据

1. 控制台在“比赛与消息”选择我方红/蓝、本车机型、比赛阶段。
2. “双方机器人”编辑血量、最大血量、护盾、等级、弹量与地图坐标。
3. “视频与故障”先选 `udp`、`none`，点击“启动联调”。
4. 在客户端项目运行 `.\run-terminal.cmd`，启动面板进入本机模拟 HUD；不要选择自动产生假数据的离线哨兵演练。模拟端口保持默认 39001。
5. 修改控制台数据，点击“应用编辑值”。本车和队伍栏的血量来自同一状态；切换阵营只改变我方视角，不交换红蓝车辆状态。

`remaining_seconds` / `paused` 是控制台的项目模拟字段，目前 C++ HUD 不显示它们。
取消“冻结模拟时钟”后按单调时钟递减剩余秒数，不自动推导阶段、处罚、复活、兑换规则。
血量 0 表示模拟阵亡，但不会凭空增加未经确认的死亡遮挡。每方六个位置沿用现有 HUD，不等同官方编制。

### 再加入实际图传

1. 选择已有的合法 H.264 `.ts` 文件（最多 64 MiB）；`ts` 模式只检查 MPEG-TS 包结构，编码格式由解码器确认。
2. 在客户端项目使用 `.\run-terminal.cmd -SimUdpVideo` 启动客户端。
3. 控制台选 `ts`，设置发送码率，启动。当前原创样本见本项目 `video/console-h264-valid.ts`，已验证 H.264 出帧与断流恢复。它是测试产物，不进入源码仓库。
4. 点“视频：断流 / 恢复”，裁判数据继续发送。点“裁判数据：停 / 恢复”，视频继续发送。

`probe` 只向 39002 发测试探针，**不能解码为画面**；`ts` 才向 39003 发实际视频。
若用本地 MP4 回放，控制台的视频断流不会停止该本地播放器；必须使用 `-SimUdpVideo` 才能验证网络图传中断。
TS 恢复从文件头开始、丢弃断流前延迟队列，不发送旧包突发。

### MQTT 与故障

控制台选 `mqtt`，在客户端项目运行 `.\run-terminal.cmd -SimMqtt -SimUdpVideo`。
控制台自带只读本机模拟 broker，固定 39004，只有一个观察客户端，不接收机器人控制主题，不缓存旧数据。
停止其他占用 39004 的测试 broker，再启动控制台。“裁判数据暂停”保持连接但不发送状态；“MQTT 断线”实际关闭 TCP 连接，恢复后客户端重新订阅。
不要同时运行旧模拟脚本或第二个控制台，避免端口占用及序号流竞争。更换通信或视频源需停止后重启。

视频故障参数：丢包 0–100%、固定延迟 0–1000 ms、额外随机抖动 0–500 ms、概率性延后 60 ms 制造乱序、单比特损坏 0–100%。
故障随机数由种子驱动；队列最多 256 包，满后丢弃新包。状态栏显示发送、丢弃、损坏和排队计数，它们是**发送端统计，不是接收端测得丢包率或端到端延迟**。

### 场景与回放

- “填入预设步骤”提供正常、裁判过期、独立断流恢复、MQTT 断线重连。选好后点击填入，再启动，步骤执行一次。
- “保存场景”保存完整状态、视频设置、随机种子和时间脚本为 `rm27.sim.scene.v1` JSON；“加载场景”只填编辑器，不启动网络。视频路径是本机路径，换电脑需重选文件。
- 步骤时间是相对启动时间的 `at_ms`；`changes` 只允许 `state` 完整替换及 `referee_enabled`、`video_enabled`、`mqtt_enabled` 布尔开关；可选 `event` 沿用项目模拟消息格式。最多 1000 步、24 小时，不执行 Python 或 shell。
- 选择一个**不存在**的 `.rm27-replay` 录制路径后启动；停止会写结束时间，保留尾部静默。不会覆盖旧录像。
- 停止后“选择记录并回放”，支持 0.1–10 倍。先静默 800 ms 使前一会话过期，再按记录间隔发送同样的字节，不需要原视频文件。回放中禁止手动注入。
- 新记录为 `rm27.sim.replay.v2`，兼容旧 v1。包含实际已发送的裁判、探针/TS 包（含注入损坏后的字节）、控制备注和状态快照；MQTT 控制台回放重现断线开关。上限 64 MiB、200000 条、24 小时，达到上限停止联调并保留已写记录。
- **这是发送端记录，不是客户端输入、指令门判定、接收时间或屏幕录像**。相同发送输入不保证操作系统调度或解码帧数逐位相同。控制备注不会自动打开路径或执行代码。
- CLI `rm27_simulator.py --replay` 支持 v1/v2 的 UDP 包回放，但忽略控制备注；重现 MQTT 断线请使用控制台。

以下仍保留原有命令行联调方法。

## 本地视频测试

客户端现在需要 Qt 6.9+（当前已安装 6.11.2）及 Multimedia，使用 Qt 自带 FFmpeg 后端。点击右侧“打开本地视频”选择文件；暂停和播放按钮控制回放，回放期间控制锁定。视频不含音频输出。

控制台、CLI 发包与测试只需要 Python 标准库；仅这个可选样本生成工具需要已有的 OpenCV 和 NumPy 环境。先创建 `video/` 目录，再生成原创 10 秒测试画面：

```powershell
New-Item -ItemType Directory -Force video | Out-Null
python simulator\generate_video_sample.py video\pattern-h264.mp4
```

也可使用客户端 `--video 文件路径` 启动回放。已实测 H.264；H.265 当前本机样本编码器失败，解码尚未验证。本地回放不能代表赛事 UDP 图传延迟。

## 本机 UDP 解码联调

`--sim-udp-video` 将 Qt Multimedia 的 FFmpeg 后端指向 `127.0.0.1:39003`，接收模拟 MPEG-TS 内的 H.264/H.265；这只是测试容器，**不是 RM2027 官方 UDP 帧格式**。先生成原创测试样本，再分别在两个终端运行：

```powershell
D:\YOLOV8\python.exe simulator\generate_video_sample.py video\pattern-h264.ts
..\client\run-terminal.cmd -Build -SimUdpVideo
```

```powershell
D:\YOLOV8\python.exe simulator\stream_video.py video\pattern-h264.ts --loop
```

画面首次解码后顶部显示本机视频状态；停止发送约 700ms 后标为过期。首次启动可能等待 TS 头和关键帧。若 OpenCV 无法生成可解码的 H.264 TS，可使用已有的合法 H.264/H.265 `.ts` 测试文件；发送脚本会检查文件大小与 TS 包同步字节，不接收任意网络地址。

## 本机 MQTT 裁判数据联调

没有 RM2027 正式通信协议时，只启用显式 `-SimMqtt` 的只读项目模拟 MQTT 3.1.1：固定 `127.0.0.1:39004`、主题 `rm27/sim/v1/referee`、QoS 0，载荷仍为已有的 `rm27.sim.v1.referee` JSON。它不使用官方 Protobuf、不发布上行指令，也不连接真实 broker。启动客户端后另开终端：

```powershell
..\client\run-terminal.cmd -Build -SimMqtt
```

```powershell
D:\YOLOV8\python.exe simulator\sim_mqtt_broker.py --team blue
```

停止 broker 后，模拟裁判数据约 600ms 过期；重新启动 broker，客户端会重新订阅。需要同时测视频时给客户端加 `-SimUdpVideo`，再运行上一节的 `stream_video.py`。不要同时运行 UDP `rm27_simulator.py` 与 MQTT broker 向同一客户端灌入两条序号流，否则两者会按现有去重规则竞争；分别测试即可。

## 裁判数据模拟

这是与正式客户端分离的本机测试程序，不是官方裁判系统协议。

- `127.0.0.1:39001`：`rm27.sim.v1.referee` JSON 模拟裁判数据；
- `127.0.0.1:39002`：确定性 UDP 视频探针数据，不是 H.264/H.265；
- 默认 10 Hz 裁判数据、30 包/秒视频探针；
- 模拟数据接入后，客户端进入“模拟”模式并禁止真实指令下发。
- 模拟包可选的 `hud` 对象提供队伍阵营、双方六台机器人血量、金币、重启次数、俯仰角、射速、允许发弹量及归一化地图坐标；这是项目模拟字段，不是官方协议。默认我方蓝队，`--team red` 改为我方红队；两种情况下我方均显示在右。
- `hud` 内可选的 `level`、`shield` / `max_shield` 和 `actions`（`climb`、`fire`、`speed_up`）用于演示左下等级/护盾及右侧动作高亮；旧模拟包不含这些字段时仍可接收，界面显示未知。它们不表示实际控制器输入，也不是官方字段。
- `hud` 内可选的 `yaw_deg`（-180..180）驱动准星下方的横向偏航刻度条；不带该字段时界面显示 `YAW --°` 且不画标记。它是项目模拟字段，与官方偏航语义无关；数值越界或类型错误的整包会被拒绝。
- `hud` 内可选的 `self_index`（0..5）表示「本车」在队列表六个位置中的下标：被选中的格子标「本车」并换成对应机型线稿，左下角自车模块同步显示「本车 · <机型>」。模拟器每 150 包（10 Hz 下约 15 秒）换一台车，便于观察本车随数据动态更换。不带该字段时不做本车标记；越界或类型错误拒绝整包。它同样是项目模拟字段，不是官方协议。
- 模拟包可选的 `event` 对象提供 `id`、`category`（`system` / `robot`）、`text`。第 30–34 包重复发送机器人升级，第 80–84 包重复发送飞镖命中，客户端按 ID 去重并分标签显示；断流超过 600ms 后清空。全部为项目模拟消息，不是官方事件格式。

运行：

```powershell
python simulator\rm27_simulator.py
```

也可一边打开本地 H.264 视频，一边运行模拟器，检查透明 HUD 与视频叠加。红队示例：

```powershell
python simulator\rm27_simulator.py --team red
```

有限运行 50 个裁判数据包：

```powershell
python simulator\rm27_simulator.py --ticks 50
```

循环演示裁判数据中断、恢复和视频中断、恢复：

```powershell
python simulator\rm27_simulator.py --scenario link-recovery
```

记录一个有限的 12 秒故障场景：

```powershell
python simulator\rm27_simulator.py --scenario link-recovery --ticks 120 --record link-recovery.rm27-replay
```

通过同一组本机接收适配层回放：

```powershell
python simulator\rm27_simulator.py --replay link-recovery.rm27-replay
```

旧 CLI 录制文件使用 `rm27.sim.replay.v1`；控制台录制使用 v2。两者最大 64 MiB，均不接受官方协议或任意网络目标。

### 独立模拟雷达

开发联调用第二个终端，在本项目目录执行：

```powershell
D:\YOLOV8\python.exe simulator\sim_radar.py --duration 30
```

固定发送到环回 127.0.0.1:39001（`--port` 可匹配客户端自定义本机数据端口），10 Hz、红蓝双方归一化全量坐标。`--duration 0` 持续运行，Ctrl+C 停止。可与原有模拟裁判同时运行，两路序号和 600 ms 过期独立；停雷达不应使建筑血量过期，停裁判不应刷新或抹掉仍新鲜的雷达坐标，两路均停则清空地图。

这不是官方雷达接口，不接无线、不控制机器人，不实现场地标定或处罚推导；真实协议待独立《通信协议》。只新增开发脚本，旧发布 EXE/录制格式未包含该工具。见 [模拟接口-v1.md](contracts/模拟接口-v1.md#7-独立模拟雷达坐标非官方)。

学习路线见 [学习/README.md](学习/README.md)，项目约束见 [CLAUDE.md](CLAUDE.md)。

测试（在本项目目录执行）：

网络单测每例使用系统自动分配的独立 UDP/MQTT 端口，普通控制台可继续在默认端口发包；排空等待有总超时，持续异常输入会失败而非无限等待。应用默认 39001–39004 不变。GUI 单测创建窗口时隐藏，不自动开启网络。

```powershell
D:\YOLOV8\python.exe -m unittest discover -s simulator -p "test_*.py"
```

接口边界与版本策略见 [contracts/模拟接口-v1.md](contracts/模拟接口-v1.md)。本项目的 test_contract.py 校验 18 个双端固定样例，并核对统一状态真实生成的载荷；客户端用自己的 C++ 实际解析器验同一组样例。联调发布前 `scripts/check-contract.ps1 -ReferenceProject "客户端项目路径"` 比较两份文档/样例哈希，失败不自动覆盖。独立运行和单测不读取客户端源码。
