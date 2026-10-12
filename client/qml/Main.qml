pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    width: 1600
    height: 900
    minimumWidth: 1180
    minimumHeight: 680
    visible: true
    title: root.backend.historicalLocal ? "RM2026 本机合成测试 · 只读 HUD"
        : root.backend.upstreamData ? root.backend.protocolTestSource
            ? "RM2026 本机协议测试" : "RM2026 自定义终端" : "RM2027 重装操作终端"
    color: theme.ink

    Theme { id: theme }

    // qmllint disable unqualified
    readonly property var backend: terminal
    readonly property url startupVideo: initialVideo
    readonly property bool startupSimUdpVideo: initialSimUdpVideo
    // qmllint enable unqualified
    // 启动面板的链路配置对象（ConnectionSettings：网卡只读枚举、端点、哨兵段判定）。
    readonly property var link: root.backend.connection
    visibility: root.link.fullscreen ? Window.FullScreen : Window.Windowed

    property bool localVideoSelected: false
    property bool startPanelVisible: !root.backend.historicalLocal
    property bool simulationVideoSelected: false
    property bool playbackDockExpanded: true
    property bool topStatusExpanded: true
    readonly property string playbackLabel: player.error !== MediaPlayer.NoError ? "播放失败"
        : player.mediaStatus === MediaPlayer.LoadingMedia ? "加载中"
        : player.mediaStatus === MediaPlayer.EndOfMedia ? "已结束"
        : player.playing ? "播放中"
        : player.playbackState === MediaPlayer.PausedState ? "已暂停" : "待播放"
    // 顶部状态圆点：只有告警才用红色，其次是链路新鲜度
    readonly property color stateDot: root.backend.alertCritical ? theme.danger
        : root.backend.refereeFresh ? theme.ok : theme.amber
    readonly property bool hasVideo: root.localVideoSelected || root.simulationVideoSelected || root.backend.upstreamVideoActive
    // 底部浮层的上沿（窗口坐标）：本地回放时包含回放条与其上方 24px 的收起把手。
    readonly property real bottomChromeTop: root.localVideoSelected
        ? Math.min(root.height - 52, playbackDockToggle.y)
        : root.height - 52

    function formatTime(milliseconds: real): string {
        const seconds = Math.max(0, Math.floor(milliseconds / 1000));
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0");
    }

    function togglePlayback() {
        if (player.playing) {
            player.pause();
        } else {
            if (player.mediaStatus === MediaPlayer.EndOfMedia)
                player.position = 0;
            player.play();
        }
    }

    // 界面占用：启动面板、文件对话框、选手面板展开都算占屏 → 交给单一闸门拦截指令。
    function syncUiOccupied() {
        root.backend.setUiOccupied(root.startPanelVisible || videoDialog.visible || mapDialog.visible
                                   || hudLayer.cameraExpanded);
    }
    onStartPanelVisibleChanged: root.syncUiOccupied()

    // 快捷键表来自 core 的动作表：界面标题与按键提示同源，改键位不必改文案。
    function actionKeys(id) {
        const actions = root.backend.inputActions;
        for (let index = 0; index < actions.length; ++index) {
            if (actions[index].id === id)
                return actions[index].keys;
        }
        return "";
    }
    function stopVideoSource() {
        player.stop();
        videoOutput.clearOutput();
        localVideoSelected = false;
        simulationVideoSelected = false;
    }

    function openLocalVideo(file: url) {
        if (file.toString().indexOf("file:") !== 0)
            return;
        player.stop();
        videoOutput.clearOutput();
        root.backend.enterLocalPlayback();
        localVideoSelected = true;
        simulationVideoSelected = false;
        player.source = "";
        player.source = file;
        player.play();
    }
    function openSimulationUdpVideo() {
        player.stop();
        videoOutput.clearOutput();
        root.backend.enterSimulationVideo();
        root.backend.configureSimulationVideoPlayer(player);
        localVideoSelected = false;
        simulationVideoSelected = true;
        player.source = "udp://127.0.0.1:" + root.link.simulationVideoPort + "?localaddr=127.0.0.1";
        player.play();
    }
    // 隐形离线入口：Ctrl+Shift+O（也支持启动面板标题双击、哨兵段主机 + 离线演练按钮）。
    function enterOfflineDrill() {
        root.link.enterOfflineSentinel();
        if (root.backend.applyConnectionProfile())
            root.startPanelVisible = false;
    }
    function reopenStartPanel() {
        root.startPanelVisible = true;
    }
    Component.onCompleted: {
        root.backend.observeVideoSink(videoOutput.videoSink);
        root.backend.observeInputWindow(root);
        root.syncUiOccupied();
        if (startupSimUdpVideo)
            openSimulationUdpVideo();
        else if (startupVideo.toString().length > 0)
            openLocalVideo(startupVideo);
    }
    FileDialog {
        id: videoDialog
        objectName: "localVideoDialog"
        title: "打开本地测试视频"
        nameFilters: ["视频文件 (*.mp4 *.mkv *.mov *.avi *.ts)", "所有文件 (*)"]
        onAccepted: root.openLocalVideo(selectedFile)
        onVisibleChanged: root.syncUiOccupied()
    }
    // 快捷键全部经单一输入闸门派发（命令类动作必须先过 evaluateCommand）。
    FileDialog {
        id: mapDialog
        title: "选择本地场地参考图（非 RM2027 正式地图）"
        nameFilters: ["场地图 (*.png *.jpg *.jpeg)"]
        onAccepted: root.link.mapImage = selectedFile.toString()
        onVisibleChanged: root.syncUiOccupied()
    }
    Shortcut {
        objectName: "startPanelShortcut"
        sequence: root.actionKeys("panel.start")
        onActivated: root.backend.submitInputAction("panel.start")
    }
    Shortcut {
        objectName: "offlineDrillShortcut"
        sequence: root.actionKeys("link.offlineDrill")
        onActivated: root.backend.submitInputAction("link.offlineDrill")
    }
    Shortcut {
        sequence: root.actionKeys("video.localPlayback")
        onActivated: root.backend.submitInputAction("video.localPlayback")
    }
    Shortcut {
        sequence: root.actionKeys("video.simulationUdp")
        onActivated: root.backend.submitInputAction("video.simulationUdp")
    }
    Shortcut {
        sequence: root.actionKeys("controls.toggleArmed")
        onActivated: root.backend.submitInputAction("controls.toggleArmed")
    }
    Shortcut {
        sequence: root.actionKeys("gate.inspectMotion")
        onActivated: root.backend.submitInputAction("gate.inspectMotion")
    }
    Shortcut {
        sequence: root.actionKeys("mode.toggleCustomClient")
        onActivated: root.backend.submitInputAction("mode.toggleCustomClient")
    }
    Shortcut {
        objectName: "freeRespawnShortcut"
        sequence: root.actionKeys("event.freeRespawn")
        enabled: !root.backend.uiOccupied
        autoRepeat: false
        onActivated: root.backend.submitInputAction("event.freeRespawn")
    }
    Shortcut {
        objectName: "paidRespawnShortcut"
        sequence: root.actionKeys("event.paidRespawn")
        enabled: !root.backend.uiOccupied
        autoRepeat: false
        onActivated: root.backend.submitInputAction("event.paidRespawn")
    }
    Connections {
        target: root.backend
        function onStateChanged() {
            if (root.backend.upstreamVideoActive && player.source.toString().length > 0) {
                player.stop();
                player.source = "";
                root.localVideoSelected = false;
                root.simulationVideoSelected = false;
            }
        }
        function onInputActionRequested(actionId) {
            if (actionId === "panel.start")
                root.reopenStartPanel();
            else if (actionId === "link.offlineDrill")
                root.enterOfflineDrill();
            else if (actionId === "video.localPlayback")
                root.backend.requestVideoSource(1);
            else if (actionId === "video.simulationUdp")
                root.backend.requestVideoSource(2);
            else if (actionId === "controls.toggleArmed")
                root.backend.toggleControlsArmed();
            else if (actionId === "gate.inspectMotion")
                root.backend.inspectMotionGate();
            else if (actionId === "mode.toggleCustomClient")
                root.backend.toggleFallbackMode();
        }
        function onVideoSourceRequested(source) {
            if (source === 0)
                root.stopVideoSource();
            else if (source === 1)
                videoDialog.open();
            else if (source === 2)
                root.openSimulationUdpVideo();
        }
        function onConnectionProfileApplied() {
            if (root.backend.upstreamData && !root.backend.offlineDrill) {
                if (root.localVideoSelected || root.simulationVideoSelected) {
                    player.stop();
                    player.source = "";
                    root.localVideoSelected = false;
                    root.simulationVideoSelected = false;
                }
                return;
            }
            // 参数应用会重置后台链路；与仍选中的画面来源重新同步。
            if (root.simulationVideoSelected)
                root.openSimulationUdpVideo();
            else if (root.localVideoSelected)
                root.backend.enterLocalPlayback();
        }
    }
    Connections {
        target: hudLayer
        function onCameraExpandedChanged() {
            root.syncUiOccupied();
        }
    }
    MediaPlayer {
        id: player
        objectName: "localPlayer"
        videoOutput: videoOutput
        audioOutput: AudioOutput {
            objectName: "videoAudioOutput"
            volume: root.link.volume / 100
        }
        loops: loopPlayback.checked ? MediaPlayer.Infinite : 1
        onErrorOccurred: (error, errorString) => {
            videoOutput.clearOutput();
            console.warn("Local video error:", errorString);
        }
    }

    component ActionButton: Button {
        id: control
        implicitHeight: 36
        leftPadding: 16
        rightPadding: 16

        contentItem: Text {
            text: control.text
            color: control.enabled ? theme.textPrimary : theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: theme.uiFont
            font.pixelSize: theme.fsBody
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
        }
        background: Rectangle {
            radius: theme.radiusSm + 1
            color: control.down ? "#1B4256"
                                 : control.hovered ? "#16303F" : theme.surfaceRaised
            border.width: 1
            border.color: !control.enabled ? theme.hairline
                          : control.hovered || control.visualFocus ? theme.cyan : theme.hairline
            // 顶部高光，让按钮有轻微体积感
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 1
                height: 1
                color: control.hovered ? "#2EFFFFFF" : "#14FFFFFF"
            }
        }
    }

    // 顶部状态条使用的「标签 + 数值」读数对
    component StatusPair: Row {
        id: pair
        required property string label
        required property string value
        required property color valueColor
        spacing: 5

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pair.label
            color: theme.textFaint
            font.family: theme.uiFont
            font.pixelSize: theme.fsLabel
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pair.value
            color: pair.valueColor
            font.family: theme.uiFont
            font.pixelSize: theme.fsLabel
            font.weight: Font.DemiBold
        }
    }

    Item {
        id: videoViewport
        objectName: "videoViewport"
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: theme.surface }
        VideoOutput {
            id: videoOutput
            objectName: "localVideoOutput"
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            endOfStreamPolicy: VideoOutput.KeepLastFrame
            visible: root.hasVideo
        }
        HudOverlay {
            id: hudLayer
            objectName: "hudOverlay"
            anchors.fill: parent
            backend: root.backend
            showCrosshair: root.link.showCrosshair
            showMap: root.link.showMap
            showFps: root.link.showFps
            mapImage: root.link.mapImage
            topShift: topStrip.y
            ownTeam: root.backend.refereeFresh
                ? (root.backend.hudSnapshot.ownTeam || "unknown")
                : root.backend.historicalLocal || root.backend.upstreamData
                    ? "unknown" : root.backend.ownTeamPreference
            bottomInset: 52 + (root.localVideoSelected ? playbackDock.height : 0)
            bottomClearance: root.bottomChromeTop
        }

        // —— 离线演练常驻横幅：结构上无法下发指令，且必须一眼可辨 ——
        Rectangle {
            id: offlineDrillBanner
            objectName: "offlineDrillBanner"
            y: topStrip.y + topStrip.height
            width: parent.width
            height: root.backend.offlineDrill || root.backend.historicalLocal ? 26 : 0
            visible: root.backend.offlineDrill || root.backend.historicalLocal
            clip: true
            color: "#CC2A1F5C"
            border.width: 1
            border.color: "#8C7A5CD6"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenter: root.backend.historicalLocal ? undefined : parent.horizontalCenter
                anchors.left: root.backend.historicalLocal ? parent.left : undefined
                anchors.leftMargin: 24
                text: root.backend.linkBannerText
                color: "#CBB6FF"
                font.family: theme.uiFont
                font.pixelSize: theme.fsLabel
                font.weight: Font.DemiBold
                font.letterSpacing: 0.6
            }
        }
        Text {
            objectName: "videoFallbackText"
            anchors.centerIn: parent
            // 让开圆弧下端的偏航刻度条。
            anchors.verticalCenterOffset: 230
            visible: !root.hasVideo || player.error !== MediaPlayer.NoError
                     || (root.backend.upstreamVideoActive && !root.backend.videoConnected)
                     || (root.simulationVideoSelected && root.backend.decodedFrames === 0)
            text: player.error !== MediaPlayer.NoError ? "播放失败：" + player.errorString
                  : (root.simulationVideoSelected || root.backend.upstreamVideoActive) ? root.backend.videoStatusLabel
                  : "尚未选择画面\n从启动面板选择本机图传或本地回放"
            width: parent.width * 0.4
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: root.backend.videoStale ? theme.danger : theme.textMuted
            font.family: theme.uiFont
            font.pixelSize: theme.fsTitle
            font.letterSpacing: 1.5
            lineHeight: 1.4
        }

        // —— 顶部告警与链路状态条 ——
        Rectangle {
            id: topStrip
            objectName: "topStatusStrip"
            y: root.topStatusExpanded ? 0 : -height
            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            width: parent.width
            height: 34
            color: root.backend.alertCritical ? "#D92A121B" : "#CE071019"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root.backend.alertCritical ? "#66FF5F68" : "#3A223A48"
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 10
                    implicitHeight: 10
                    Rectangle {
                        anchors.centerIn: parent
                        width: 10; height: 10; radius: 5
                        color: root.stateDot
                        opacity: 0.26
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 6; height: 6; radius: 3
                        color: root.stateDot
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.backend.alertText
                    elide: Text.ElideRight
                    color: root.backend.alertCritical ? theme.danger : theme.textPrimary
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsBody
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 14
                    color: theme.hairline
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: modePillText.implicitWidth + 18
                    implicitHeight: 20
                    radius: 10
                    color: "#1FF2B84B"
                    border.width: 1
                    border.color: "#5CF2B84B"
                    Text {
                        id: modePillText
                        anchors.centerIn: parent
                        text: root.backend.modeLabel + " · " + root.backend.phaseLabel
                        color: theme.amber
                        font.family: theme.uiFont
                        font.pixelSize: theme.fsLabel
                        font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 14
                    color: theme.hairline
                }
                StatusPair {
                    Layout.alignment: Qt.AlignVCenter
                    label: "裁判"
                    value: root.backend.refereeFresh
                        ? root.backend.historicalLocal || root.backend.protocolTestSource ? "测试新鲜" : "实时" : "离线 / 过期"
                    valueColor: root.backend.refereeFresh ? theme.ok : theme.amber
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 14
                    color: theme.hairline
                }
                StatusPair {
                    Layout.alignment: Qt.AlignVCenter
                    label: "控制器"
                    value: root.backend.controllerConnected ? "在线" : "断开"
                    valueColor: root.backend.controllerConnected ? theme.ok : theme.danger
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 14
                    color: theme.hairline
                }
                StatusPair {
                    Layout.alignment: Qt.AlignVCenter
                    label: "模拟丢包"
                    value: root.backend.refereeFresh ? root.backend.packetLossLabel : "-- %"
                    valueColor: theme.textPrimary
                }
                ActionButton {
                    text: "设置 · F1"
                    implicitHeight: 24
                    onClicked: root.reopenStartPanel()
                }
            }
        }

        ActionButton {
            id: statusToggle
            objectName: "topStatusToggle"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: root.backend.offlineDrill ? root.width * 0.38 : 0
            y: topStrip.y + topStrip.height
            implicitHeight: 18
            implicitWidth: 148
            text: root.topStatusExpanded ? "⌃ 收起状态" : (root.backend.alertCritical ? "● 告警 · 展开 ⌄" : "状态 · 展开 ⌄")
            onClicked: root.topStatusExpanded = !root.topStatusExpanded
            ToolTip.visible: hovered && !root.topStatusExpanded
            ToolTip.text: root.backend.alertText
            background: Rectangle {
                radius: 4
                color: "#B0071019"
                border.color: !root.topStatusExpanded && root.backend.alertCritical
                    ? theme.danger : theme.hairline
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                property real pressY: 0
                onPressed: mouse => {
                    statusToggle.forceActiveFocus();
                    pressY = mapToItem(videoViewport, mouse.x, mouse.y).y;
                }
                onReleased: mouse => {
                    const delta = mapToItem(videoViewport, mouse.x, mouse.y).y - pressY;
                    root.topStatusExpanded = Math.abs(delta) > 12 ? delta > 0 : !root.topStatusExpanded;
                }
            }
        }

        // 画面来源提示，做成小胶囊以在亮画面上保持可读
        Rectangle {
            id: videoStateChip
            visible: root.hasVideo && player.error === MediaPlayer.NoError
                     && !(root.simulationVideoSelected && root.backend.decodedFrames === 0)
            readonly property bool stale: (root.simulationVideoSelected || root.backend.upstreamVideoActive)
                                         && root.backend.videoStale
            anchors.horizontalCenter: parent.horizontalCenter
            // 紧凑队伍栏之下，不与无画面提示重复。
            y: hudLayer.rosterBottom + 12
            width: videoStateText.implicitWidth + 22
            height: 22
            radius: 11
            color: "#B0071019"
            border.width: 1
            border.color: videoStateChip.stale ? theme.danger
                         : root.hasVideo ? "#4A2A4352" : "#33536E7C"
            Text {
                id: videoStateText
                objectName: "videoStateText"
                anchors.centerIn: parent
                text: root.backend.upstreamVideoActive ? root.backend.videoStatusLabel
                      : root.simulationVideoSelected ? root.backend.videoStatusLabel
                            + " · 非官方分包格式"
                            + (videoStateChip.stale && root.backend.decodedFrames > 0 ? " · 旧帧非实时" : "")
                      : root.localVideoSelected ? "本地回放 · " + root.playbackLabel + " · 非实时图传"
                      : ""
                color: videoStateChip.stale ? theme.danger
                     : root.hasVideo ? theme.amber : theme.textMuted
                font.family: theme.uiFont
                font.pixelSize: theme.fsLabel
                font.letterSpacing: 0.4
            }
        }

        // —— 回放条：悬浮圆角面板（高度仍严格 48 / 0）——
        Rectangle {
            id: playbackDock
            objectName: "playbackDock"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 54
            height: root.playbackDockExpanded ? 48 : 0
            opacity: root.playbackDockExpanded ? 1 : 0
            clip: true
            radius: theme.radiusLg
            color: "#E60A1620"
            border.width: 1
            border.color: theme.hairline
            visible: root.localVideoSelected
            Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12
                enabled: root.playbackDockExpanded
                ActionButton {
                    objectName: "playbackButton"
                    text: player.playing ? "暂停" : player.mediaStatus === MediaPlayer.EndOfMedia ? "重播" : "播放"
                    enabled: player.error === MediaPlayer.NoError
                    onClicked: root.togglePlayback()
                }
                Text {
                    text: root.formatTime(player.position)
                    color: theme.textPrimary
                    font.family: theme.numFont
                    font.pixelSize: theme.fsBody
                    font.weight: Font.DemiBold
                }
                Slider {
                    id: timeline
                    objectName: "videoTimeline"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, player.duration)
                    value: player.position
                    enabled: player.seekable && player.error === MediaPlayer.NoError
                    onMoved: player.position = Math.round(value)
                    Accessible.name: "本地视频进度"
                    background: Rectangle {
                        x: timeline.leftPadding
                        y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                        width: timeline.availableWidth
                        height: 4
                        radius: 2
                        color: theme.hairline
                        Rectangle {
                            width: timeline.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: timeline.enabled ? theme.cyan : theme.textFaint
                        }
                    }
                    handle: Rectangle {
                        x: timeline.leftPadding + timeline.visualPosition * (timeline.availableWidth - width)
                        y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: timeline.pressed ? theme.textPrimary : theme.cyan
                        border.width: timeline.visualFocus ? 2 : 1
                        border.color: timeline.pressed ? theme.textPrimary : "#80071019"
                    }
                }
                Text {
                    text: root.formatTime(player.duration)
                    color: theme.textMuted
                    font.family: theme.numFont
                    font.pixelSize: theme.fsBody
                }
                CheckBox {
                    id: loopPlayback
                    objectName: "loopPlayback"
                    text: "循环"
                    spacing: 7
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsBody
                    Accessible.name: "循环播放"
                    contentItem: Text {
                        leftPadding: loopPlayback.indicator.width + loopPlayback.spacing
                        text: loopPlayback.text
                        color: theme.textPrimary
                        font: loopPlayback.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    indicator: Rectangle {
                        implicitWidth: 16
                        implicitHeight: 16
                        x: loopPlayback.leftPadding
                        y: parent.height / 2 - height / 2
                        radius: 4
                        color: loopPlayback.checked ? "#1D4A58" : "#33101F29"
                        border.width: 1
                        border.color: loopPlayback.checked ? theme.cyan
                                      : loopPlayback.hovered ? theme.textMuted : theme.hairline
                        Text {
                            anchors.centerIn: parent
                            visible: loopPlayback.checked
                            text: "✓"
                            color: theme.cyan
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }
                }
            }
        }
        Button {
            id: playbackDockToggle
            objectName: "playbackDockToggle"
            visible: root.localVideoSelected
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: playbackDock.top
            anchors.bottomMargin: 4
            width: 96; height: 24
            text: root.playbackDockExpanded ? "收起播放  ▾" : "展开播放  ▴"
            onClicked: root.playbackDockExpanded = !root.playbackDockExpanded
            contentItem: Text {
                text: playbackDockToggle.text
                color: theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: theme.uiFont
                font.pixelSize: theme.fsLabel
            }
            background: Rectangle {
                radius: 12
                color: playbackDockToggle.hovered ? "#D91A3542" : "#B0071019"
                border.width: 1
                border.color: playbackDockToggle.hovered ? "#8C7F9EAD" : "#547F9EAD"
            }
        }

        // —— 底部状态与操作栏 ——
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 46; color: "#E0071019"
            Rectangle {
                anchors.top: parent.top
                width: parent.width; height: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#00223A48" }
                    GradientStop { position: 0.5; color: "#5A223A48" }
                    GradientStop { position: 1.0; color: "#00223A48" }
                }
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 10
                Item {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 9; implicitHeight: 9
                    Rectangle {
                        anchors.centerIn: parent
                        width: 9; height: 9; radius: 4.5
                        color: root.backend.controlsArmed ? theme.ok : theme.amber
                        opacity: 0.26
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 5; height: 5; radius: 2.5
                        color: root.backend.controlsArmed ? theme.ok : theme.amber
                    }
                }
                Text {
                    text: root.backend.controlsArmed ? "控制已解锁" : "控制已锁定"
                    color: root.backend.controlsArmed ? theme.ok : theme.amber
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsBody
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 13
                    color: theme.hairline
                }
                Text {
                    text: root.backend.modeLabel
                    color: theme.textSecondary
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsBody
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 13
                    color: theme.hairline
                }
                Text {
                    text: "模拟延迟"
                    color: theme.textFaint
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsLabel
                }
                Text {
                    text: root.backend.refereeFresh ? root.backend.latencyLabel : "-- ms"
                    color: root.backend.refereeFresh ? theme.textPrimary : theme.textMuted
                    font.family: theme.numFont
                    font.pixelSize: theme.fsBody
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 1; implicitHeight: 13
                    color: theme.hairline
                }
                // 数据新鲜度分级（显示用；放行判断仍是二值的 referee.fresh）。
                StatusPair {
                    objectName: "freshnessReadout"
                    Layout.alignment: Qt.AlignVCenter
                    label: "新鲜度"
                    value: root.backend.refereeFreshnessLabel
                    valueColor: root.backend.refereeFreshnessScale >= 1.0 ? theme.ok
                              : root.backend.refereeFreshnessScale > 0 ? theme.amber
                              : theme.textMuted
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "打开本地视频"; onClicked: root.backend.requestVideoSource(1) }
                ActionButton {
                    text: root.backend.modeLabel === "自定义客户端" ? "返回重装终端" : "切换自定义客户端"
                    onClicked: root.backend.toggleFallbackMode()
                }
                ActionButton {
                    text: root.backend.controlsArmed ? "锁定控制" : "解锁控制"
                    onClicked: root.backend.toggleControlsArmed()
                }
                ActionButton { text: "检查指令门"; onClicked: root.backend.inspectMotionGate() }
            }
        }
    }

    // —— 启动面板：每次启动都会出现（用户确认的启动行为）——
    StartPanel {
        objectName: "startPanel"
        anchors.fill: parent
        z: 100
        visible: root.startPanelVisible
        backend: root.backend
        link: root.link
        onEntered: root.startPanelVisible = false
        onRequestLocalVideo: root.backend.requestVideoSource(1)
        onRequestSimulationVideo: root.backend.requestVideoSource(2)
        onRequestMapImage: mapDialog.open()
    }
}
