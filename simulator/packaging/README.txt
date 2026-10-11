RM27 本机模拟裁判系统 — Windows x64 便携预览

解压整个 ZIP，不要只拷贝 exe。双击 rm27_simulator.exe 即可打开控制台，
无需安装 Python、Qt、外部 MQTT broker 或网页运行环境。
程序不会自动发包。编辑状态后，点击“启动联调”才发送本机测试数据。
客户端另行启动，两个窗口只通过 127.0.0.1 的项目模拟接口交互。
39001 裁判数据 / 39002 探针 / 39003 TS / 39004 只读 MQTT。
不要同时运行多个发包会话。视频需自行选择合法 .ts 文件，不随本包提供。

这不是官方裁判系统，不连接真实赛事设备，不发送机器人控制指令。
H.264 本机联调已验证；H.265 与正式 RM2027 协议尚未验证/接入。
录制是发送端字节时间线，不是客户端接收或屏幕录像。

manifest.json 中 cleanMachineVerified=false 表示仍需另一台干净电脑验收。
licenses/ 保留 Python、Tcl/Tk、PyInstaller 的许可正文。
源代码：https://github.com/rgzrj/Custom_Client （simulator 目录）
