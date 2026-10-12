RM2027 自定义终端 · Windows 便携预览版

1. 将 ZIP 整体解压，然后双击 rm27_terminal.exe。
   无需安装 Qt、Python、FFmpeg、MSYS2 或编译器。
   EXE、DLL 和子目录须放在一起，不要只复制 EXE。
   目标系统：Windows 10/11 64 位，正常安装显卡驱动。

2. 启动面板中点击“离线演练进入”，没有网线也可以进入界面。
   F1 可重新打开启动面板；底部“打开本地视频”可选择自己的视频。
   若包内有 demo.cmd，双击它可加载附带的原创测试视频。

3. 当前版本用于界面预览、本地视频回放与本机联调。
   普通客户端已接入固定开源的 RM2026 数据后端，需在启动面板明确填写
   MQTT 地址/端口、启用 MQTT 并选择红/蓝方及机型；红步兵3请求ID 3。
   未确认配置时不连接，报告身份不等于对频成功。
   普通入口已有明确 RM2026 数据和 HEVC 接入；启动须显式配置地址、
   来源与分片起始号。实物图传、正式 RM2027 协议和指令发送尚未验收。
   离线演练与回放保持控制锁定；未接模拟器时裁判状态显示待接入。
   动态模拟数据需要另行运行项目 Python 模拟器，本便携包不附带。

4. 选手摄像头需要自己的有线 USB 摄像头或采集卡，
   并允许 Windows 相机访问。默认不会开启摄像头。
   个人配置保存在当前 Windows 用户的数据目录。

5. manifest.json 是版本信息，dependency-inventory.json 是校验清单。
   licenses 和 THIRD_PARTY.md 提供运行库版权与来源说明。
   package-smoke.txt 记录本机隔离开发路径后的部署检查。
   testFixtures 记录构建时实际提供的 MP4/HEVC/TS 测试覆盖。
   portableLaunchVerified 不等于另一台干净 Windows 的验收。

6. 请解压到可写目录。运行日志保存在 EXE 同目录的 log，
   临时预览目录为 pitcure/temp；启动时清理 7 天前的专用生成文件，
   不清空未知文件或当前会话。出问题可连同包 SHA-256 提供日志。
   本包不含独立历史协议测试程序；普通客户端的软件接入仍需实物联调验收。

源码与更新：https://github.com/rgzrj/Custom_Client
