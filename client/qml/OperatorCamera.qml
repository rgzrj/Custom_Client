pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtMultimedia

Item {
    id: panel
    objectName: "operatorCameraPanel"
    height: camera.active ? width * 0.5625 : expanded ? width * 0.5625 + 34 : 28
    clip: true
    property bool expanded: false
    property int selectedIndex: -1
    property string statusText: devices.videoInputs.length ? "选择选手摄像头" : "未检测到摄像头"
    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    Theme { id: theme }

    component PanelButton: Button {
        id: panelButton
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
        contentItem: Text {
            text: panelButton.text
            color: panelButton.enabled ? theme.textPrimary : theme.textMuted
            font: panelButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: theme.radiusSm
            color: panelButton.down ? "#D9203C4C"
                                    : panelButton.hovered ? "#C0183341" : "#B20D1B27"
            border.width: 1
            border.color: panelButton.hovered || panelButton.visualFocus ? theme.cyan : "#577F9EAD"
        }
    }

    function stopCamera() {
        camera.stop();
        preview.clearOutput();
        selectedIndex = -1;
        expanded = false;
        statusText = devices.videoInputs.length ? "摄像头已关闭" : "未检测到摄像头";
    }

    function selectCamera(index: int) {
        stopCamera();
        if (index < 0 || index >= devices.videoInputs.length)
            return;
        camera.cameraDevice = devices.videoInputs[index];
        selectedIndex = index;
        expanded = true;
        statusText = "摄像头连接中";
        camera.start();
    }

    MediaDevices {
        id: devices
        onVideoInputsChanged: {
            if (panel.selectedIndex >= 0)
                panel.stopCamera();
            else if (videoInputs.length === 0)
                panel.statusText = "未检测到摄像头";
            else
                panel.statusText = "选择选手摄像头";
        }
    }
    Camera {
        id: camera
        objectName: "operatorCamera"
        onErrorOccurred: (error, errorString) => {
            panel.stopCamera();
            panel.statusText = errorString || "摄像头无法启动";
        }
    }
    CaptureSession {
        camera: camera
        videoOutput: preview
    }

    PanelButton {
        id: openButton
        objectName: "operatorCameraOpen"
        visible: !panel.expanded && !camera.active
        anchors.left: parent.left
        anchors.top: parent.top
        width: 126; height: 28
        text: "选手画面  ＋"
        onClicked: panel.expanded = true
    }

    Rectangle {
        id: frame
        visible: panel.expanded || camera.active
        anchors.top: parent.top
        width: parent.width
        height: width * 0.5625
        clip: true
        radius: 10
        color: "#C00B1620"
        border.color: theme.hairline
        border.width: 1
        VideoOutput {
            id: preview
            objectName: "operatorCameraPreview"
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            endOfStreamPolicy: VideoOutput.ClearOutput
            visible: camera.active
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 24
            visible: !camera.active
            text: panel.statusText
            color: theme.textSecondary
            font.family: theme.uiFont
            font.pixelSize: theme.fsBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        // 底部信息条：中性青色指示点表示预览中，不使用红色以免被理解为录制
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 25
            color: "#D4142531"
            Rectangle {
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10; width: 3; height: 13
                radius: 1.5
                color: camera.active ? theme.cyan : theme.textFaint
            }
            Text {
                anchors.centerIn: parent
                text: "操作手 --  /  机器人 --"
                color: "#DCE9EE"
                font.family: theme.uiFont
                font.pixelSize: theme.fsLabel
                font.letterSpacing: 0.5
            }
        }
        PanelButton {
            id: hideButton
            objectName: "operatorCameraHide"
            visible: camera.active
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            width: 48; height: 24
            text: "关闭"
            onClicked: panel.stopCamera()
        }
    }

    ComboBox {
        id: selector
        objectName: "operatorCameraSelector"
        anchors.left: parent.left
        anchors.right: closeButton.left
        anchors.rightMargin: 6
        anchors.top: frame.bottom
        anchors.topMargin: 5
        height: 27
        visible: panel.expanded && !camera.active
        enabled: devices.videoInputs.length > 0
        model: devices.videoInputs
        textRole: "description"
        currentIndex: panel.selectedIndex
        displayText: panel.selectedIndex < 0
            ? "选择摄像头" : devices.videoInputs[panel.selectedIndex].description
        onActivated: (index) => panel.selectCamera(index)
        Accessible.name: "选择选手摄像头"
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
        palette.text: theme.textPrimary
        palette.buttonText: theme.textPrimary
        background: Rectangle {
            radius: theme.radiusSm
            color: "#B20D1B27"
            border.color: theme.hairline
        }
    }
    PanelButton {
        id: closeButton
        objectName: "operatorCameraStop"
        anchors.right: parent.right
        anchors.top: frame.bottom
        anchors.topMargin: 5
        width: 60
        height: 27
        visible: panel.expanded && !camera.active
        text: "收起"
        onClicked: panel.stopCamera()
        Accessible.name: "收起摄像头选择"
    }
}
