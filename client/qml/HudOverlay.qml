pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Item {
    id: hud
    required property var backend
    property real bottomInset: 52
    // 底部所有浮层（回放条、它的收起把手、底部操作栏）的上沿；HUD 元素不得越过它。
    property real bottomClearance: height - bottomInset
    // 新鲜数据优先；赛前偏好只作为明确标注的预设，未知时保持中性色。
    property string ownTeam: "unknown"
    property bool showCrosshair: true
    property bool showMap: true
    property bool showFps: true
    property url mapImage
    property real topShift: 0
    readonly property color friendly: ownTeam === "red" ? "#FF626C" : ownTeam === "blue" ? "#49D7EF" : "#9AAAB8"
    readonly property color enemy: ownTeam === "red" ? "#49D7EF" : ownTeam === "blue" ? "#FF626C" : "#9AAAB8"
    readonly property real unit: Math.min(width / 1600, height / 900)
    readonly property real side: width * 0.19
    readonly property real teamWidth: Math.min((102 * photos.count + 12 * (photos.count - 1)) * unit,
        (width - 48 - 164) / 2) // 小窗口也给中央收起把手留出间距。
    readonly property real topOffset: topShift + (backend.offlineDrill || backend.historicalLocal ? 26 : 0)
    readonly property bool fresh: backend.refereeConnected && backend.refereeFresh
    readonly property real teamBottom: Math.max(enemyRoster.y + enemyRoster.height,
        friendlyRoster.y + friendlyRoster.height)
    readonly property real rosterBottom: teamBottom + 30
    readonly property var hudData: fresh ? backend.hudSnapshot : ({})
    // 本车身份：裁判数据（selfIndex）优先；数据未到时用开局配置里挑选的机型；
    // 两者都没有则为 -1（不排除任何一格、左下角显示待确认）。
    readonly property int selfIndex: fresh && hudData.selfIndex !== undefined
                                     ? Number(hudData.selfIndex)
                                     : backend.historicalLocal
                                         ? -1 : photos.indexOf(backend.ownRobotPreference)
    // 选手画面面板是否展开：界面占用闸门需要知道它正在占屏。
    readonly property bool cameraExpanded: camera.expanded

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
        property real photoOpacity: 0.92
        readonly property string photoSource: photos.sourceAt(roleIndex)
        readonly property bool photoReady: photoImage.status === Image.Ready
        Image {
            id: photoImage
            anchors.fill: parent
            source: visual.photoSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            visible: visual.photoReady
            opacity: visual.photoOpacity
        }
        Robot {
            anchors.fill: parent
            visible: !visual.photoReady
            accent: visual.accent
        }
    }
    component Team: Row {
        id: team
        width: hud.teamWidth
        required property color accent
        required property var hp
        // 供窗口测试定位：friendly / enemy。
        required property string idPrefix
        // 我方列表只列队友：把本车那一台去掉（本车状态完全交给左下角自车模块）。
        property bool excludeSelf: false
        spacing: 12 * hud.unit
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
                width: (team.width - team.spacing * (team.rosterIndices.length - 1)) / team.rosterIndices.length
                spacing: 3
                RobotVisual {
                    objectName: team.idPrefix + "SlotPhoto" + slot.modelData
                    width: parent.width
                    height: 62 * hud.unit
                    roleIndex: slot.modelData
                    accent: team.accent
                    photoOpacity: 0.72
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
                    text: hud.fresh && team.hp && team.hp.length === photos.count
                        ? (team.hp[slot.modelData] ?? "--") : "--"
                    color: team.accent
                    font.family: theme.numFont
                    font.pixelSize: theme.fsTitle
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

    // 基地/前哨与车队常驻，跟随顶部状态行的动画上移，不改变车图大小。
    component Structures: Row {
        id: structures
        required property string prefix
        required property string teamName
        required property color accent
        readonly property var base: hud.hudData.structures ? hud.hudData.structures[prefix + "_base"] : undefined
        readonly property var outpost: hud.hudData.structures ? hud.hudData.structures[prefix + "_outpost"] : undefined
        spacing: 8
        layoutDirection: prefix === "friendly" ? Qt.RightToLeft : Qt.LeftToRight
        Rectangle {
            objectName: structures.prefix + "_baseCapsule"
            width: structures.width - outpostStatus.width - structures.spacing
            height: 26
            radius: height / 2
            color: "#D908141D"
            Canvas {
                id: baseFill
                objectName: structures.prefix + "_baseFill"
                anchors.fill: parent
                readonly property bool known: structures.base !== undefined && structures.base.hp !== undefined
                readonly property real ratio: known && structures.base.maxHp > 0
                    ? Math.max(0, Math.min(1, structures.base.hp / structures.base.maxHp)) : 0
                // 左侧保留左端、右侧保留右端：扣血从靠中央的一端退向外侧。
                readonly property bool fillFromRight: structures.prefix === "friendly"
                readonly property color accent: Qt.darker(structures.accent, 1.15)
                onKnownChanged: requestPaint()
                onRatioChanged: requestPaint()
                onAccentChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d"); c.reset();
                    const r = (height - 4) / 2;
                    const filledWidth = (width - 4) * baseFill.ratio;
                    c.save();
                    c.beginPath();
                    c.rect(baseFill.fillFromRight ? width - 2 - filledWidth : 2, 0, filledWidth, height);
                    c.clip();
                    c.beginPath(); c.moveTo(r + 2, 2);
                    c.lineTo(width - r - 2, 2);
                    c.arc(width - r - 2, height/2, r, -Math.PI/2, Math.PI/2);
                    c.lineTo(r + 2, height - 2);
                    c.arc(r + 2, height/2, r, Math.PI/2, 3*Math.PI/2);
                    c.closePath();
                    const body = c.createLinearGradient(0, 2, 0, height - 2);
                    body.addColorStop(0, Qt.darker(structures.accent, 1.1));
                    body.addColorStop(0.5, Qt.darker(structures.accent, 1.3));
                    body.addColorStop(1, Qt.darker(structures.accent, 1.65));
                    c.fillStyle = body;
                    c.fill();
                    // 抛光高光只落在有效填充区的顶部，空血槽与未知状态不发亮。
                    c.clip();
                    const gloss = c.createLinearGradient(0, 2, 0, height * 0.28);
                    gloss.addColorStop(0, "#B3FFFFFF");
                    gloss.addColorStop(0.35, "#70FFFFFF");
                    gloss.addColorStop(1, "#00FFFFFF");
                    c.fillStyle = gloss;
                    c.fillRect(2, 2, width - 4, height * 0.28 - 2);
                    c.restore();
                    c.lineWidth = 1.2;
                    c.strokeStyle = baseFill.known ? baseFill.accent : "#617380";
                    c.stroke();
                }
            }
            Label {
                objectName: structures.prefix + "_baseHp"
                anchors.centerIn: parent
                text: structures.teamName + "基地 " + (structures.base && structures.base.hp !== undefined
                    ? structures.base.hp : "--")
                font.pixelSize: theme.fsLabel
                font.weight: Font.DemiBold
            }
        }
        Rectangle {
            id: outpostStatus
            readonly property string life: !structures.outpost ? "unknown"
                : structures.outpost.life !== undefined ? structures.outpost.life
                : structures.outpost.hp > 0 ? "alive" : "destroyed"
            objectName: structures.prefix + "_outpostCapsule"
            width: 92 * hud.unit
            y: 3; height: 20; radius: height / 2
            color: "#7008141D"; border.color: "#285C7380"
            Label {
                objectName: structures.prefix + "_outpostStatus"
                anchors.centerIn: parent
                text: "前哨 " + (outpostStatus.life === "alive" ? "存活"
                    : outpostStatus.life === "destroyed" ? "摧毁" : "未知")
                color: outpostStatus.life === "alive" ? structures.accent
                    : outpostStatus.life === "destroyed" ? theme.danger : theme.textMuted
                font.pixelSize: theme.fsMicro
            }
        }
    }
    Structures {
        objectName: "enemyStructures"
        x: 24; y: 45 + hud.topOffset; width: enemyRoster.width
        prefix: "enemy"; accent: hud.enemy
        teamName: hud.ownTeam === "blue" ? "敌方 · 红方 " : hud.ownTeam === "red" ? "敌方 · 蓝方 " : "敌方 "
    }
    Structures {
        objectName: "friendlyStructures"
        anchors.right: parent.right; anchors.rightMargin: 24
        y: 45 + hud.topOffset; width: friendlyRoster.width
        prefix: "friendly"; accent: hud.friendly
        teamName: hud.ownTeam === "blue" ? "我方 · 蓝方 " : hud.ownTeam === "red" ? "我方 · 红方 " : "我方 "
    }
    Team {
        id: enemyRoster
        objectName: "enemyTeam"; x: 24
        y: 82 + hud.topOffset; accent: hud.enemy; hp: hud.hudData.enemyHp; idPrefix: "enemy"
    }
    Team {
        id: friendlyRoster
        objectName: "friendlyTeam"; anchors.right: parent.right; anchors.rightMargin: 24
        y: 82 + hud.topOffset; accent: hud.friendly; hp: hud.hudData.friendlyHp; idPrefix: "friendly"
        excludeSelf: true
    }
    Row {
        objectName: "rightTelemetry"
        anchors.right: parent.right; anchors.rightMargin: 24
        y: hud.teamBottom + 8
        spacing: 16
        Label {
            objectName: "coinReadout"
            text: "金币 " + (hud.hudData.friendlyCoins === undefined ? "--" : hud.hudData.friendlyCoins)
            color: theme.amber
            font.family: theme.numFont
            font.pixelSize: theme.fsLabel
        }
        Label {
            objectName: "performanceReadout"
            visible: hud.showFps
            text: "FPS " + hud.backend.presentFps.toFixed(1)
            color: hud.backend.guiThreadStalled ? theme.danger : theme.ok
            font.family: theme.numFont
            font.pixelSize: theme.fsLabel
        }
        Label {
            text: "重启 " + (hud.hudData.restartCount === undefined ? "--" : hud.hudData.restartCount)
            color: theme.textSecondary
            font.pixelSize: theme.fsLabel
        }
    }

    Column {
        objectName: "cameraPanel"
        x: 24; y: Math.max(parent.height * 0.275, hud.rosterBottom + 14); width: hud.side; spacing: 12
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
                        text: hud.backend.historicalLocal ? "历史测试事件" : "项目模拟消息"
                        color: theme.textFaint; font.pixelSize: theme.fsMicro; font.letterSpacing: 1
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
                                objectName: "eventMessageText"
                                text: eventRow.modelData.text
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: theme.textSecondary
                                font.pixelSize: theme.fsLabel
                            }
                        }
                        onCountChanged: Qt.callLater(function () { eventList.positionViewAtEnd() })
                        onModelChanged: Qt.callLater(function () { eventList.positionViewAtEnd() })
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: eventList.count === 0
                        text: "暂无事件"
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
        readonly property real arcRadius: width * 0.205
        readonly property real arcHalfAngle: Math.PI * 0.17
        readonly property real arcLineWidth: 1.8
        readonly property color referenceColor: hud.fresh ? "#9C8CA8B4" : "#658CA8B4"
        onReferenceColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        property real currentPitch: hud.hudData.pitchDeg ?? NaN
        onCurrentPitchChanged: requestPaint()
        property bool crosshairEnabled: hud.showCrosshair
        onCrosshairEnabledChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset();
            const x = width/2, y = height/2, r = 45*hud.unit;
            c.lineCap = "round";
            if (crosshairEnabled) {
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
            }
            // 俯仰指示弧：绿/红双段 + 缺口指示点
            const leftRadius = arcRadius;
            c.lineWidth = arcLineWidth;
            if (hud.fresh && Number.isFinite(currentPitch)) {
                const gapAngle = Math.PI + Math.max(-45, Math.min(45, currentPitch)) * arcHalfAngle / 45;
                c.strokeStyle = "#9C6FE2A6";
                if (gapAngle - .014 > Math.PI - arcHalfAngle) {
                    c.beginPath(); c.arc(x, y, leftRadius, Math.PI - arcHalfAngle, gapAngle - .014); c.stroke();
                }
                c.strokeStyle = "#9CE96B77";
                if (gapAngle + .014 < Math.PI + arcHalfAngle) {
                    c.beginPath(); c.arc(x, y, leftRadius, gapAngle + .014, Math.PI + arcHalfAngle); c.stroke();
                }
                c.fillStyle = "#E6EAF4F7";
                c.beginPath();
                c.arc(x + leftRadius*Math.cos(gapAngle), y + leftRadius*Math.sin(gapAngle), 2.6*hud.unit, 0, Math.PI*2);
                c.fill();
            } else {
                c.strokeStyle = referenceColor;
                c.beginPath(); c.arc(x, y, leftRadius, Math.PI - arcHalfAngle, Math.PI + arcHalfAngle); c.stroke();
            }
            c.strokeStyle = referenceColor;
            c.beginPath(); c.arc(x, y, arcRadius, -arcHalfAngle, 0); c.stroke();
        }
    }
    // —— 偏航角反馈：圆形准星下方的横向刻度条 ——
    // 数据来自项目模拟字段 yaw_deg（-180..180）；官方偏航语义未确认，
    // 无新鲜数据时整条变暗并显示 "--"，不猜值、不保留旧角度。
    Item {
        id: yawBar
        objectName: "yawIndicator"
        anchors.horizontalCenter: parent.horizontalCenter
        // 轨道仍对齐左侧俯仰弧下端；右侧只裁剪掉下半段，不改圆心或半径。
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
    component ActionStatus: Label {
        id: chip
        required property string label
        required property bool active
        // 供窗口测试按既有名称定位并读取颜色
        required property string indicatorName
        // 水平正读，但首字沿同一条斜直线逐行向内退。
        x: -0.4 * (y + height / 2)
        objectName: chip.indicatorName
        text: chip.label
        color: chip.active ? "#FF626C" : "#82939F"
        font.family: theme.numFont
        font.pixelSize: Math.max(14, 18 * hud.unit)
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }
    Column {
        id: actionStates
        objectName: "actionStates"
        x: hud.width * 0.705
        // 字体回退可能加宽文字；横向贴近地图时，让整组位于地图上方。
        readonly property real lowerEdge: hud.showMap
            && x + childrenRect.x + childrenRect.width + 8 * hud.unit > minimap.x
            ? Math.min(hud.bottomClearance, minimap.y) : hud.bottomClearance
        y: Math.min(hud.height * 0.65, lowerEdge - height - 8 * hud.unit)
        spacing: 8 * hud.unit
        ActionStatus { label: "CLIMB"; active: hud.hudData.climbActive === true; indicatorName: "climbIndicator" }
        ActionStatus { label: "FIRE"; active: hud.hudData.fireActive === true; indicatorName: "fireIndicator" }
        ActionStatus { id: speedStatus; label: "SPEED UP"; active: hud.hudData.speedUpActive === true; indicatorName: "speedUpIndicator" }
        Label {
            id: actionHint
            objectName: "actionHint"
            x: speedStatus.x
            text: hud.hudData.fireActive !== undefined ? "模拟动作" : "状态待接入"
            color: theme.textFaint
            font.pixelSize: theme.fsMicro
        }
    }
    Column {
        id: weaponPanel
        objectName: "weaponPanel"
        anchors.right: parent.right; anchors.rightMargin: 28
        // 下移，让开加高后的顶部队伍栏。
        y: Math.max(parent.height*.27, hud.rosterBottom + 14); width: hud.side; spacing: 6
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
                text: "装甲状态" + (hud.fresh
                    ? hud.backend.historicalLocal ? " · 历史只读遥测" : " · 模拟遥测" : " · 无有效数据")
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
    BattlefieldMap {
        id: minimap
        objectName: "minimap"
        anchors.right: parent.right; anchors.rightMargin: 24
        anchors.bottom: parent.bottom; anchors.bottomMargin: hud.bottomInset + 12
        width: Math.min(hud.width*.23, hud.height*.43)
        maximumHeight: Math.max(74, hud.height - hud.bottomInset - 12
            - (weaponPanel.y + weaponPanel.height + 12))
        visible: hud.showMap
        snapshot: hud.backend.mapSnapshot
        friendly: hud.friendly
        enemy: hud.enemy
        imageSource: hud.mapImage
    }
}
