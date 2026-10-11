pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: map
    required property var snapshot
    required property color friendly
    required property color enemy
    property url imageSource
    property real maximumHeight: 10000
    readonly property bool imageReady: fieldImage.status === Image.Ready
    readonly property real aspect: imageReady ? fieldImage.sourceSize.width / fieldImage.sourceSize.height : 1.8
    height: Math.min((width - 8) / Math.max(0.5, aspect) + 8, maximumHeight)
    radius: 4
    color: "#700A1924"
    border.color: "#286A8591"
    clip: true
    Theme { id: theme }
    Item {
        id: field
        x: 4; y: 4; width: map.width - 8; height: map.height - 8
        Image {
            id: fieldImage
            objectName: "mapFieldImage"
            anchors.fill: parent
            source: map.imageSource
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            opacity: 0.60
        }
        Canvas {
            anchors.fill: parent
            visible: !map.imageReady
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const c = getContext("2d"); c.reset();
                c.lineWidth = 1;
                c.strokeStyle = "#36566C7A";
                for (let i = 1; i < 8; ++i) {
                    c.beginPath(); c.moveTo(width*i/8, 0); c.lineTo(width*i/8, height); c.stroke();
                }
                for (let i = 1; i < 5; ++i) {
                    c.beginPath(); c.moveTo(0, height*i/5); c.lineTo(width, height*i/5); c.stroke();
                }
                // 原创模拟场地：两侧阵地、中央区域，仅用于坐标布局联调。
                const regions = [
                    [.04,.12,.30,.12,.42,.30,.26,.43,.04,.43],
                    [.96,.88,.70,.88,.58,.70,.74,.57,.96,.57],
                    [.44,.17,.66,.34,.56,.83,.34,.66]];
                for (let i = 0; i < regions.length; ++i) {
                    const p = regions[i]; c.beginPath();
                    c.moveTo(p[0]*width, p[1]*height);
                    for (let j = 2; j < p.length; j += 2) c.lineTo(p[j]*width, p[j+1]*height);
                    c.closePath();
                    c.fillStyle = "#253549"; c.fill();
                    c.strokeStyle = i === 0 ? "#996B4550" : i === 1 ? "#996097AB" : "#807E8E99";
                    c.stroke();
                }
            }
        }
        Repeater {
            model: [{positions: map.snapshot.enemyPositions || [], accent: map.enemy},
                    {positions: map.snapshot.friendlyPositions || [], accent: map.friendly}]
            Item {
                id: team
                required property var modelData
                anchors.fill: parent
                Repeater {
                    model: team.modelData.positions
                    Item {
                        required property var modelData
                        // 与 PreserveAspectFit 的实际绘制区域同源；边缘点留出半径。
                        readonly property real drawWidth: map.imageReady ? fieldImage.paintedWidth : field.width
                        readonly property real drawHeight: map.imageReady ? fieldImage.paintedHeight : field.height
                        x: (field.width - drawWidth)/2 + 6 + (drawWidth - 12)*modelData.x
                        y: (field.height - drawHeight)/2 + 6 + (drawHeight - 12)*modelData.y
                        Rectangle {
                            anchors.centerIn: parent
                            width: 13; height: 13; radius: 6.5
                            color: team.modelData.accent; opacity: 0.25
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 9; height: 9; radius: 4.5
                            color: team.modelData.accent; border.color: "#E4EDF3"
                        }
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            objectName: "mapWaitingLabel"
            visible: !(map.snapshot.friendlyPositions && map.snapshot.friendlyPositions.length)
                && !(map.snapshot.enemyPositions && map.snapshot.enemyPositions.length)
            text: "坐标待接入"
            color: theme.textPrimary
            style: Text.Outline; styleColor: "#DC071019"
            font.family: theme.uiFont
            font.pixelSize: theme.fsMicro
        }
    }
}
