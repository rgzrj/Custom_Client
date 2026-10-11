# VT03/VT13 图传配对框架

当前只支持 VT03/VT13。框架管理本机配对会话和证据；实际对频仍由官方主控/客户端完成。没有发现已核对开源路径提供VT03/VT13的公开电脑自动配对接口，因此不生成 USB、串口、IPC 或角色写入命令。

## 对照来源与采纳范围

| 来源 | 对照结果 | 本轮采纳 |
| --- | --- | --- |
| RM26_Client，MIT，Fudan University EGA Team，固定提交 `e291f2f65f5a5924509e4e2fe1b148ff9d4b24af` 的 `MqttManager.cpp`、`NetworkManager.cpp`、`VideoReceiver.cpp` | 网络身份、订阅、视频接收分工；请求 clientID 不能证明硬件身份。这些路径未提供 USB 对频实现 | 配对目标独立于本地显示预设和网络请求身份；重试清空旧证据 |
| 已核对的 vtstream-ffplay / RMMock 视频入口 | UDP 解码桥 / 模拟发送端，未提供电脑自动硬件对频实现 | 继续复用现有视频后端，不把出包或出帧当成硬件身份认证 |
| 本地 `规则/RoboMaster裁判系统相机图传模块VT03&VT13使用说明书-CN&EN.pdf`，PDF 阅读器第16页、印刷第15页 | 非日本地区主控菜单与接收端按键对频；日本地区另有 ID/登录流程 | 按设备、地区提供对应操作指引 |

本轮原创实现，不复制第三方源码。检查范围限于上述版本及入口，不能据此断言所有开源项目都没有配对功能。

## 状态和证据

`idle → external_required → video_required → video_review`；还可进入 `identity_mismatch`、`timed_out`、`cancelled`、`disconnected`。

- `beginPairing(region, robotId, timeoutMs=60000)`：新建会话，同时清空人工确认、报告身份与视频证据。设备固定 VT03/VT13，地区取 `global` / `japan`。目标 ID 支持现有操作角色 1/2/3/4/6/7 和 101/102/103/104/106/107；不宣称支持全部官方 ID。
- `confirmExternalPairing(session)`：操作员报告已经完成官方流程；来源记录为 `operator`，不冒充硬件回执。
- `reportPairingIdentity(session, optionalId)`：供 C++ 适配器接入可靠的报告身份。缺失保留未知，不从目标 ID、显示设置、视频像素或 MQTT clientID 推导。匹配只叫 `reported_match`，不等于认证；不匹配清除人工确认和旧画面证据。
- `cancelPairing()` / `pairingDeviceDisconnected(session)`：清空证据并使旧会话失效；重试再次调用 `beginPairing`。
- 有效 QVideoSink 帧只更新本会话视频证据。替换 sink 清除旧视频证据；旧 sink 回调和旧会话代号不能推进新流程。解码器停流只转回等待视频，不推断硬件取消了对频。

尚未完成“人工确认且有有效画面”的流程默认60秒超时；身份不匹配也受此限额约束。完成后视频短暂停流允许同一会话恢复。700ms 视频新鲜窗口沿用本项目既有门槛。这两个时间都属于项目决策，不是官方硬件对频时限。

`pairingDiagnostics` 提供字符串会话代号、状态、指引、目标 ID、报告 ID、人工确认来源和视频新鲜度。`hardwareVerified`、`controlAllowed` 始终为 false。`video_review` 表示人工确认与有效画面同时存在，不表示已认证相机身份。日志记录状态及会话代号，既有视频诊断文本附带配对状态；不添加 QML 控件。

## 官方操作

VT03/VT13 非日本地区：发送端连接主控并通电，接收端开机；主控“模块设置→图传对频”。接收端长按电源键约2秒至提示音，松开，再长按约2秒。发送端绿灯以1Hz闪烁，接收端响两声后静音，再报告人工确认。官方手册说明成功后后续使用无需重复对频。

VT03/VT13 日本地区：主控设置机器人 ID，接收端 USB-C 连接电脑，官方客户端选择对应 ID 登录。接收端断电重启后按手册重新连接操作，不沿用本进程的旧确认。


## 开发入口

在原来已验证的视频启动命令上增加以下可选参数即可新建会话：

```powershell
# VT03/VT13 的独立只读 HEVC 入口；端口、地址和分片起始仍需按设备输出明确填写。
./scripts/run-rm2026-video.ps1 -ExperimentalVideo -VideoPort 39006 -FragmentBase 0 `
    -PairingRobotId 3 -PairingRegion global -DurationMs 1000

```

只有已完成官方操作时才追加 `-PairingConfirmed`。这只是本轮人工报告，不会执行对频。没有目标 ID 时单独指定地区或确认会被拒绝；不提供这些参数则保持原视频入口行为。普通入口不自动创建或确认会话；配对 CLI 参数仅接入上述独立 HEVC 入口。合成 LOCAL HUD 和本地回放不能创建硬件配对会话。

## 验证范围

`rm27_pairing_tests` 验证 VT03/VT13 两地区状态转换、700ms 边界、超时、重试、身份不匹配和失效会话；`rm27_pairing_workflow_tests` 用人工像素验证真实 QVideoSink 信号接线、换源与回放退出。`tests/test_video_launcher.ps1` 检查参数校验，不操作设备。

实机仍需确认：VT03/VT13 官方对频流程、地区差异、目标角色对应真实镜头、断电后的身份保持、拔插和长期恢复。电脑自动配对及硬件认证需要对应公开协议或授权 SDK；当前框架未实现，也不能用本机测试宣布完成。
