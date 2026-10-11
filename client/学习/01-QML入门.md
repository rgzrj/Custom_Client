# 第 1 阶段：先看懂屏幕是怎么画出来的

预计 2～3 次学习，每次 30～45 分钟。目标不是立刻做漂亮界面，而是能打开 `qml/Main.qml`，说清楚“哪个对象画什么、哪个属性会变化、点击后执行什么”。以下均为已核实可访问的 **Qt 6 官方中文文档**，以在线原文为准。

## 1. 建立地图：Qt Quick 与 QML（约 30 分钟）

读：[使用 QML 的第一步](https://doc.qt.io/qt-6/zh/qmlfirststeps.html)。先读到“处理用户输入”，后面的动画和自定义类型可以暂时跳过。重点认出 `import`、`ApplicationWindow`、`Rectangle`、`Text`、`Button`、父子对象。

回仓库找：`qml/Main.qml` 的 `ApplicationWindow`、`VideoOutput`、`HudOverlay`。可以把 `VideoOutput` 想成你的摄像机画面，`HudOverlay` 想成叠在画面上的标注层；但它们是可交互的界面对象，不是 OpenCV 画进视频帧里的像素。

## 2. 会读对象和属性（约 30 分钟）

读：[QML 语法基础](https://doc.qt.io/qt-6/zh/qtqml-syntax-basics.html)。先看“导入语句”和“对象声明”；遇到复杂 JavaScript 语法先略过。

在 `qml/HudOverlay.qml` 找 `ownTeamBadge`：`Rectangle { ... }` 是一个对象，`width`、`height`、`color` 是它的属性，内部的 `Label` 是子对象。`id` 只是在本 QML 文件内引用对象的名字，不是屏幕文字。

## 3. 理解“数据一变，界面自己变”（约 30 分钟）

读：[属性绑定](https://doc.qt.io/qt-6/zh/qtqml-syntax-propertybinding.html)。重点理解：`color: hud.ownTeam === "red" ? ...` 是持续有效的关系，不是只运行一次的赋值。阵营更新，徽章颜色会跟着更新。

再读：[信号和处理程序事件系统](https://doc.qt.io/qt-6/zh/qtqml-syntax-signals.html)的“使用信号处理程序接收信号”。在 `qml/OperatorCamera.qml` 找 `onClicked`：它表示用户点击按钮后的处理；在 `qml/Main.qml` 找播放条的 `onClicked`，比较两者。

| 你熟悉的 CV 概念 | 在这个项目里的对应理解 |
| --- | --- |
| 视频帧 | `VideoOutput` 显示的底层画面 |
| 叠加的检测框/标签 | `HudOverlay` 中的 QML 可视对象 |
| 状态变量改变后重画结果 | QML 属性绑定自动更新显示 |
| 鼠标/键盘回调 | QML 的信号处理器，如 `onClicked` |

## 对照本项目做一次练习（约 20 分钟）

先运行 `run-terminal.cmd` 看一眼，再只读代码，不用改文件。尝试回答：

1. 哪个对象让视频铺满窗口？哪个对象负责叠加 HUD？
2. 当 `ownTeam` 从 `blue` 变为 `red`，左上角阵营标识的文字和颜色为何会自动变化？
3. 摄像头入口按钮的点击动作写在哪里？收起之后为什么选择器会隐藏？
4. `onClicked` 属于界面事件，还是裁判协议解析？这两类逻辑应该分别留在哪一层？

能用自己的话回答前 3 题，就可以进入第 2 阶段。暂时不会写 QML 没关系；把卡住的代码行和你的理解发给我，我会按你的理解调整下一阶段，不让材料一下子变难。

> 注意：当前阵营、血量和事件来自项目模拟数据，不等于正式裁判协议；学习界面时也不要把模拟字段当作比赛事实。
