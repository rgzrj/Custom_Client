pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    required property var link
    signal chooseMap()
    spacing: 6
    Theme { id: theme }
    component Caption: Text {
        color: theme.textSecondary
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
    }
    component Toggle: CheckBox {
        id: toggle
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
        implicitHeight: 28
        padding: 0
        spacing: 5
        indicator: Rectangle {
            x: 0; y: (toggle.height - height)/2
            width: 16; height: 16; radius: 4
            color: toggle.checked ? "#2645D6E8" : theme.surface
            border.color: toggle.checked || toggle.visualFocus ? theme.cyan : theme.textMuted
            Text {
                anchors.centerIn: parent
                text: toggle.checked ? "✓" : ""
                color: theme.cyan
                font.family: theme.uiFont
                font.pixelSize: 12
            }
        }
        contentItem: Text {
            text: toggle.text
            leftPadding: 21
            verticalAlignment: Text.AlignVCenter
            color: theme.textPrimary
            font: toggle.font
        }
    }
    component SettingSlider: Slider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 24
        stepSize: 1
        background: Rectangle {
            x: slider.leftPadding; y: (slider.height - height)/2
            width: slider.availableWidth; height: 3; radius: 1.5
            color: theme.hairline
            Rectangle { width: parent.width * slider.visualPosition; height: 3; radius: 1.5; color: theme.cyan }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: (slider.height - height)/2
            width: 14; height: 14; radius: 7
            color: theme.surfaceRaised
            border.width: 2
            border.color: slider.visualFocus || slider.pressed ? theme.textPrimary : theme.cyan
        }
    }
    component MapButton: Button {
        id: button
        implicitHeight: 30
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
        background: Rectangle {
            radius: 5
            color: button.hovered ? "#16303F" : theme.surfaceRaised
            border.color: button.visualFocus ? theme.cyan : theme.hairline
        }
        contentItem: Text {
            text: button.text; font: button.font
            color: theme.textPrimary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Caption { text: "显示设置" }
        Item { Layout.fillWidth: true }
        Toggle {
            objectName: "crosshairToggle"
            text: "准星"; checked: settings.link.showCrosshair
            onToggled: settings.link.showCrosshair = checked
        }
        Toggle {
            objectName: "mapToggle"
            text: "地图"; checked: settings.link.showMap
            onToggled: settings.link.showMap = checked
        }
        Toggle {
            objectName: "fpsToggle"
            text: "FPS"; checked: settings.link.showFps
            onToggled: settings.link.showFps = checked
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Caption { text: "显示模式" }
        Item { Layout.fillWidth: true }
        Toggle {
            objectName: "fullscreenToggle"
            text: "全屏"; checked: settings.link.fullscreen
            onToggled: settings.link.fullscreen = checked
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Caption { text: "视频音量" }
        SettingSlider {
            objectName: "volumeSlider"
            from: 0; to: 100; stepSize: 1
            value: settings.link.volume
            onMoved: settings.link.volume = Math.round(value)
        }
        Caption { text: settings.link.volume + "%"; Layout.preferredWidth: 36 }
    }
    RowLayout {
        Layout.fillWidth: true
        Caption { text: "鼠标灵敏度" }
        SettingSlider {
            objectName: "sensitivitySlider"
            from: 10; to: 300; stepSize: 10
            value: settings.link.mouseSensitivity
            onMoved: settings.link.mouseSensitivity = Math.round(value)
        }
        Caption { text: "×" + (settings.link.mouseSensitivity / 100).toFixed(1); Layout.preferredWidth: 36 }
    }
    Caption {
        Layout.fillWidth: true
        text: "灵敏度用于鼠标控制量；Windows 采用原始相对移动。\n控制权限核验后生效；保存后下次启动继续使用。"
        color: theme.textMuted
        wrapMode: Text.WordWrap
        font.pixelSize: theme.fsMicro
    }
    RowLayout {
        Layout.fillWidth: true
        MapButton {
            text: "选择地图原图…"
            onClicked: settings.chooseMap()
        }
        MapButton {
            text: "示意图"
            onClicked: settings.link.mapImage = ""
        }
    }
    Caption {
        Layout.fillWidth: true
        text: (settings.link.mapImage.length > 0 ? "本地参考底图 · 非 RM2027 正式地图" : "自主示意底图 · 非正式场地")
            + "\n坐标为未标定的本机模拟数据；底图读取失败时回退示意图。"
        color: theme.textMuted
        font.pixelSize: theme.fsMicro
        wrapMode: Text.WordWrap
    }
}
