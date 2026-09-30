pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Item {
    id: hud
    required property var backend
    property real bottomInset: 52
    // 底部所有浮层（回放条、它的收起把手、底部操作栏）的上沿；HUD 元素不得越过它。
    property real bottomClearance: height - bottomInset
    // 只按新鲜数据标示阵营；未知时保持中性色，不假定蓝方身份。
    property string ownTeam: "unknown"
    readonly property color friendly: ownTeam === "red" ? "#FF626C" : ownTeam === "blue" ? "#49D7EF" : "#9AAAB8"
    readonly property color enemy: ownTeam === "red" ? "#49D7EF" : ownTeam === "blue" ? "#FF626C" : "#9AAAB8"
    readonly property real unit: Math.min(width / 1600, height / 900)
    readonly property real side: width * 0.19
    readonly property bool fresh: backend.refereeConnected && backend.refereeFresh
    readonly property var hudData: fresh ? backend.hudSnapshot : ({})
    // 本车身份：裁判数据（selfIndex）优先；数据未到时用开局配置里挑选的机型；
    // 两者都没有则为 -1（不排除任何一格、左下角显示待确认）。
    readonly property int selfIndex: fresh && hudData.selfIndex !== undefined
                                     ? Number(hudData.selfIndex)
                                     : photos.indexOf(backend.ownRobotPreference)
    // 选手画面面板是否展开：界面占用闸门需要知道它正在占屏。
    readonly property bool cameraExpanded: camera.expanded
    readonly property color glass: "#B00A1924"

    Theme { id: theme }
    RobotPhotos { id: photos; objectName: "robotPhotos" }

    // 装甲比例：仅用于分段填充与颜色分级，数值仍来自既有模拟遥测
    readonly property real armorRatio: {
        if (!fresh)
            return 0;
        const parts = String(backend.hpLabel).split("/");
        const maximum = Number(parts[1]);
        if (!(maximum > 0))
            return 0;
        return Math.max(0, Math.min(1, Number(parts[0]) / maximum));
    }
    readonly property color armorFill: armorRatio > 0.6 ? "#7FD6A8"
                                       : armorRatio > 0.3 ? "#E2B45F" : "#F07079"

    component Label: Text {
        color: theme.textPrimary
        font.family: theme.uiFont
        font.pixelSize: theme.fsBody
        style: Text.Outline
        styleColor: "#B0071019"
    }
    // 俯视线稿：车体 + 炮管 + 两侧履带 + 云台 + 天线，缩小后仍可辨认
    component Robot: Canvas {
        id: robot
        property color accent: "#9AAAB8"
        onAccentChanged: requestPaint()
        onPaint: {
            const c = getContext("2d");
            c.reset();
            const w = width, h = height;
            c.strokeStyle = accent;
            c.lineWidth = 1;
            // 两侧履带
            c.fillStyle = "#70172530";
            c.fillRect(w*.06, h*.32, w*.15, h*.54);
            c.strokeRect(w*.06, h*.32, w*.15, h*.54);
            c.fillRect(w*.79, h*.32, w*.15, h*.54);
            c.strokeRect(w*.79, h*.32, w*.15, h*.54);
            // 车体
            c.fillStyle = "#8E1B2B36";
            c.beginPath();
            c.moveTo(w*.27, h*.42);
            c.lineTo(w*.73, h*.42);
            c.lineTo(w*.78, h*.74);
            c.lineTo(w*.22, h*.74);
            c.closePath();
            c.fill(); c.stroke();
            // 炮管
            c.lineWidth = 1.6;
            c.beginPath(); c.moveTo(w*.5, h*.44); c.lineTo(w*.5, h*.08); c.stroke();
            // 云台
            c.lineWidth = 1;
            c.fillStyle = "#B80B1620";
            c.beginPath(); c.arc(w*.5, h*.54, w*.13, 0, Math.PI*2); c.fill(); c.stroke();
            // 天线
            c.beginPath(); c.moveTo(w*.62, h*.44); c.lineTo(w*.8, h*.18); c.stroke();
        }
    }
    // 机型外观：优先用内置照片（依据 pitcure/ 下的文件名对应），照片缺失或加载失败时回退线稿。
    // 不套任何边框：HUD 上只用线稿本身与文字表达信息。
    component RobotVisual: Item {
        id: visual
        required property int roleIndex
        required property color accent
        readonly property string photoSource: photos.sourceAt(roleIndex)
        readonly property bool photoReady: photoImage.status === Image.Ready
        Image {
            id: photoImage
            anchors.fill: parent
            source: visual.photoSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            visible: visual.photoReady
            opacity: 0.92
        }
        Robot {
            anchors.fill: parent
            visible: !visual.photoReady
            accent: visual.accent
        }
    }
    component Team: Row {
        id: team
        required property color accent
        required property var hp
        // 供窗口测试定位：friendly / enemy。
        required property string idPrefix
        // 我方列表只列队友：把本车那一台去掉（本车状态完全交给左下角自车模块）。
        property bool excludeSelf: false
        spacing: 10 * hud.unit
        // 要显示的机型下标：本车未知时无法排除，退回显示全部六台。
        readonly property var rosterIndices: {
            const list = [];
            for (let index = 0; index < photos.count; ++index) {
                if (team.excludeSelf && index === hud.selfIndex)
                    continue;
                list.push(index);
            }
            return list;
        }
        Repeater {
            model: team.rosterIndices
            Column {
                id: slot
                // modelData 就是机型下标（不是格位序号）：本车被去掉后血量仍按机型取，不会错位。
                required property int modelData
                objectName: team.idPrefix + "Slot" + modelData
                width: 110 * hud.unit
                spacing: 3
                RobotVisual {
                    objectName: team.idPrefix + "SlotPhoto" + slot.modelData
                    width: parent.width
                    height: 74 * hud.unit
                    roleIndex: slot.modelData
                    accent: team.accent
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: photos.labelAt(slot.modelData)
                    color: "#CFE0E8"
                    font.pixelSize: theme.fsLabel
                    font.letterSpacing: 0.6
                }
                // 名称下划线：中间实、两端透明，弱化硬边
                Rectangle {
                    width: parent.width
                    height: 1
                    opacity: 0.7
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.rgba(team.accent.r, team.accent.g, team.accent.b, 0) }
                        GradientStop { position: 0.5; color: team.accent }
                        GradientStop { position: 1.0; color: Qt.rgba(team.accent.r, team.accent.g, team.accent.b, 0) }
                    }
                }
                // 本车不在这个列表里（见 excludeSelf）：本车实时状态完全由左下角自车模块承担。
                Label {
                    objectName: team.idPrefix + "SlotHp" + slot.modelData
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: hud.fresh && team.hp && team.hp.length === photos.count ? team.hp[slot.modelData] : "--"
                    color: team.accent
                    font.family: theme.numFont
                    font.pixelSize: theme.fsBody
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // —— 遮罩层：只用渐变，不做实心大框 ——
    Rectangle {
        width: parent.width; height: 170
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#D806111A" }
            GradientStop { position: 0.6; color: "#5C071019" }
            GradientStop { position: 1.0; color: "#00071019" }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 160
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00071019" }
            GradientStop { position: 0.5; color: "#38071019" }
            GradientStop { position: 1.0; color: "#8C071019" }
        }
    }
    Rectangle {
        width: hud.side + 60; height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#38071019" }
            GradientStop { position: 1.0; color: "#00071019" }
        }
    }

    // 左上角口号：让出身位牌后紧跟其后，弱化为水印。
    RowLayout {
        x: 202; y: 53
        spacing: 8
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 2
            Layout.preferredHeight: 12
            radius: 1
            color: theme.cyan
            opacity: 0.75
        }
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: "不计后果才配称作艺术"
            color: theme.textFaint
            font.pixelSize: theme.fsLabel
            font.letterSpacing: 2
        }
    }
    Rectangle {
        id: ownTeamBadge
        objectName: "ownTeamBadge"
        // 与口号同处最上一行，把左侧整条横向空间让给敌方队伍栏（线稿才能放大）。
        x: 22; y: 45; width: 166; height: 32; radius: 8
        color: hud.ownTeam === "red" ? "#A923151D"
             : hud.ownTeam === "blue" ? "#A90D2B38" : "#A91A2730"
        border.color: hud.friendly
        border.width: 1
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 1
            color: "#33FFFFFF"
        }
        Rectangle {
            x: 10; anchors.verticalCenter: parent.verticalCenter
            width: 4; height: 19; radius: 2; color: hud.friendly
        }
        Label {
            objectName: "ownTeamBadgeLabel"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 5
            text: hud.ownTeam === "red" ? "我方 · 红方"
                : hud.ownTeam === "blue" ? "我方 · 蓝方" : "我方 · 阵营待确认"
            color: hud.ownTeam === "unknown" ? "#B9C6CC" : hud.friendly
            font.pixelSize: 15; font.weight: Font.Bold
        }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 22; y: 49
        spacing: 6
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: "剩余重启次数"
            color: theme.textFaint
            font.pixelSize: theme.fsLabel
        }
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: hud.hudData.restartCount === undefined ? "--" : String(hud.hudData.restartCount)
            color: theme.textPrimary
            font.family: theme.numFont
            font.pixelSize: theme.fsBody
            font.weight: Font.DemiBold
        }
    }
    // 两条队伍栏分别贴左右外边、同一行：结构对称，中央让给金币徽标。
    Team {
        objectName: "enemyTeam"; x: 24
        y: 78; accent: hud.enemy; hp: hud.hudData.enemyHp; idPrefix: "enemy"
    }
    Team {
        objectName: "friendlyTeam"; anchors.right: parent.right; anchors.rightMargin: 24
        y: 78; accent: hud.friendly; hp: hud.hudData.friendlyHp; idPrefix: "friendly"
        excludeSelf: true
    }
    RowLayout {
        anchors.right: coin.left; anchors.rightMargin: 30; y: 45
        spacing: 5
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: hud.enemy
        }
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: "敌方"; color: hud.enemy; font.pixelSize: theme.fsLabel
            font.weight: Font.DemiBold; font.letterSpacing: 1
        }
    }
    RowLayout {
        anchors.left: coin.right; anchors.leftMargin: 30; y: 45
        spacing: 5
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: hud.friendly
        }
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: "我方"; color: hud.friendly; font.pixelSize: theme.fsLabel
            font.weight: Font.DemiBold; font.letterSpacing: 1
        }
    }
    Rectangle {
        id: coin
        anchors.horizontalCenter: parent.horizontalCenter; y: 69
        width: 48 * hud.unit; height: width; radius: width/2
        color: "#C0232825"; border.color: "#CDB875"; border.width: 2
        Rectangle {
            anchors.fill: parent; anchors.margins: 3
            radius: width/2
            color: "#00000000"
            border.width: 1
            border.color: "#59E8D596"
        }
        Label { anchors.centerIn: parent; text: "R"; color: "#E8D596"; font.pixelSize: theme.fsValue; font.weight: Font.Bold }
    }
    Row {
        anchors.horizontalCenter: coin.horizontalCenter
        y: coin.y + coin.height + 6
        spacing: 4
        Label {
            text: hud.hudData.enemyCoins === undefined ? "--" : String(hud.hudData.enemyCoins)
            color: hud.enemy; font.family: theme.numFont; font.pixelSize: theme.fsBody; font.weight: Font.DemiBold
        }
        Label { text: "/"; color: theme.textFaint; font.family: theme.numFont; font.pixelSize: theme.fsBody }
        Label {
            text: hud.hudData.friendlyCoins === undefined ? "--" : String(hud.hudData.friendlyCoins)
            color: hud.friendly; font.family: theme.numFont; font.pixelSize: theme.fsBody; font.weight: Font.DemiBold
        }
    }

    Column {
        objectName: "cameraPanel"
        x: 24; y: parent.height * 0.275; width: hud.side; spacing: 12
        OperatorCamera { id: camera; width: parent.width }
        Rectangle {
            id: messageFeed
            objectName: "messageFeed"
            width: parent.width; height: 124 * hud.unit
            radius: 9 * hud.unit
            color: "#300A1924"
            border.width: 1
            border.color: "#256A8591"
            Rectangle {
                anchors.left: parent.left; anchors.top: parent.top
                anchors.bottom: parent.bottom; anchors.topMargin: 12 * hud.unit
                anchors.bottomMargin: 12 * hud.unit
                width: 2; radius: 1; color: "#69A9D6DE"
            }
            Column {
                anchors.fill: parent; anchors.margins: 10 * hud.unit; spacing: 6 * hud.unit
                RowLayout {
                    spacing: 6
                    Label { Layout.alignment: Qt.AlignVCenter; text: "系统"; color: "#E9C779"; font.pixelSize: theme.fsMicro; font.letterSpacing: 1 }
                    Label { Layout.alignment: Qt.AlignVCenter; text: "·"; color: theme.textFaint; font.pixelSize: theme.fsMicro }
                    Label { Layout.alignment: Qt.AlignVCenter; text: "机器人"; color: "#57DAEA"; font.pixelSize: theme.fsMicro; font.letterSpacing: 1 }
                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        text: "项目模拟消息"; color: theme.textFaint; font.pixelSize: theme.fsMicro; font.letterSpacing: 1
                    }
                }
                Rectangle { width: parent.width; height: 1; color: "#3A2A4352" }
                Item {
                    width: parent.width
                    height: parent.height - y
                    ListView {
                        id: eventList
                        objectName: "eventList"
                        anchors.fill: parent
                        clip: true
                        spacing: 5 * hud.unit
                        model: hud.backend.eventMessages
                        delegate: RowLayout {
                            id: eventRow
                            required property var modelData
                            width: eventList.width
                            spacing: 6 * hud.unit
                            // 类别用短色杠 + 着色文字区分，避免小尺寸下文字被裁切
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: 2; implicitHeight: 12; radius: 1
                                color: eventRow.modelData.category === "robot" ? "#57DAEA" : "#E9C779"
                            }
                            Label {
                                Layout.alignment: Qt.AlignTop
                                text: eventRow.modelData.category === "robot" ? "机器人" : "系统"
                                color: eventRow.modelData.category === "robot" ? "#57DAEA" : "#E9C779"
                                font.pixelSize: theme.fsMicro
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: eventRow.modelData.text
                                wrapMode: Text.Wrap
                                color: theme.textSecondary
                                font.pixelSize: theme.fsLabel
                            }
                        }
                        onCountChanged: Qt.callLater(function () { eventList.positionViewAtEnd() })
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: eventList.count === 0
                        text: hud.fresh ? "等待模拟事件…" : "等待模拟数据…"
                        color: theme.textMuted
                        font.pixelSize: theme.fsLabel
                    }
                }
            }
        }
    }
    Canvas {
        id: aim
        objectName: "aimOverlay"
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        property real currentPitch: hud.hudData.pitchDeg ?? NaN
        onCurrentPitchChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset();
            const x = width/2, y = height/2, r = 45*hud.unit;
            c.lineCap = "round";
            // 主环：四段留缺口，降低视觉重量
            c.lineWidth = 1.2;
            c.strokeStyle = "#8CFF626C";
            const slice = 0.34;
            for (let k = 0; k < 4; k++) {
                c.beginPath();
                c.arc(x, y, r, k*Math.PI/2 + slice/2, (k+1)*Math.PI/2 - slice/2);
                c.stroke();
            }
            // 中心断开十字 + 中心点
            c.lineWidth = 1;
            c.strokeStyle = "#B349D7EF";
            c.beginPath();
            c.moveTo(x - 18*hud.unit, y); c.lineTo(x - 6*hud.unit, y);
            c.moveTo(x + 6*hud.unit, y); c.lineTo(x + 18*hud.unit, y);
            c.moveTo(x, y - 18*hud.unit); c.lineTo(x, y - 6*hud.unit);
            c.moveTo(x, y + 6*hud.unit); c.lineTo(x, y + 18*hud.unit);
            c.stroke();
            c.fillStyle = "#E649D7EF";
            c.beginPath(); c.arc(x, y, 1.6*hud.unit, 0, Math.PI*2); c.fill();
            // 四角参考 bracket
            const left = x - width*.12, right = x + width*.12;
            const top = y - height*.11, bottom = y + height*.11;
            const tick = 15*hud.unit;
            c.strokeStyle = "#5882C887"; c.lineWidth = 1;
            c.beginPath();
            c.moveTo(left+tick, top); c.lineTo(left, top); c.lineTo(left, top+tick);
            c.moveTo(right-tick, top); c.lineTo(right, top); c.lineTo(right, top+tick);
            c.moveTo(left+tick, bottom); c.lineTo(left, bottom); c.lineTo(left, bottom-tick);
            c.moveTo(right-tick, bottom); c.lineTo(right, bottom); c.lineTo(right, bottom-tick);
            c.stroke();
            // 俯仰指示弧：绿/红双段 + 缺口指示点
            const leftRadius = width*.205;
            if (hud.fresh && Number.isFinite(currentPitch)) {
                const gapAngle = Math.PI*(1 + Math.max(-45, Math.min(45, currentPitch))*.17/45);
                c.lineWidth = 2;
                c.strokeStyle = "#B36FE2A6";
                if (gapAngle - .014 > Math.PI*.83) {
                    c.beginPath(); c.arc(x, y, leftRadius, Math.PI*.83, gapAngle - .014); c.stroke();
                }
                c.strokeStyle = "#B3E96B77";
                if (gapAngle + .014 < Math.PI*1.17) {
                    c.beginPath(); c.arc(x, y, leftRadius, gapAngle + .014, Math.PI*1.17); c.stroke();
                }
                c.fillStyle = "#E6EAF4F7";
                c.beginPath();
                c.arc(x + leftRadius*Math.cos(gapAngle), y + leftRadius*Math.sin(gapAngle), 2.6*hud.unit, 0, Math.PI*2);
                c.fill();
            } else {
                c.lineWidth = 1.4;
                c.strokeStyle = "#557F969F";
                c.beginPath(); c.arc(x, y, leftRadius, Math.PI*.83, Math.PI*1.17); c.stroke();
            }
            c.lineWidth = 1;
            c.strokeStyle = "#307F969F";
            c.beginPath(); c.arc(x, y, width*.205, -Math.PI*.17, Math.PI*.17); c.stroke();
        }
    }
    // —— 偏航角反馈：圆形准星下方的横向刻度条 ——
    // 数据来自项目模拟字段 yaw_deg（-180..180）；官方偏航语义未确认，
    // 无新鲜数据时整条变暗并显示 "--"，不猜值、不保留旧角度。
    Item {
        id: yawBar
        objectName: "yawIndicator"
        anchors.horizontalCenter: parent.horizontalCenter
        // 俯仰弧与右侧对照弧（半径 = 宽度×0.205）只画了左右各 ±0.17π，
        // 它们两个下端落在同一条水平线上；轨道就压在这条线上。
        readonly property real arcBottomLine: parent.height * 0.5
            + parent.width * 0.205 * Math.sin(Math.PI * 0.17)
        readonly property real trackY: 8 * hud.unit
        // 正常情况下轨道正好落在圆弧下端的连线上；被底部浮层挡住时整体上抬。
        y: Math.min(arcBottomLine - trackY,
                    hud.bottomClearance - height - 4 * hud.unit)
        width: 300 * hud.unit
        height: 32 * hud.unit

        readonly property bool known: hud.fresh && hud.hudData.yawDeg !== undefined
            && Number.isFinite(Number(hud.hudData.yawDeg))
        readonly property real yaw: known
            ? Math.max(-180, Math.min(180, Number(hud.hudData.yawDeg))) : 0

        // 轨道
        Rectangle {
            id: yawTrack
            anchors.left: parent.left
            anchors.right: parent.right
            y: yawBar.trackY
            height: 1
            color: yawBar.known ? "#4A7F969F" : "#2A7F969F"
        }
        // 刻度：每 30°；0° 与 ±90° 加重
        Repeater {
            model: [-180, -150, -120, -90, -60, -30, 0, 30, 60, 90, 120, 150, 180]
            delegate: Rectangle {
                required property int modelData
                x: yawBar.width / 2 * (1 + modelData / 180) - width / 2
                y: yawBar.trackY
                width: 1
                height: (modelData === 0 ? 9 : (modelData % 90 === 0 ? 6 : 3)) * hud.unit
                color: modelData === 0 ? "#809FB9B9" : "#4A7F969F"
            }
        }
        // 当前偏航标记（在轨道上方）
        Rectangle {
            objectName: "yawIndicatorMarker"
            visible: yawBar.known
            x: yawBar.width / 2 * (1 + yawBar.yaw / 180) - width / 2
            y: yawBar.trackY - 7 * hud.unit
            width: 3
            height: 6 * hud.unit
            radius: 1.5
            color: "#CC49D7EF"
        }
        // 读数（在刻度下方，不会被标记压住）
        Label {
            id: yawLabel
            objectName: "yawIndicatorLabel"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: yawBar.trackY + 11 * hud.unit
            text: "YAW " + (yawBar.known ? Number(hud.hudData.yawDeg).toFixed(1) : "--") + "°"
            color: yawBar.known ? "#9FB9B9" : "#5C7482"
            font.family: theme.numFont
            font.pixelSize: theme.fsLabel
        }
    }

    Rectangle {
        id: pitchChip
        x: parent.width*0.5 - parent.width*0.205 - width - 10
        y: parent.height*.5
        width: pitchText.implicitWidth + 14
        height: 20
        radius: 10
        color: "#A6071019"
        border.width: 1
        border.color: "#3A536E7C"
        Label {
            id: pitchText
            anchors.centerIn: parent
            text: "PITCH " + (hud.hudData.pitchDeg !== undefined
                             ? Number(hud.hudData.pitchDeg).toFixed(1) : "--") + "°"
            color: "#9FB9B9"
            font.family: theme.numFont
            font.pixelSize: theme.fsLabel
        }
    }

    // —— 右侧仪表与读数 ——
    component Readout: Item {
        id: readout
        required property string label
        required property string value
        required property color valueColor
        width: hud.side
        height: 18
        Label {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: readout.label
            color: theme.textFaint
            font.pixelSize: theme.fsLabel
        }
        Label {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: readout.value
            color: readout.valueColor
            font.family: theme.numFont
            font.pixelSize: theme.fsBody
            font.weight: Font.DemiBold
        }
    }
    component ActionChip: Rectangle {
        id: chip
        required property string label
        required property bool active
        // 供窗口测试按既有名称定位并读取颜色
        required property string indicatorName
        width: chipText.implicitWidth + 16
        height: 20
        radius: 10
        color: chip.active ? "#33FF626C" : "#33101F29"
        border.width: 1
        border.color: chip.active ? "#8CFF626C" : "#3A536E7C"
        Label {
            id: chipText
            objectName: chip.indicatorName
            anchors.centerIn: parent
            text: chip.label
            // 颜色被窗口测试断言，必须保持原字面量
            color: chip.active ? "#FF626C" : "#82939F"
            font.pixelSize: theme.fsLabel
            font.weight: chip.active ? Font.Bold : Font.Normal
            font.letterSpacing: 0.6
        }
    }
    Column {
        objectName: "weaponPanel"
        anchors.right: parent.right; anchors.rightMargin: 28
        // 下移，让开加高后的顶部队伍栏。
        y: parent.height*.27; width: hud.side; spacing: 6
        Item {
            width: parent.width; height: 124 * hud.unit
            Rectangle {
                anchors.centerIn: parent
                width: 100 * hud.unit; height: width; radius: width/2
                color: "#7208151E"
                border.width: 1
                border.color: "#3A2A4352"
            }
            Canvas {
                anchors.centerIn: parent; width: parent.height; height: width
                property real currentSpeed: hud.hudData.speedMps ?? NaN
                property real currentShots: hud.hudData.allowedShots ?? NaN
                // 量程按本车机型取（英雄 16、其余 24 m/s）；速度满了正好填满整条弧。
                readonly property real speedMax: photos.speedMaxAt(hud.selfIndex)
                onCurrentSpeedChanged: requestPaint()
                onCurrentShotsChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d"); c.reset();
                    const r = width*.43, mid = width/2;
                    // 两条 270° 圆弧、粗细相同：外圈弹速，内圈允许发弹量。
                    const start = Math.PI*.75, sweep = Math.PI*1.5;
                    c.lineCap = "round";
                    c.lineWidth = 5;
                    c.strokeStyle = "#5C78693A";
                    c.beginPath(); c.arc(mid, mid, r, start, start + sweep); c.stroke();
                    c.strokeStyle = "#5C51A2AC";
                    c.beginPath(); c.arc(mid, mid, r*.78, start, start + sweep); c.stroke();
                    // 外圈刻度，帮助快速判读比例
                    c.strokeStyle = "#4D8FA8B4"; c.lineWidth = 1;
                    for (let i = 0; i <= 12; i++) {
                        const a = start + sweep*i/12;
                        c.beginPath();
                        c.moveTo(mid + (r+5)*Math.cos(a), mid + (r+5)*Math.sin(a));
                        c.lineTo(mid + (r+9)*Math.cos(a), mid + (r+9)*Math.sin(a));
                        c.stroke();
                    }
                    if (hud.fresh && Number.isFinite(currentSpeed)) {
                        c.strokeStyle = "#E8555C"; c.lineWidth = 5;
                        c.beginPath(); c.arc(mid, mid, r, start,
                            start + sweep*Math.min(currentSpeed/speedMax, 1)); c.stroke();
                    }
                    if (hud.fresh && Number.isFinite(currentShots)) {
                        c.strokeStyle = "#58D9E9"; c.lineWidth = 5;
                        c.beginPath(); c.arc(mid, mid, r*.78, start,
                            start + sweep*Math.min(currentShots/100, 1)); c.stroke();
                    }
                }
            }
            Column {
                anchors.centerIn: parent; spacing: 1
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: hud.hudData.speedMps !== undefined
                          ? Number(hud.hudData.speedMps).toFixed(1) : "--"
                    font.pixelSize: theme.fsValue; color: "#E8555C"
                    font.family: theme.numFont
                    font.weight: Font.DemiBold
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "m/s"
                    color: theme.textMuted
                    font.pixelSize: theme.fsMicro
                    font.letterSpacing: 1
                }
            }
        }
        Readout {
            label: "射击初速度"
            value: hud.hudData.speedMps !== undefined ? Number(hud.hudData.speedMps).toFixed(1) : "--"
            valueColor: "#EDCF6F"
        }
        Readout {
            label: "弹丸余量"
            value: hud.fresh ? hud.backend.ammoLabel : "--"
            valueColor: theme.textPrimary
        }
        Readout {
            label: "允许发弹量"
            value: hud.hudData.allowedShots === undefined ? "--" : String(hud.hudData.allowedShots)
            valueColor: "#58D9E9"
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6 * hud.unit
            ActionChip { label: "CLIMB"; active: hud.hudData.climbActive === true; indicatorName: "climbIndicator" }
            ActionChip { label: "FIRE"; active: hud.hudData.fireActive === true; indicatorName: "fireIndicator" }
            ActionChip { label: "SPEED UP"; active: hud.hudData.speedUpActive === true; indicatorName: "speedUpIndicator" }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: hud.hudData.fireActive !== undefined ? "项目模拟动作提示" : "动作状态待接入"
            color: theme.textFaint
            font.pixelSize: theme.fsMicro
        }
    }

    // —— 左下角：本车机型、等级、装甲、护盾、功率 ——
    // 本车线稿不套圆形边框（HUD 上尽量少用「框」）；LV 读数写在线稿下方，不与图形叠在一起。
    Row {
        x: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: hud.bottomInset + 18; spacing: 16
        Item {
            width: 92 * hud.unit; height: 66 * hud.unit
            // 本车外观：跟随裁判数据给出的 selfIndex 动态换图（本车身份可能变化）；
            // 数据未给出本车时回退通用线稿。
            RobotVisual {
                objectName: "selfRobotVisual"
                width: parent.width
                height: 66 * hud.unit
                roleIndex: hud.selfIndex
                accent: "#BDCDD7"
            }
            Label {
                objectName: "levelIndicator"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 3 * hud.unit
                text: "LV " + (hud.hudData.level ?? "--")
                color: "#E4EFF4"
                font.family: theme.numFont
                font.pixelSize: theme.fsLabel
                font.weight: Font.DemiBold
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter; spacing: 6; width: hud.width*.22
            // 本车机型：由裁判数据决定，数据未给出时明确标注待确认，不假定型号。
            Label {
                objectName: "selfRobotLabel"
                text: hud.selfIndex >= 0 ? "本车 · " + photos.labelAt(hud.selfIndex)
                                         : "本车 · 机型待确认"
                color: hud.selfIndex >= 0 ? theme.textPrimary : theme.textFaint
                font.pixelSize: theme.fsLabel
                font.weight: hud.selfIndex >= 0 ? Font.DemiBold : Font.Normal
            }
            Label {
                text: "装甲状态" + (hud.fresh ? " · 模拟遥测" : " · 无有效数据")
                color: theme.textSecondary
                font.pixelSize: theme.fsLabel
            }
            Item {
                width: parent.width; height: 26
                Rectangle {
                    anchors.fill: parent
                    radius: 5 * hud.unit
                    color: "#AE14242F"
                    border.width: 1
                    border.color: "#6A8D5262"
                }
                // 分段填充，按比例点亮并分级配色
                Row {
                    anchors.fill: parent; anchors.margins: 3
                    spacing: 2
                    Repeater {
                        model: 10
                        Rectangle {
                            required property int index
                            width: (parent.width - 18) / 10
                            height: parent.height
                            radius: 2
                            color: hud.armorRatio > index/10 ? hud.armorFill : "#3A55606B"
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    text: hud.fresh ? hud.backend.hpLabel : "-- / --"
                    font.pixelSize: 16; font.weight: Font.DemiBold
                }
            }
            Item {
                width: parent.width; height: 4
                Rectangle {
                    anchors.fill: parent; radius: 2
                    color: "#33455A66"
                }
                Rectangle {
                    objectName: "shieldBarFill"
                    height: parent.height
                    radius: 2
                    width: hud.hudData.maxShield > 0
                           ? parent.width * hud.hudData.shield / hud.hudData.maxShield : 0
                    color: "#55BAD7"
                }
            }
            Row {
                spacing: 8
                Label {
                    objectName: "shieldLabel"
                    text: "护盾 " + (hud.hudData.shield !== undefined
                                   ? hud.hudData.shield + "/" + hud.hudData.maxShield : "--")
                    font.pixelSize: theme.fsMicro; color: theme.textMuted
                }
                Label { text: "|"; color: theme.hairline; font.pixelSize: theme.fsMicro }
                Label { text: "功率"; color: theme.textFaint; font.pixelSize: theme.fsMicro }
                Label {
                    text: hud.fresh ? hud.backend.chassisPowerLabel : "--"
                    color: theme.textSecondary
                    font.family: theme.numFont
                    font.pixelSize: theme.fsMicro
                }
            }
        }
    }

    // —— 右下角：战术地图 ——
    Rectangle {
        id: minimap
        objectName: "minimap"
        anchors.right: parent.right; anchors.rightMargin: 24
        anchors.bottom: parent.bottom; anchors.bottomMargin: hud.bottomInset + 12
        width: Math.min(hud.width*.19, hud.height*.21); height: width
        radius: 9 * hud.unit
        clip: true
        color: "#900A1924"; border.color: "#426A8591"
        border.width: 1
        Canvas {
            anchors.fill: parent
            onPaint: {
                const c = getContext("2d"); c.reset();
                // 细网格 + 主网格双层
                c.strokeStyle = "#24506A7A"; c.lineWidth = 1;
                for (let i = 1; i < 8; i++) {
                    c.beginPath(); c.moveTo(width*i/8, 0); c.lineTo(width*i/8, height); c.stroke();
                    c.beginPath(); c.moveTo(0, height*i/8); c.lineTo(width, height*i/8); c.stroke();
                }
                c.strokeStyle = "#385C7A8A";
                for (let i = 2; i < 8; i += 2) {
                    c.beginPath(); c.moveTo(width*i/8, 0); c.lineTo(width*i/8, height); c.stroke();
                    c.beginPath(); c.moveTo(0, height*i/8); c.lineTo(width, height*i/8); c.stroke();
                }
                // 四角括号
                const t = 12*hud.unit, m = 6*hud.unit;
                c.strokeStyle = "#668FA8B4"; c.lineWidth = 1.2;
                c.beginPath();
                c.moveTo(m, m+t); c.lineTo(m, m); c.lineTo(m+t, m);
                c.moveTo(width-m-t, m); c.lineTo(width-m, m); c.lineTo(width-m, m+t);
                c.moveTo(width-m, height-m-t); c.lineTo(width-m, height-m); c.lineTo(width-m-t, height-m);
                c.moveTo(m+t, height-m); c.lineTo(m, height-m); c.lineTo(m, height-m-t);
                c.stroke();
            }
        }
        Repeater {
            model: hud.hudData.enemyPositions || []
            Item {
                required property var modelData
                x: minimap.width * modelData.x
                y: minimap.height * modelData.y
                Rectangle {
                    anchors.centerIn: parent
                    width: 13; height: 13; radius: 6.5
                    color: hud.enemy; opacity: 0.22
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 7; height: 7; radius: 3.5
                    color: hud.enemy; border.color: "#DDE8EC"; border.width: 1
                }
            }
        }
        Repeater {
            model: hud.hudData.friendlyPositions || []
            Item {
                required property var modelData
                x: minimap.width * modelData.x
                y: minimap.height * modelData.y
                Rectangle {
                    anchors.centerIn: parent
                    width: 13; height: 13; radius: 6.5
                    color: hud.friendly; opacity: 0.22
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 7; height: 7; radius: 3.5
                    color: hud.friendly; border.color: "#DDE8EC"; border.width: 1
                }
            }
        }
        Label {
            x: 10; y: 8
            text: hud.fresh ? "战术地图 · 模拟" : "战术地图"
            font.pixelSize: theme.fsMicro; color: "#A6C4CB"
            font.letterSpacing: 0.8
        }
        Label {
            anchors.centerIn: parent
            visible: !hud.hudData.friendlyPositions
            text: "地图 / 坐标待接入"; font.pixelSize: theme.fsLabel; color: theme.textMuted
        }
    }
}
