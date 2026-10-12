pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 启动面板：每次启动都会出现，确认链路与模式后才进入终端。
//
// 设计口径：
// - 只配置本应用自己的端点，绝不修改 Windows 网卡的 IP / 掩码 / DNS（仓库硬约束）。
// - 默认值全部是本机模拟环回；RM2027 正式地址、端口、主题仍待官方协议，面板不预填。
// - 裁判数据主机填进哨兵段 203.0.113.0/24（RFC 5737 文档保留段，内外网均不可路由）
//   即进入「离线演练」：不解析域名、不连接任何外部地址、不需要网线。
//   该入口只落到模拟模式，evaluateCommand() 对模拟/回放一律返回 OfflineMode，
//   结构上不可能下发指令——它不是绕过规则的后门，而是本地演练入口。
// - 标题、链路概览与底部按钮固定，中段可滚动：1180×680 下也不会把「进入」挤出画面。
Item {
    id: panel

    required property var backend
    required property var link
    property bool diagnosticsExpanded: false
    property bool advancedExpanded: false
    property bool shortcutsExpanded: false
    property bool displayExpanded: false

    signal entered()
    signal requestLocalVideo()
    signal requestSimulationVideo()
    signal requestMapImage()

    Theme { id: theme }
    // 机型表与本车选择共用同一份资源（按名字内置的线稿）。
    RobotPhotos { id: robotPhotos }
    readonly property var robotRoleIds: {
        const ids = [];
        for (let index = 0; index < robotPhotos.count; ++index)
            ids.push(robotPhotos.roles[index].role);
        return ids;
    }
    readonly property string ownRobotSource: robotPhotos.sourceAt(robotPhotos.indexOf(link.ownRobot))

    function enterTerminal() {
        if (!panel.link.entryAllowed)
            return;
        if (!panel.backend.applyConnectionProfile())
            return;
        panel.entered();
    }

    // 由控制器停止活动链路并进入离线演练。
    function enterOffline() {
        panel.backend.enterOfflineDrill();
        if (!panel.backend.offlineDrill)
            return;
        panel.entered();
    }

    component PanelButton: Button {
        id: control
        implicitHeight: 32
        leftPadding: 14
        rightPadding: 14
        property color accent: theme.cyan
        property bool primary: false

        contentItem: Text {
            text: control.text
            color: control.enabled ? (control.primary ? theme.ink : theme.textPrimary)
                                   : theme.textFaint
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: theme.uiFont
            font.pixelSize: theme.fsBody
            font.weight: control.primary ? Font.Bold : Font.DemiBold
        }
        background: Rectangle {
            radius: theme.radiusSm
            color: !control.enabled ? "#141F2933"
                   : control.primary ? (control.hovered ? "#63E3F2" : control.accent)
                   : control.down ? "#1B4256" : control.hovered ? "#16303F" : "#152430"
            border.width: 1
            border.color: !control.enabled ? theme.hairline
                          : control.primary ? "#8CE9F5"
                          : control.hovered || control.visualFocus ? control.accent : theme.hairline
        }
    }

    component FieldLabel: Text {
        color: theme.textFaint
        font.family: theme.uiFont
        font.pixelSize: theme.fsLabel
        verticalAlignment: Text.AlignVCenter
    }

    component PanelField: TextField {
        id: field
        implicitHeight: 28
        color: theme.textPrimary
        selectByMouse: true
        font.family: theme.numFont
        font.pixelSize: theme.fsBody
        leftPadding: 8
        rightPadding: 8
        background: Rectangle {
            radius: theme.radiusSm
            color: "#33101F29"
            border.width: 1
            border.color: field.activeFocus ? theme.cyan : theme.hairline
        }
    }

    component SectionTitle: Text {
        color: theme.textSecondary
        font.family: theme.uiFont
        font.pixelSize: theme.fsTitle
        font.weight: Font.DemiBold
        font.letterSpacing: 0.6
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.hairline
    }

    Rectangle {
        anchors.fill: parent
        color: "#D903080D"
    }

    // 吞掉面板外的点击，避免误触到底下的 HUD。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    Rectangle {
        id: card
        objectName: "startPanelCard"
        anchors.centerIn: parent
        width: Math.min(1060, panel.width - 60)
        height: Math.min(760, panel.height - 32)
        radius: theme.radiusLg
        color: "#F20A1620"
        border.width: 1
        border.color: panel.link.offlineSentinel ? "#8C7A5CD6" : theme.hairline

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            // —— 标题 ——
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        id: titleText
                        objectName: "startPanelTitle"
                        text: "RM2027 重装操作终端 · 启动"
                        color: theme.textPrimary
                        font.family: theme.uiFont
                        font.pixelSize: theme.fsValue
                        font.weight: Font.DemiBold

                        // 双击标题 = 隐形的离线演练入口
                        MouseArea {
                            anchors.fill: parent
                            onDoubleClicked: panel.enterOffline()
                        }
                    }
                    Text {
                        text: "链路配置只作用于本应用，不修改 Windows 网卡设置；"
                              + "裁判数据主机只接受 IPv4 字面量，不解析域名"
                        color: theme.textFaint
                        font.family: theme.uiFont
                        font.pixelSize: theme.fsMicro
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: modePill.implicitWidth + 18
                    implicitHeight: 22
                    radius: 11
                    color: panel.link.offlineSentinel ? "#3D7A5CD6" : "#1FF2B84B"
                    border.width: 1
                    border.color: panel.link.offlineSentinel ? "#B37A5CD6" : "#5CF2B84B"
                    Text {
                        id: modePill
                        objectName: "startPanelModePill"
                        anchors.centerIn: parent
                        text: panel.link.entryModeLabel
                        color: panel.link.offlineSentinel ? "#CBB6FF" : theme.amber
                        font.family: theme.uiFont
                        font.pixelSize: theme.fsLabel
                        font.weight: Font.DemiBold
                    }
                }
            }

            // —— 链路状态概览 ——
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        panel.link.localSimulation || panel.link.offlineSentinel
                            ? "有线链路 本机模式不要求"
                            : panel.link.hasWiredLink ? "有线链路 已连接" : "有线链路 未检测到",
                        panel.backend.refereeFresh ? "裁判数据 新鲜" : "裁判数据 离线 / 过期",
                        panel.backend.videoConnected && !panel.backend.videoStale ? "图传 在线" : "图传 离线 / 过期"
                    ]
                    delegate: Rectangle {
                        id: statusChip
                        required property string modelData
                        required property int index
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: statusChipText.implicitWidth + 26
                        implicitHeight: 24
                        radius: 12
                        property bool good: statusChip.index === 0 ? panel.link.hasWiredLink
                                          : statusChip.index === 1 ? panel.backend.refereeFresh
                                          : panel.backend.videoConnected && !panel.backend.videoStale
                        readonly property bool optional: statusChip.index === 0
                            && (panel.link.localSimulation || panel.link.offlineSentinel)
                        color: statusChip.optional ? "#1A7C96A4"
                             : statusChip.good ? "#2646E0A4" : "#26F2B84B"
                        border.width: 1
                        border.color: statusChip.optional ? theme.hairline
                                    : statusChip.good ? "#665CE0A3" : "#66F2B84B"
                        Text {
                            id: statusChipText
                            anchors.centerIn: parent
                            text: statusChip.modelData
                            color: statusChip.optional ? theme.textSecondary
                                 : statusChip.good ? theme.ok : theme.amber
                            font.family: theme.uiFont
                            font.pixelSize: theme.fsLabel
                            font.weight: Font.DemiBold
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: panel.link.localSimulation || panel.link.offlineSentinel
                          ? "仅本机联调 · 真实指令禁用" : panel.link.interfaceSummary
                    color: panel.link.localSimulation || panel.link.offlineSentinel
                           || panel.link.hasWiredLink ? theme.textMuted : theme.amber
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsLabel
                    wrapMode: Text.WordWrap
                }
                PanelButton {
                    objectName: "startPanelDiagnosticsToggle"
                    text: panel.diagnosticsExpanded ? "收起诊断 −" : "诊断详情 +"
                    onClicked: panel.diagnosticsExpanded = !panel.diagnosticsExpanded
                }
            }

            // 视频链路分阶段诊断：目前只有「收包」与「出帧」两段可观测，
            // 组装/解码两段明确标为待官方协议。
            Text {
                objectName: "startPanelDiagnostics"
                Layout.fillWidth: true
                visible: panel.diagnosticsExpanded
                text: panel.backend.videoDiagnosticsLabel
                color: theme.textFaint
                font.family: theme.numFont
                font.pixelSize: theme.fsMicro
                elide: Text.ElideRight
            }

            // 性能读数：出帧率与 GUI 抖动（抖动大意味着解码/IO 可能回到 GUI 线程）。
            Text {
                objectName: "startPanelPerformance"
                Layout.fillWidth: true
                visible: panel.diagnosticsExpanded
                text: panel.backend.performanceLabel
                color: panel.backend.guiThreadStalled ? theme.danger : theme.textFaint
                font.family: theme.numFont
                font.pixelSize: theme.fsMicro
                elide: Text.ElideRight
            }

            Divider {}

            Text {
                objectName: "connectionStateSummary"
                Layout.fillWidth: true
                text: (panel.backend.mqttEnabled ? "本机 MQTT · " + (panel.backend.mqttConnected ? "订阅已建立" : "未连接 / 重试中")
                        : "本机 UDP · " + (panel.backend.refereeFresh ? "正在接收有效数据" : "等待有效数据"))
                    + "   |   " + panel.backend.videoStatusLabel
                    + "   |   有线控制器 " + (panel.backend.controllerConnected ? "在线" : "未接入")
                color: theme.textSecondary
                font.family: theme.uiFont
                font.pixelSize: theme.fsMicro
                wrapMode: Text.WordWrap
            }

            // —— 中段：可滚动，保证小窗口下底部按钮始终可见 ——
            ScrollView {
                id: bodyScroll
                objectName: "startPanelBody"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                RowLayout {
                    // 再留 12px 给纵向滚动条，避免滚动条压在右列文字上。
                    width: Math.max(0, bodyScroll.availableWidth - 12)
                    spacing: 16

                    // —— 左列：链路配置 ——
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 560
                        Layout.alignment: Qt.AlignTop
                        spacing: 8

                        SectionTitle { text: "链路预设" }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ComboBox {
                                id: presetCombo
                                objectName: "startPanelPreset"
                                Layout.fillWidth: true
                                implicitHeight: 30
                                model: panel.link.presets
                                textRole: "label"
                                valueRole: "id"
                                currentIndex: panel.link.presetIndex
                                onActivated: (index) => {
                                    panel.link.selectPreset(index);
                                    presetCombo.currentIndex = panel.link.presetIndex;
                                }
                                font.family: theme.uiFont
                                font.pixelSize: theme.fsBody
                                contentItem: Text {
                                    leftPadding: 10
                                    text: presetCombo.displayText.length > 0
                                          ? presetCombo.displayText : "自定义参数（不对应任何预设）"
                                    color: presetCombo.displayText.length > 0
                                           ? theme.textPrimary : theme.textMuted
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    font: presetCombo.font
                                }
                                background: Rectangle {
                                    radius: theme.radiusSm
                                    color: "#33101F29"
                                    border.width: 1
                                    border.color: presetCombo.activeFocus ? theme.cyan : theme.hairline
                                }
                                popup: Popup {
                                    y: presetCombo.height + 2
                                    width: presetCombo.width
                                    implicitHeight: Math.min(190, presetList.contentHeight + 8)
                                    padding: 4
                                    background: Rectangle {
                                        radius: theme.radiusSm
                                        color: "#F20B1620"
                                        border.width: 1
                                        border.color: theme.hairline
                                    }
                                    contentItem: ListView {
                                        id: presetList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: presetCombo.delegateModel
                                        currentIndex: presetCombo.highlightedIndex
                                    }
                                }
                                delegate: ItemDelegate {
                                    id: presetDelegate
                                    required property int index
                                    required property var modelData
                                    width: presetCombo.width - 8
                                    implicitHeight: 34
                                    contentItem: ColumnLayout {
                                        spacing: 1
                                        Text {
                                            text: presetDelegate.modelData.label
                                            color: theme.textPrimary
                                            font.family: theme.uiFont
                                            font.pixelSize: theme.fsBody
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: presetDelegate.modelData.hint
                                            color: presetDelegate.modelData.offline
                                                   ? "#CBB6FF" : theme.textFaint
                                            font.family: theme.uiFont
                                            font.pixelSize: theme.fsMicro
                                            elide: Text.ElideRight
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: presetDelegate.hovered || presetDelegate.highlighted
                                               ? "#26324A5C" : "transparent"
                                    }
                                }
                            }
                            PanelButton {
                                objectName: "startPanelOfflineShortcut"
                                text: "离线演练"
                                accent: "#9A7AFF"
                                onClicked: panel.enterOffline()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: presetCombo.currentIndex >= 0
                                  && presetCombo.currentIndex < panel.link.presets.length
                                  ? panel.link.presets[presetCombo.currentIndex].hint
                                  : "自定义参数（不对应任何预设）"
                            color: theme.textFaint
                            font.family: theme.uiFont
                            font.pixelSize: theme.fsMicro
                            wrapMode: Text.WordWrap
                        }

                        // 离线哨兵段提示
                        Rectangle {
                            objectName: "startPanelSentinel"
                            Layout.fillWidth: true
                            implicitHeight: sentinelColumn.implicitHeight + 16
                            visible: panel.link.offlineSentinel
                            radius: theme.radiusSm
                            color: "#3D2A1F5C"
                            border.width: 1
                            border.color: "#8C7A5CD6"
                            ColumnLayout {
                                id: sentinelColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                Text {
                                    text: "已选中离线哨兵段 " + panel.link.sentinelCidr
                                    color: "#CBB6FF"
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "RFC 5737 文档保留段，公网与内网都不会路由到它。无需网线、"
                                          + "不解析域名、不连接任何外部地址，直接进入本机演练；"
                                          + "该模式结构上无法下发指令。"
                                    color: theme.textSecondary
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsMicro
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Text {
                            objectName: "startPanelEndpointSummary"
                            Layout.fillWidth: true
                            text: "裁判  " + panel.link.refereeHost + ":" + panel.link.refereePort
                                  + "\n视频  " + panel.link.videoBindAddress + " · 联调 "
                                  + panel.link.simulationVideoPort + " · 探针 " + panel.link.videoProbePort
                                  + "\nMQTT  " + (panel.link.useMqtt
                                      ? panel.link.mqttHost + ":" + panel.link.mqttPort + " · 只订阅"
                                      : "未启用")
                            color: theme.textSecondary
                            font.family: theme.numFont
                            font.pixelSize: theme.fsLabel
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                        PanelButton {
                            objectName: "startPanelAdvancedToggle"
                            Layout.fillWidth: true
                            text: panel.advancedExpanded ? "收起网络参数 −" : "网络参数与接口 +"
                            onClicked: panel.advancedExpanded = !panel.advancedExpanded
                        }
                        SectionTitle {
                            text: "网络接口（只读）"
                            visible: panel.advancedExpanded
                        }

                        ListView {
                            id: interfaceList
                            objectName: "startPanelInterfaces"
                            visible: panel.advancedExpanded
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(96, contentHeight)
                            clip: true
                            spacing: 3
                            model: panel.link.interfaces
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: RowLayout {
                                id: interfaceRow
                                required property var modelData
                                width: interfaceList.width - 10
                                spacing: 8
                                Text {
                                    Layout.preferredWidth: 132
                                    text: interfaceRow.modelData.name
                                    color: interfaceRow.modelData.kind === "wired"
                                           ? theme.textPrimary : theme.textSecondary
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsBody
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    Layout.preferredWidth: 40
                                    implicitHeight: 17
                                    radius: 3
                                    color: interfaceRow.modelData.kind === "wired" ? "#2646E0A4"
                                         : interfaceRow.modelData.kind === "wireless" ? "#26F2B84B"
                                         : "#1A7C96A4"
                                    Text {
                                        anchors.centerIn: parent
                                        text: interfaceRow.modelData.kindLabel
                                        color: interfaceRow.modelData.kind === "wired" ? theme.ok
                                             : interfaceRow.modelData.kind === "wireless"
                                               ? theme.amber : theme.textMuted
                                        font.family: theme.uiFont
                                        font.pixelSize: theme.fsMicro
                                    }
                                }
                                Text {
                                    Layout.preferredWidth: 56
                                    text: interfaceRow.modelData.stateLabel
                                    color: interfaceRow.modelData.up ? theme.textSecondary
                                                                     : theme.textFaint
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsLabel
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: interfaceRow.modelData.addresses
                                    color: theme.textMuted
                                    font.family: theme.numFont
                                    font.pixelSize: theme.fsLabel
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        SectionTitle { text: "端点参数"; visible: panel.advancedExpanded }

                        GridLayout {
                            objectName: "startPanelEndpointFields"
                            visible: panel.advancedExpanded
                            Layout.fillWidth: true
                            columns: 6
                            columnSpacing: 8
                            rowSpacing: 6

                            FieldLabel { text: "裁判数据主机" }
                            PanelField {
                                objectName: "startPanelRefereeHost"
                                Layout.fillWidth: true
                                Layout.columnSpan: 3
                                text: panel.link.refereeHost
                                placeholderText: panel.link.sentinelHost
                                onEditingFinished: if (text !== panel.link.refereeHost)
                                                       panel.link.refereeHost = text
                            }
                            FieldLabel { text: "端口" }
                            PanelField {
                                objectName: "startPanelRefereePort"
                                text: String(panel.link.refereePort)
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: panel.link.refereePort = Number(text)
                            }

                            FieldLabel { text: "图传绑定地址" }
                            PanelField {
                                objectName: "startPanelVideoBind"
                                Layout.fillWidth: true
                                Layout.columnSpan: 3
                                text: panel.link.videoBindAddress
                                onEditingFinished: if (text !== panel.link.videoBindAddress)
                                                       panel.link.videoBindAddress = text
                            }
                            FieldLabel { text: "探针端口" }
                            PanelField {
                                objectName: "startPanelVideoProbePort"
                                text: String(panel.link.videoProbePort)
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: panel.link.videoProbePort = Number(text)
                            }

                            FieldLabel { text: "本机联调解码" }
                            PanelField {
                                objectName: "startPanelSimVideoPort"
                                Layout.preferredWidth: 76
                                text: String(panel.link.simulationVideoPort)
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: panel.link.simulationVideoPort = Number(text)
                            }
                            FieldLabel { text: "MQTT 主机" }
                            PanelField {
                                objectName: "startPanelMqttHost"
                                Layout.fillWidth: true
                                Layout.columnSpan: 2
                                text: panel.link.mqttHost
                                onEditingFinished: if (text !== panel.link.mqttHost)
                                                       panel.link.mqttHost = text
                            }
                            PanelField {
                                objectName: "startPanelMqttPort"
                                text: String(panel.link.mqttPort)
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: panel.link.mqttPort = Number(text)
                            }

                            FieldLabel { text: "模拟 MQTT" }
                            CheckBox {
                                id: mqttCheck
                                objectName: "startPanelUseMqtt"
                                Layout.columnSpan: 5
                                text: "启用只订阅的本机模拟 MQTT（不发布、不连接外部 broker）"
                                checked: panel.link.useMqtt
                                onToggled: panel.link.useMqtt = mqttCheck.checked
                                font.family: theme.uiFont
                                font.pixelSize: theme.fsLabel
                                contentItem: Text {
                                    leftPadding: mqttCheck.indicator.width + 6
                                    text: mqttCheck.text
                                    color: theme.textSecondary
                                    font: mqttCheck.font
                                    verticalAlignment: Text.AlignVCenter
                                }
                                indicator: Rectangle {
                                    implicitWidth: 15
                                    implicitHeight: 15
                                    x: mqttCheck.leftPadding
                                    y: mqttCheck.height / 2 - height / 2
                                    radius: 3
                                    color: mqttCheck.checked ? "#1D4A58" : "#33101F29"
                                    border.width: 1
                                    border.color: mqttCheck.checked ? theme.cyan : theme.hairline
                                    Text {
                                        anchors.centerIn: parent
                                        visible: mqttCheck.checked
                                        text: "✓"
                                        color: theme.cyan
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            Text {
                                Layout.columnSpan: 6
                                Layout.fillWidth: true
                                text: "绑定地址 0.0.0.0 表示监听本机所有 IPv4 网卡，不是裁判系统地址；"
                                      + "当前探针只在本机环回监听，正式 UDP 分包格式待官方协议。"
                                color: theme.textFaint
                                font.family: theme.uiFont
                                font.pixelSize: theme.fsMicro
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle { implicitWidth: 1; Layout.fillHeight: true; color: theme.hairline }

                    // —— 右列：身份、模式与入口 ——
                    ColumnLayout {
                        Layout.preferredWidth: 360
                        Layout.alignment: Qt.AlignTop
                        spacing: 8

                        SectionTitle { text: "身份" }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            FieldLabel { text: "我方阵营" }
                            PanelButton {
                                objectName: "startPanelTeamRed"
                                Layout.fillWidth: true
                                text: "红方"
                                accent: theme.teamRed
                                primary: panel.link.ownTeam === "red"
                                onClicked: panel.link.ownTeam = "red"
                            }
                            PanelButton {
                                objectName: "startPanelTeamBlue"
                                Layout.fillWidth: true
                                text: "蓝方"
                                accent: theme.teamBlue
                                primary: panel.link.ownTeam === "blue"
                                onClicked: panel.link.ownTeam = "blue"
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "赛前设定，仅用于裁判数据到达前的占位显示；数据一到以裁判系统为准。"
                            color: theme.textFaint
                            font.family: theme.uiFont
                            font.pixelSize: theme.fsMicro
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            FieldLabel { text: "操作角色" }
                            PanelButton {
                                Layout.fillWidth: true
                                text: "重装（本项目范围）"
                                enabled: false
                            }
                        }

                        // 开局挑选本车：只作为裁判数据到达前的本车身份；
                        // 数据一到（self_index）就以裁判系统为准，队列表与左下角都会跟着换。
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            FieldLabel { text: "本车机型" }
                            ComboBox {
                                id: ownRobotCombo
                                objectName: "startPanelOwnRobot"
                                Layout.fillWidth: true
                                implicitHeight: 30
                                model: ["未指定（以裁判数据为准）"].concat(panel.robotRoleIds.map(
                                    (role) => robotPhotos.labelAt(robotPhotos.indexOf(role))))
                                currentIndex: {
                                    const index = panel.robotRoleIds.indexOf(panel.link.ownRobot);
                                    return index < 0 ? 0 : index + 1;
                                }
                                onActivated: (index) => {
                                    panel.link.ownRobot = index <= 0 ? ""
                                                                      : panel.robotRoleIds[index - 1];
                                }
                                font.family: theme.uiFont
                                font.pixelSize: theme.fsBody
                                contentItem: Text {
                                    leftPadding: 10
                                    text: ownRobotCombo.displayText
                                    color: panel.link.ownRobot.length > 0
                                           ? theme.textPrimary : theme.textMuted
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    font: ownRobotCombo.font
                                }
                                background: Rectangle {
                                    radius: theme.radiusSm
                                    color: "#33101F29"
                                    border.width: 1
                                    border.color: ownRobotCombo.activeFocus ? theme.cyan : theme.hairline
                                }
                                popup: Popup {
                                    y: ownRobotCombo.height + 2
                                    width: ownRobotCombo.width
                                    implicitHeight: Math.min(220, ownRobotList.contentHeight + 8)
                                    padding: 4
                                    background: Rectangle {
                                        radius: theme.radiusSm
                                        color: "#F20B1620"
                                        border.width: 1
                                        border.color: theme.hairline
                                    }
                                    contentItem: ListView {
                                        id: ownRobotList
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: ownRobotCombo.delegateModel
                                        currentIndex: ownRobotCombo.highlightedIndex
                                    }
                                }
                                delegate: ItemDelegate {
                                    id: ownRobotDelegate
                                    required property int index
                                    required property string modelData
                                    width: ownRobotCombo.width - 8
                                    implicitHeight: 30
                                    contentItem: Text {
                                        leftPadding: 6
                                        text: ownRobotDelegate.modelData
                                        color: ownRobotDelegate.index === 0
                                               ? theme.textMuted : theme.textPrimary
                                        verticalAlignment: Text.AlignVCenter
                                        font.family: theme.uiFont
                                        font.pixelSize: theme.fsBody
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: ownRobotDelegate.hovered || ownRobotDelegate.highlighted
                                               ? "#26324A5C" : "transparent"
                                    }
                                }
                            }
                            Image {
                                objectName: "startPanelOwnRobotPreview"
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: 26
                                Layout.alignment: Qt.AlignVCenter
                                source: panel.ownRobotSource
                                fillMode: Image.PreserveAspectFit
                                visible: panel.ownRobotSource.length > 0
                            }
                        }

                        SectionTitle { text: "当前模式" }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: ["重装操作", "模拟", "回放"]
                                delegate: Rectangle {
                                    id: modeChip
                                    required property string modelData
                                    required property int index
                                    implicitWidth: 74
                                    implicitHeight: 24
                                    radius: 12
                                    readonly property bool active: panel.backend.modeLabel === "回放"
                                        ? modeChip.index === 2
                                        : modeChip.index === 0
                                          ? panel.link.entryModeLabel === "正式链路"
                                          : modeChip.index === 1
                                            && panel.link.entryModeLabel !== "正式链路"
                                    color: modeChip.active ? "#2645D6E8" : "#1A7C96A4"
                                    border.width: 1
                                    border.color: modeChip.active ? theme.cyan : theme.hairline
                                    Text {
                                        anchors.centerIn: parent
                                        text: modeChip.modelData
                                        color: modeChip.active ? theme.cyan : theme.textMuted
                                        font.family: theme.uiFont
                                        font.pixelSize: theme.fsMicro
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "综合指挥 / 手动切换：待正式接口接入"
                            color: theme.textFaint
                            font.family: theme.uiFont
                            font.pixelSize: theme.fsMicro
                            wrapMode: Text.WordWrap
                        }

                        SectionTitle { text: "画面来源" }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            PanelButton {
                                objectName: "startPanelSimVideo"
                                Layout.fillWidth: true
                                text: "本机 UDP 联调"
                                onClicked: panel.requestSimulationVideo()
                            }
                            PanelButton {
                                objectName: "startPanelLocalVideo"
                                Layout.fillWidth: true
                                text: "本地回放…"
                                onClicked: panel.requestLocalVideo()
                            }
                        }

                        // 切源代价提示：同源不重启，异源要满足最短驻留。
                        Text {
                            objectName: "startPanelSourceHint"
                            Layout.fillWidth: true
                            text: panel.backend.sourceSwitchHint.length > 0
                                  ? panel.backend.sourceSwitchHint
                                  : "当前画面来源：" + panel.backend.videoSourceLabel
                            color: theme.textFaint
                            font.family: theme.uiFont
                            font.pixelSize: theme.fsMicro
                            wrapMode: Text.WordWrap
                        }

                        PanelButton {
                            objectName: "displaySettingsToggle"
                            Layout.fillWidth: true
                            text: panel.displayExpanded ? "收起显示与声音 −" : "显示与声音 +"
                            onClicked: panel.displayExpanded = !panel.displayExpanded
                        }
                        DisplaySettings {
                            objectName: "displaySettings"
                            Layout.fillWidth: true
                            visible: panel.displayExpanded
                            link: panel.link
                            onChooseMap: panel.requestMapImage()
                        }
                        PanelButton {
                            Layout.fillWidth: true
                            visible: panel.displayExpanded
                            text: "保存显示设置"
                            onClicked: panel.link.save()
                        }

                        // 「标题即快捷键表」：按键与文案都来自同一张动作表。
                        PanelButton {
                            objectName: "startPanelShortcutsToggle"
                            Layout.fillWidth: true
                            text: panel.shortcutsExpanded ? "收起快捷键 −" : "有线输入快捷键 +"
                            onClicked: panel.shortcutsExpanded = !panel.shortcutsExpanded
                        }

                        ColumnLayout {
                            objectName: "startPanelKeymap"
                            visible: panel.shortcutsExpanded
                            Layout.fillWidth: true
                            spacing: 2
                            Repeater {
                                model: panel.backend.inputActions
                                delegate: RowLayout {
                                    id: keyRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.preferredWidth: 92
                                        text: keyRow.modelData.keys
                                        color: theme.cyan
                                        font.family: theme.numFont
                                        font.pixelSize: theme.fsMicro
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: keyRow.modelData.label
                                              + (keyRow.modelData.isCommand
                                                 ? " · 命令类，需过指令安全门" : " · 界面动作")
                                        color: keyRow.modelData.isCommand ? theme.textSecondary
                                                                         : theme.textMuted
                                        font.family: theme.uiFont
                                        font.pixelSize: theme.fsMicro
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "按键提示与动作表同源；所有动作经同一闸门派发，"
                                      + "界面占用、模拟/离线、阶段、链路与解锁状态都会拦截。"
                                color: theme.textFaint
                                font.family: theme.uiFont
                                font.pixelSize: theme.fsMicro
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: offlineColumn.implicitHeight + 18
                            visible: panel.advancedExpanded
                            radius: theme.radiusSm
                            color: "#3D2A1F5C"
                            border.width: 1
                            border.color: "#8C7A5CD6"

                            ColumnLayout {
                                id: offlineColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 4
                                Text {
                                    text: "离线演练入口"
                                    color: "#CBB6FF"
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "把裁判数据主机置为哨兵段地址即可无网进入。"
                                          + "也可按 Ctrl+Shift+O 直接进入，或用标题双击。"
                                    color: theme.textSecondary
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsMicro
                                    wrapMode: Text.WordWrap
                                }
                                PanelButton {
                                    objectName: "startPanelOfflineButton"
                                    Layout.fillWidth: true
                                    text: "离线演练进入"
                                    accent: "#9A7AFF"
                                    onClicked: panel.enterOffline()
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: panel.link.auditLabel
                                    color: theme.textFaint
                                    font.family: theme.uiFont
                                    font.pixelSize: theme.fsMicro
                                }
                            }
                        }
                    }
                }
            }

            Divider {}

            // 配置自愈提示：文件损坏被隔离、从备份恢复或写入失败时才出现。
            Text {
                objectName: "startPanelStorageNotice"
                Layout.fillWidth: true
                visible: panel.link.storageNotice.length > 0
                text: panel.link.storageNotice
                color: theme.amber
                font.family: theme.uiFont
                font.pixelSize: theme.fsMicro
                wrapMode: Text.WordWrap
            }

            // —— 底部：校验与进入（固定在面板底部）——
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    objectName: "startPanelValidation"
                    Layout.fillWidth: true
                    text: panel.link.validationMessage.length > 0
                          ? "参数校验：" + panel.link.validationMessage
                          : panel.link.entryAllowed
                            ? "参数校验通过 · " + panel.link.entryModeLabel
                            : "参数校验通过，但未检测到有线链路：正式模式不可进入，"
                              + "可先用离线演练或本机模拟"
                    color: panel.link.validationMessage.length > 0 ? theme.danger
                         : panel.link.entryAllowed ? theme.ok : theme.amber
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsLabel
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "Ctrl+Shift+O 离线 · F1 重开面板"
                    color: theme.textFaint
                    font.family: theme.uiFont
                    font.pixelSize: theme.fsMicro
                }

                PanelButton {
                    objectName: "startPanelQuit"
                    text: "退出"
                    onClicked: Qt.quit()
                }
                PanelButton {
                    objectName: "startPanelEnter"
                    text: panel.link.offlineSentinel ? "离线演练进入"
                        : panel.link.localSimulation ? "本机模拟进入" : "进入正式终端"
                    primary: true
                    enabled: panel.link.entryAllowed
                    onClicked: panel.enterTerminal()
                }
            }
        }
    }
}
