pragma ComponentBehavior: Bound

import QtQuick

// 统一设计令牌。作为同目录普通组件实例化（而不是 singleton），
// 这样正式模板（qt_add_qml_module）与测试直接加载 qml/Main.qml 的两条路径都能解析。
// 说明：遮罩、按钮状态等需要特定透明度的颜色仍以 #AARRGGBB 字面量写在原处，
// 但保持与前缀同色，避免渐变出现灰边。
QtObject {
    id: theme

    // —— 底色与面板 ——
    readonly property color ink: "#071019"           // 窗口底色，与 HUD 遮罩同源
    readonly property color surface: "#0B1620"
    readonly property color surfaceRaised: "#11222F"
    readonly property color hairline: "#1E3444"

    // —— 文字三级 + 最弱一级 ——
    readonly property color textPrimary: "#E8F3F8"
    readonly property color textSecondary: "#A7BECB"
    readonly property color textMuted: "#7C96A4"
    readonly property color textFaint: "#5C7482"

    // —— 有限强调色 ——
    readonly property color cyan: "#45D6E8"
    readonly property color amber: "#F2B84B"
    readonly property color danger: "#FF5F68"
    readonly property color ok: "#5CE0A3"

    // —— 颜色语义被窗口测试精确断言，以下三个字面量不得更改 ——
    readonly property color teamBlue: "#49D7EF"      // 我方=蓝 / 敌方=红时的蓝
    readonly property color teamRed: "#FF626C"       // 我方=红 / 敌方=蓝时的红，也是动作激活色
    readonly property color inactive: "#82939F"      // 动作提示未激活色

    readonly property color glass: "#B00A1924"

    // —— 字体 ——
    readonly property string uiFont: Qt.platform.os === "windows"
        ? "Microsoft YaHei UI" : "Noto Sans CJK SC"
    // 仅用于纯数字读数，保证数字等宽对齐（Windows 必装 Consolas）
    readonly property string numFont: Qt.platform.os === "windows"
        ? "Consolas" : "monospace"

    // —— 字号阶梯 ——
    readonly property int fsDisplay: 34
    readonly property int fsValue: 22
    readonly property int fsTitle: 14
    readonly property int fsBody: 12
    readonly property int fsLabel: 11
    readonly property int fsMicro: 10

    // —— 圆角 ——
    readonly property int radiusSm: 6
    readonly property int radiusLg: 10
}
