pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Explicit selections only. These controls never supply authority or send wire data.
Popup {
    id: popup
    required property var backend
    property int targetRobotId: 0
    property var draft: ({})
    signal submitRequested(string action, var parameters, int robotId)
    Theme { id: theme }
    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(520, parent.width - 32)
    height: Math.min(content.implicitHeight + 32, parent.height - 32)
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {}
    exit: Transition {}
    palette.window: theme.surfaceRaised
    palette.base: theme.surface
    palette.text: theme.textPrimary
    palette.button: theme.surfaceRaised
    palette.buttonText: theme.textPrimary
    palette.highlight: theme.cyan
    font.family: theme.uiFont
    font.pixelSize: theme.fsBody
    background: Rectangle { color: theme.surface; radius: theme.radiusSm; border.color: theme.hairline }

    readonly property var actions: [
        {id: "performance", text: "性能选择", fields: [
            {key: "shooter", text: "射击性能", codes: [0,1,2,3,4], labels: ["不更改","冷却优先","爆发优先","英雄近战优先","英雄远程优先"]},
            {key: "chassis", text: "底盘性能", codes: [0,1,2,3,4], labels: ["不更改","血量优先","功率优先","英雄近战优先","英雄远程优先"]},
            {key: "sentry", text: "哨兵控制方式", codes: [-1,0,1], labels: ["不更改","自动控制","半自动控制"]}]},
        {id: "deploy", text: "英雄部署", fields: [
            {key: "mode", text: "部署操作", codes: [0,1], labels: ["退出部署","进入部署"]}]},
        {id: "assembly", text: "工程装配", fields: [
            {key: "operation", text: "装配操作", codes: [0,1,2], labels: ["开始装配","确认装配","取消装配"]},
            {key: "difficulty", text: "装配难度（取消可选0）", codes: [0,1,2,3,4]}]},
        {id: "ammo17", text: "兑换17mm允许发弹量", fields: [
            {key: "quantity", text: "申请发数（10的倍数）", minimum: 10, maximum: 1000, step: 10}]},
        {id: "ammo42", text: "兑换42mm允许发弹量", fields: [
            {key: "quantity", text: "申请发数", minimum: 1, maximum: 100, step: 1}]},
        {id: "freeRespawn", text: "免费复活", fields: []},
        {id: "paidRespawn", text: "付费复活", fields: []},
        {id: "rune", text: "激活能量机关", fields: []},
        {id: "dart", text: "飞镖操作", fields: [
            {key: "target", text: "飞镖目标", codes: [1,2,3,4,5], labels: ["前哨站","基地固定目标","基地随机固定目标","基地随机移动目标","基地末端移动目标"]},
            {key: "open", text: "闸门", codes: [0,1], labels: ["关闭","开启"]},
            {key: "confirm", text: "确认发射", codes: [0,1], labels: ["否","是"]}]},
        {id: "sentry", text: "哨兵控制", fields: [
            {key: "command", text: "哨兵指令", codes: [1,3,4,5,6,7,8,9,10,11,12], labels: ["补血点补弹","远程补弹","远程回血","确认复活","金币复活","进攻姿态","防御姿态","移动姿态","强化进攻姿态","强化防御姿态","强化移动姿态"]}]},
        {id: "air", text: "空中支援", fields: [
            {key: "command", text: "空中支援操作", codes: [0,1,2,3], labels: ["取消支援","免费呼叫","付费呼叫","金币解除反制"]}]}
    ]
    readonly property var selectedAction: operation.currentIndex < 0 ? null : actions[operation.currentIndex]
    readonly property bool complete: {
        if (!selectedAction) return false;
        for (const field of selectedAction.fields)
            if (draft[field.key] === undefined) return false;
        if (selectedAction.id === "performance" && draft.shooter === 0 && draft.chassis === 0 && draft.sentry === -1)
            return false;
        return true;
    }
    function selectField(key, value) {
        const next = Object.assign({}, draft);
        next[key] = value;
        draft = next;
    }
    function submitDraft() {
        if (!complete || targetRobotId <= 0 || targetRobotId !== backend.officialActionRobotId) return;
        const id = selectedAction.id, parameters = Object.assign({}, draft), robotId = targetRobotId;
        close();
        submitRequested(id, parameters, robotId);
    }
    onOpened: {
        operation.currentIndex = -1;
        draft = ({});
        targetRobotId = backend.officialActionRobotId;
    }
    onClosed: { draft = ({}); targetRobotId = 0; operation.currentIndex = -1; }
    Connections {
        target: popup.backend
        function onStateChanged() {
            if (popup.visible && popup.targetRobotId !== popup.backend.officialActionRobotId) popup.close();
        }
    }
    contentItem: ColumnLayout {
        id: content
        spacing: 10
        Label { text: "赛事操作"; font.pixelSize: theme.fsTitle; color: theme.textPrimary }
        Label {
            objectName: "officialActionIdentity"
            text: popup.targetRobotId > 0 ? "报告机器人 ID " + popup.targetRobotId : "等待匹配且新鲜的机器人身份报告"
            color: theme.textSecondary
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        ComboBox {
            id: operation
            objectName: "officialActionChoice"
            Layout.fillWidth: true
            model: popup.actions
            textRole: "text"
            currentIndex: -1
            displayText: currentIndex < 0 ? "选择赛事操作" : currentText
            onCurrentIndexChanged: popup.draft = ({})
        }
        ScrollView {
            id: parameterScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: fields.implicitHeight
            contentWidth: availableWidth
            ColumnLayout {
                id: fields
                width: parameterScroll.availableWidth
                spacing: 8
                Repeater {
                    model: popup.selectedAction ? popup.selectedAction.fields : []
                    delegate: ColumnLayout {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: row.modelData.text; color: theme.textSecondary; wrapMode: Text.WordWrap }
                        ComboBox {
                            objectName: "officialParameter_" + row.modelData.key
                            Layout.fillWidth: true
                            visible: row.modelData.codes !== undefined
                            model: (row.modelData.codes || []).map((code, index) => ({
                                code: code, text: row.modelData.labels ? row.modelData.labels[index] : String(code)
                            }))
                            textRole: "text"
                            valueRole: "code"
                            currentIndex: -1
                            displayText: currentIndex < 0 ? "请选择" : currentText
                            onActivated: popup.selectField(row.modelData.key, currentValue)
                        }
                        SpinBox {
                            objectName: "officialQuantity_" + row.modelData.key
                            Layout.fillWidth: true
                            visible: row.modelData.codes === undefined
                            from: row.modelData.minimum || 0
                            to: row.modelData.maximum || 0
                            stepSize: row.modelData.step || 1
                            value: from
                            editable: true
                            onValueModified: popup.selectField(row.modelData.key, value)
                            Component.onCompleted: {
                                if (row.modelData.codes === undefined) popup.selectField(row.modelData.key, value);
                            }
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: "普通控制权限待核验。参数提交仍经统一指令门；排队或传输确认均不等于执行。"
            color: theme.amber
            font.pixelSize: theme.fsMicro
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            text: popup.backend.lastInputDecision || ""
            visible: text.indexOf("赛事操作") === 0
            color: theme.textSecondary
            font.pixelSize: theme.fsMicro
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: popup.close() }
            Button {
                objectName: "officialActionSubmit"
                text: "提交所选请求"
                enabled: popup.complete && popup.targetRobotId > 0
                onClicked: popup.submitDraft()
            }
        }
    }
}
