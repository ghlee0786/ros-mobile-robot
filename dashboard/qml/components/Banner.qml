import QtQuick
import QtQuick.Controls.impl
import RobotDashboard

// 경고 배너: 아이콘 + 문구 + 경과 시간. active 가 켜진 순간부터 'n s 째' 를 센다.
Rectangle {
    id: root
    property bool active: false
    property alias text: label.text
    property url icon: "qrc:/icons/wifi-off.svg"
    property string kind: "danger"   // danger · warn
    property real since: 0
    property real now: 0

    visible: active
    onActiveChanged: if (active) { since = Date.now(); now = since }
    implicitHeight: Theme.hSm
    implicitWidth: row.implicitWidth + Theme.sp6
    radius: Theme.rPill
    color: kind === "warn" ? Theme.warnSubtle : Theme.dangerSubtle
    border.color: kind === "warn" ? Theme.warn : Theme.dangerBorder

    Timer { interval: 1000; repeat: true; running: root.active; onTriggered: root.now = Date.now() }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.sp2
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            source: root.icon
            sourceSize: Qt.size(16, 16)
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            color: root.kind === "warn" ? Theme.warnText : Theme.dangerText
            font.pixelSize: Theme.fsBody
            font.weight: Theme.wMedium
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "· " + Math.floor((root.now - root.since) / 1000) + " s 째"
            color: label.color
            font.pixelSize: Theme.fsBody
            font.features: Theme.numFeatures
        }
    }
}
