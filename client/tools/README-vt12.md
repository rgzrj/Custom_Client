> 历史记录：2026-10-09 用户明确取消 VT02/VT12 兼容。本页对应后端和脚本已从活动项目移除，下面的旧命令不再可用；当前入口见 README-pairing.md。

# VT12 真实样本图像实验（开发工具，不随发布包分发）

目的仅是将用户 VT12 接收数据解码，再放进**现有 HUD**，不修改 UI、官方程序、网卡、驱动、固件、防火墙或配对设置。正式未来路径仍是独立的官方 2026 HEVC / UDP3334 参考模块，不能混用两种头格式。

## 已确认与限制

- 配对先由官方选手端完成：当前主控红方步兵 3，接收端 R3 - Standard；发送端应转绿。以 [官方配置与踩坑记录](../规则/VT02-VT12官方客户端图传配置与踩坑记录.md) 为准。
- 本机采样端点：192.168.42.2:58086 → 192.168.42.105:8080；8 字节不解释含义的前缀后为 H.264 Annex-B / 1280×720。不是公开通用 VT12 SDK，也不证明所有固件都相同。
- 样本中没有 IDR；需 FFmpeg `showall` 中途恢复，初期可能有参数 / 参考缺失。开发桥**重新编码**为 MPEG-TS，复用现有 Qt 测试播放器；有额外 CPU 和延迟，尚不适合作比赛低延迟实现。
- 为这个已核对的 60 Hz 短样本显式设置输入时间基准 60，防止短探测退回 FFmpeg 默认 25 fps；这不是实测端到端 FPS / 延迟，也不支持未经核验的变帧率或其他设备配置。
- MPEG-TS 输出使用 `-muxdelay 0 -flush_packets 1`，去掉默认约 1.4 秒的时间戳偏移并立即刷新输出。**时间戳偏移不等于实际排队延迟**：同一短样本的桥接首包对比约 0.156 / 0.141 秒，不能声称此改动降低了 1.4 秒实物延迟。未增大播放器缓存或掩盖溢出错误。
- 参考 [vtstream-ffplay 源码](https://github.com/OpenRoboSouls/vtstream-ffplay/blob/master/src/main.rs)（Apache-2.0）的接收桥思路，未复制代码。它的 HEVC/3334 默认值不用于旧设备参数推断。
- Python 仅开发测试使用；显式使用本机已有 FFmpeg 3.3.3，不下载安装、不随客户端分发官方包内二进制。不得因此把 Python 或这个 FFmpeg 加入正式发布依赖。

## 普通启动已内置自动接收（2026-10-07）

用户实机确认直接解码画面“无延迟”（主观观察，无端到端仪器计时），随后确认普通内置入口实际出图。普通 `run-terminal.cmd` / `scripts/run-terminal.ps1` / `build-gui/rm27_terminal.exe` 已集成同一解码后端，不需要 Python 桥、实验开关、双终端或填写解码器命令；布局及控件保持原样，车辆选择修复仅调整已有数据绑定及设置确认行为。这里“内置”指客户端自动管理解码后端，仍复用本机原安装目录中的 FFmpeg，不把官方二进制复制进程序或便携包。

日常使用：

1. **启动客户端前先连接接收端 USB**，保留已验证的 `192.168.42.105` 本机地址和 `192.168.42.2` 来源。配对继续由官方后台维持，当前红方步兵3 ↔ R3 - Standard；本程序不会自动调频或发送角色/订阅命令。
2. 手动退出官方前台、旧Python桥和旧实验HUD；保留两个配对后台。双击客户端根目录 `run-terminal.cmd` 即可。也可运行 `scripts/run-terminal.ps1`，不带 `-SimUdpVideo`。
3. 客户端启动时检查上述本机IP以及已存在的 `DJI/RoboMasterClient 11.0.0.232_1v1_student/RoboMasterClient/RoboMasterClient_Data/StreamingAssets/RockVR/FFmpeg/Windows/ffmpeg.exe`；二者具备时自动等待UDP8080。收到实际解码像素后自动关闭启动面板并显示原HUD，**不是用灯色或单纯绑定端口判断配对/出图**。首次启动没有该网卡地址时仍走普通入口；先接USB再启动，后插设备则重新启动客户端。本次未增加通用设备/安装目录自动搜索或新款硬件探测。
4. 8080被占用时不共享、不杀进程，提示检查接收端/端口并在后台每2秒重新尝试；手动退出占用前台后可接入。已收到视频后静默700ms，旧帧标为非实时，后台清理本次解码器，2秒后新建独占socket和解码器重接；恢复发送即可恢复画面，无需重开HUD。新会话重新锁有效来源端口、不保留旧解码参考或待显示帧；起始缺参数仍可能产生坏帧，不能视为完整抗丢包/IDR恢复方案。
5. 普通内置模式没有60秒自动退出，关闭客户端只结束其自己的解码子进程，不停止官方后台。配对成功但接收端未发送数据仍只能等待。此模式当前**仅图像，控制保持禁用**，不会根据有画面自动解锁真实指令，也不接入另一模拟裁判源。

旧独立实验脚本仍保留60秒上限且不自动重接。明确的 `-Video` / `-SimUdpVideo` / `-SimMqtt` / `--demo` 启动不启用VT12自动接收；需要普通功能但不接VT12时，可运行 `build-gui/rm27_terminal.exe --no-auto-video`（先按普通脚本加载工具链），或启动脚本加 `-NoAutoVideo`。没有解码器或网卡时不下载安装、不修改网卡或防火墙，普通界面保持可用。换安装目录/网卡IP、新款HEVC接入和发布解码器打包须另行配置与验证。

日志使用 `log/rm27-run-*.log` 的 `VT12_AUTO` 与 `WARN`，原统计字段含义不变：GUI提交耗时不是端到端实测。已用随机环回端口验证端口冲突释放后的自动接入、停流旧帧、恢复数据/来源端口更换、GUI旧帧隔离和永久控制锁定。实物长时负载、重连画面完整性与新款HEVC实物仍待验收。

### 本车显示选择与硬件对频分开

- 按 **F1** 打开原设置面板，选择“我方阵营”和“本车机型”，点击 **返回图像接收**。本车照片、队列表及红蓝显示立即跟随，设置保存后下次启动可恢复；若保存失败则保留面板并显示错误。此操作仅保存本应用设置，不应用网络端点，不发送接收端角色/信道命令，不重启当前播放器或清空出帧计数。改端点字段会被本机配置记住，但不改变当前固定VT12接收链路。
- 选择其他机型不是切换其他车的相机，也不是给主控改身份。当前画面仍来自已配对接收端；实际硬件仍是红方步兵3 ↔ R3 - Standard。无裁判数据时这些是本地显示预设，血量等继续未知，控制禁用；历史合成HUD仍不使用本地身份占位。
- 图像接收模式不开放重装/降级等可下发模式。它不能根据本地模型选择、相机画面或实体绿灯授权控制。

### 自动配对查证状态（2026-10-07）

官方旧款说明书第13–14页要求主控设发送端角色、RoboMaster Client设接收端对应角色，红灯约500ms闪烁为尝试对频，发送端绿为连接；没有在这一操作说明中给出供本程序调用的USB接收端角色设置报文。它也没有证明角色跨断电永久保存，因此不能承诺首次设置后所有重启均无需官方端。

本次实际阅读 [RM26_Client VideoReceiver.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/network/VideoReceiver.cpp) / [NetworkManager.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/network/NetworkManager.cpp)（MIT）、[vtstream-ffplay main.rs](https://github.com/OpenRoboSouls/vtstream-ffplay/blob/master/src/main.rs)（Apache-2.0）和 [RMMock udp_client.go](https://github.com/stydxm/RMMock/blob/main/pkg/stream/udp_client.go)（AGPL-3.0）。这些被检查的入口分别是网络接收/赛事通信、UDP到解码器、模拟视频发送；未从它们取得VT12 USB接收端的角色设置/调频实现。仅阅读、无代码复制，不把该结论扩大为“所有开源都没有”。

用户指定的 [SaltWave旧图传教程](https://bbs.robomaster.com/article/1319340?source=4) 区分主控常规版与单机版，后半节UART设信道作用于单机版发送端；[战空的老图传使用经验](https://bbs.robomaster.com/article/812950?source=11) 同样区分两者。这些社区教程不是USB接收端自动配对SDK，也不能仅据教程建议更换当前已出图固件/驱动或运行另一版本start.bat。本文不复制教程正文或指令、不据社区信道表生成设备命令。

目前配对仍保留已验证官方流程：主控红方步兵3，官方前台选R3 - Standard并观察发送端转绿及实时画面，然后退出前台保留后台，再开自定义端。已配对且仍持续输出时，自定义端只需直接打开，不主动重复对频；失配/断电后的角色恢复需官方端或对应版本正式开放接口。要移除这一步，下一项证据应是官方公开的VT12接收端USB虚拟串口角色/模式设定接口（或支持该设备的授权SDK），而不是继续调整解码器。当前未实现、未测试自动配对，不扫描或占用COM5、不逆向官方后台、不发送猜测命令、不刷固件。

## 直接解码实验（保留诊断入口）

用户确认官方基本实时，而旧开发桥仍落后约两秒。新的 `RM27_VT12_DIRECT_HUD` 默认关闭，独立构建目录 `build-vt12-direct/`，不进入普通启动或发布。只处理已实测的 VT12 H.264 / 720p60：UDP 去除未知语义的 8 字节前缀 → 已安装 FFmpeg **只解码** → YUV420P 像素 → 原 QVideoSink / VideoOutput。没有 libx264 重新编码、MPEG-TS 封装、39003 转发或 QMediaPlayer 播放时钟，也不新增 Python / FFmpeg 开发库依赖。FFmpeg 仍是独立子进程，**不是已完成原生 libavcodec 集成**。

参考 [RM26_Client H264Decoder.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/network/H264Decoder.cpp)、[VideoBackgroundWidget.cpp](https://github.com/ClearWei/RM26_Client/blob/main/src/widgets/VideoBackgroundWidget.cpp)（MIT）的后台直接解码与最新图像显示思想，以及上述 vtstream-ffplay 的解码进程入口；均未复制源码。没有采用 RM26_Client 的 MQTT 接入来推断旧设备封装，也没有把 HEVC/3334 入口套到 VT12。显示使用 Qt 公开 [QVideoSink](https://doc.qt.io/qt-6/qvideosink.html) / [QVideoFrame](https://doc.qt.io/qt-6/qvideoframe.html) API，QML 文件不变。

接收和 FFmpeg 管道在后台线程，GUI 只有一个待提交最新像素帧；界面阻塞时覆盖旧像素，不逐帧播放积压。输入预算、来源 IP / 首个有效来源端口锁定、独占监听和 700 ms 停流退出保留；输入管道积压则停止，不随意丢编码参考帧。实验规范化输出为 1280×720/60，使用 `showall` 处理无 IDR 的短样本，起始参考缺失可能导致坏帧；不承诺分辨率切换、任意固件、自动重连或比赛完整性。

实机操作（不需要管理员）：

1. 先在官方选手端确认持续更新的画面，保持当前红方步兵3 ↔ R3 - Standard 配对；然后**只手动退出官方前台**，保留 RMClientDaemon 和 RMClientAssist_SerialPort。不要运行 start.bat。
2. 关闭旧自定义 HUD；在旧 Python 桥窗口按 Ctrl+C，等它退出。新入口独占 8080，不与官方前台或旧桥同时运行。
3. 新开一个普通 PowerShell，完整执行：

```powershell
Set-Location -LiteralPath 'D:\vs\vscode\c++\huge mas\Custom_Client\client'
$vtFfmpeg = Join-Path (Get-Location) 'DJI\RoboMasterClient 11.0.0.232_1v1_student\RoboMasterClient\RoboMasterClient_Data\StreamingAssets\RockVR\FFmpeg\Windows\ffmpeg.exe'
.\scripts\run-vt12-direct.ps1 -ExperimentalVt12Direct -Ffmpeg $vtFfmpeg
```

脚本先只读检查端口占用，冲突时显示 PID 并停止，不结束其他程序；默认沿用绑定 `192.168.42.105` / 来源 `192.168.42.2` / UDP8080。仅本 PowerShell 的工具链 PATH 改变，不修改系统 PATH、驱动、固件、防火墙、网卡或官方配置。首次没有实验程序时只构建独立目录；需要重新编译时加 `-Build`。**不要同时运行 `-SimUdpVideo` 或旧 Python 桥**。此入口收到像素后直接显示原 HUD，跳过该固定实验不需要的配置确认步骤，控制始终禁用。

4. 默认 60 秒自动结束，也可关闭实验窗口；仅结束它自身的解码进程。用镜头前手部快速动作或同屏计时器对照，记录大致延迟、是否随时间增长、坏帧/卡顿。先测试一次，不通过时不要修改配对、驱动或网卡来补偿视频后端问题。回退只需退出这个入口，重新打开官方前台；旧桥命令和普通 HUD 均保留。
5. 若未出图，贴出 `client/log/rm27-run-*.log` 最新文件末尾的 `VT12_DIRECT` / `WARN` 记录。`accepted` 是输入报文，`decoded` 是解出的像素帧，`presented` 是送入 QVideoSink 的帧数，`droppedPixels` 是被覆盖或过旧的像素帧；`decodeToSubmitMs` 只测解码到 GUI 提交时间，**不包含前面的硬件/无线/输入等待，也不是屏幕实际端到端延迟**。`endToEndMeasured=false` 必须保留。

本机验证：已有私有 ETL 按 60 包/秒环回重放，312 个报文得到 304 个像素帧并提交至 HUD，随后停流标为非实时；短样本缺参数的初始诊断仍保留。另用原创 720p60 图案检查实际窗口颜色、连续出帧、界面暂停 200 ms 后覆盖旧帧、端口冲突、畸形输入、停流和永久控制锁定。可设置 `VT12_TEST_FFMPEG` 后运行 `build-vt12-direct/rm27_playback_tests.exe vt12DirectVideo`。未接真实设备的测试不能证明实物两秒延迟已解决，需上述用户复测；未提供 FFmpeg 时像素用例跳过，不算出图验收。

## 先安全验证抓包回放（旧桥保留）

在客户端根目录 PowerShell 中运行；目录和工具均使用本机已存在的路径：

```powershell
Set-Location -LiteralPath 'D:\vs\vscode\c++\huge mas\Custom_Client\client'
$vtPython = 'C:\Users\34855\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$vtFfmpeg = Join-Path (Get-Location) 'DJI\RoboMasterClient 11.0.0.232_1v1_student\RoboMasterClient\RoboMasterClient_Data\StreamingAssets\RockVR\FFmpeg\Windows\ffmpeg.exe'
& $vtPython .\tools\vt12_video_bridge.py --experimental-vt12 --ffmpeg $vtFfmpeg --bind 192.168.42.105 --source 192.168.42.2 --port 8080 --etl .\log\vt12-netsh-20261006-222229-598.etl
```

此模式只读已有 ETL（最大 16 MiB），不监听设备端口。输出 JSON 中的 `replayFile` 是生成的 `.ts`，诊断与视频均在 `log/`，唯一会话名不覆盖原文件。用现有“打开本地视频”选它，或运行 `scripts/run-terminal.ps1 -Video '完整的 replayFile 路径'`。应看到真实房间画面和既有“本地回放”标签，**这不是实时直连**。

## 短时实物联调（旧转码桥，用于回退）

用户已确认实时相机画面进入原 HUD，连续出图不卡顿；两个 60 秒会话分别接收 3600 / 3599 个有效报文，拒绝与桥接队列溢出均为 0。但用户观察画面落后实物 2–3 秒，Qt 端出现 `Circular buffer overrun`，**实时连通已确认，低延迟与播放稳定性未通过**。桥接计数不包含 Qt 内部缓存。保留已经验证的供电、角色匹配和 IP；工具不会自动配对，也不会替用户停官方进程。

1. 确认发送端已配对转绿。只读查占用：`Get-NetUDPEndpoint -LocalPort 8080 -ErrorAction SilentlyContinue | Select-Object LocalAddress,LocalPort,OwningProcess`。若端口仍被官方程序占用，停止实验，不结束未知进程、不运行 start.bat、不共享端口。
2. 对于端口空闲且接收端仍持续发送的会话，手动打开自定义客户端的**本机测试 UDP 视频**入口：`scripts/run-terminal.ps1 -SimUdpVideo`。启动设置必须保持本机测试端点；检查环回解码端口，默认 39003、探针 39002，两者不同。不要为此切到正式数据源或启用控制。
3. 另一 PowerShell 终端设置上面相同的 `$vtPython`、`$vtFfmpeg`，在客户端根目录运行：

```powershell
& $vtPython .\tools\vt12_video_bridge.py --experimental-vt12 --ffmpeg $vtFfmpeg --bind 192.168.42.105 --source 192.168.42.2 --port 8080 --allow-network-video --output-port 39003 --duration 15
```

4. 客户端仍会标为本机测试视频，控制禁用；这是图像验收路径，不把真实相机图像伪装成正式赛事数据。移动镜头前物体确认画面跟随，不能以 accepted 包数或仅一张静态图代替实时验收。桥的输出只发往 127.0.0.1，不给设备发送包。
5. 最大运行 60 秒；端口绑定失败不启动解码器。队列满或已收流后静默 700 ms 时退出，不自动复用旧参考帧；排除故障后手动重新运行。退出仅终止它自身创建的 FFmpeg 子进程。`accepted/rejected/overflow` 是传输计数，不是实际出帧数或端到端延迟。若官方端退出后设备也停止发送，先核实官方输出会话，不猜激活 / 订阅 / 串口命令。

## 验证入口

标准库边界测试（畸形包、重组乱序/重复/缺片/冲突、流过滤、端口占用）：

```powershell
& $vtPython .\tests\test_vt12_video_bridge.py -v
```

可选环回集成测试：设置 `VT12_TEST_ETL` 为完整抓包路径、`VT12_TEST_FFMPEG` 为已有 FFmpeg 路径后运行同一测试，会按 60 包/秒重放私有真实样本，检查 TS 输出实际解码及断流退出，不连接硬件。

该测试使用绝对时间安排发送，避免每包 sleep 的累积漂移；额外检查桥接首包在 1 秒内到达，短样本 TS 起点约 50 ms。均为本机开发回归门限，不是设备端到端延迟。

现有 Qt 测试另有可选 `vt12CaptureReplay`，设置 `RM27_TEST_VT12_VIDEO` 为生成 `.ts` 后运行 `build-gui/rm27_playback_tests.exe vt12CaptureReplay`（先加载 `scripts/environment.ps1` 的固定工具链）。检查 1280×720 帧像素、连续出帧、控制禁用，并将原 HUD 截图写入 `pitcure/temp/`；未提供私有样本时跳过，不将用户画面加入仓库。

同时设置 `VT12_TEST_FFMPEG` 后，可运行 `build-gui/rm27_playback_tests.exe vt12LoopbackPlayback -o 'log/rm27-vt12-qt-playback.txt,txt'`。只启动本测试自己的有限 FFmpeg 回放，通过随机环回端口验证原 HUD 连续超过 240 帧、720p、模拟模式控制锁定、停止后过期，以及回放时钟到帧时间的差值小于 1 秒；对比默认 / 零 muxdelay。此计时不包括相机、无线链路或桥接解码，不可标为实物端到端延迟。Qt 短探测仍可能显示 `120 tbr`，并不证明真实帧率是 120 FPS。

待验收：实物延迟是否下降、Qt 缓冲溢出、断流后的设备恢复、长时 CPU/GPU/内存、原生低延迟 H.264 解码集成；新款设备的实物 HEVC 输出另行验收。复测先重启自定义 HUD，再按原命令运行 60 秒桥接；记录物体动作与屏幕响应差异、是否随时间变大、溢出发生时刻。不要仅凭绿灯、accepted 数量或时间戳变化判断延迟已修复。临时诊断与生成视频可手动清理，本工具不递归清空日志，不改变既有日志清理策略。
