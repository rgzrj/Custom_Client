# RM27 项目模拟器

## 本地视频测试

客户端现在需要 Qt 6.9+（当前已安装 6.11.2）及 Multimedia，使用 Qt 自带 FFmpeg 后端。点击右侧“打开本地视频”选择文件；暂停和播放按钮控制回放，回放期间控制锁定。视频不含音频输出。

使用已有 OpenCV 和 NumPy 环境生成原创 10 秒测试画面：

```powershell
python simulator\generate_video_sample.py build-gui\pattern-h264.mp4
```

也可使用客户端 `--video 文件路径` 启动回放。已实测 H.264；H.265 当前本机样本编码器失败，解码尚未验证。本地回放不能代表赛事 UDP 图传延迟。

## 本机 UDP 解码联调

`--sim-udp-video` 将 Qt Multimedia 的 FFmpeg 后端指向 `127.0.0.1:39003`，接收模拟 MPEG-TS 内的 H.264/H.265；这只是测试容器，**不是 RM2027 官方 UDP 帧格式**。先生成原创测试样本，再分别在两个终端运行：

```powershell
D:\YOLOV8\python.exe simulator\generate_video_sample.py build-gui\pattern-h264.ts
.\run-terminal.cmd -Build -SimUdpVideo
```

```powershell
D:\YOLOV8\python.exe simulator\stream_video.py build-gui\pattern-h264.ts --loop
```

画面首次解码后顶部显示本机视频状态；停止发送约 700ms 后标为过期。首次启动可能等待 TS 头和关键帧。若 OpenCV 无法生成可解码的 H.264 TS，可使用已有的合法 H.264/H.265 `.ts` 测试文件；发送脚本会检查文件大小与 TS 包同步字节，不接收任意网络地址。

## 本机 MQTT 裁判数据联调

没有 RM2027 正式通信协议时，只启用显式 `-SimMqtt` 的只读项目模拟 MQTT 3.1.1：固定 `127.0.0.1:39004`、主题 `rm27/sim/v1/referee`、QoS 0，载荷仍为已有的 `rm27.sim.v1.referee` JSON。它不使用官方 Protobuf、不发布上行指令，也不连接真实 broker。启动客户端后另开终端：

```powershell
.\run-terminal.cmd -Build -SimMqtt
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

回放文件使用 `rm27.sim.replay.v1`，最大 64 MiB，只接受本项目的模拟裁判和视频探针载荷，不接受官方或任意网络目标。

测试：

```powershell
D:\YOLOV8\python.exe -m unittest simulator.test_simulator simulator.test_stream_video simulator.test_sim_mqtt_broker
```
