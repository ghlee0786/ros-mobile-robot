import QtQuick
import RobotDashboard

// 상태 칩: 점 + 문구. lastUpdate(ms, Date.now() 기준)를 주면 timeoutMs 가 지나면 회색 '?' 로 바뀐다.
Rectangle {
    id: chip
    property string kind: "neutral"   // ok · warn · danger · neutral · stale
    property alias text: label.text
    property bool mono: false
    property real lastUpdate: 0
    property int timeoutMs: 3000
    property real now: Date.now()
    readonly property string shown: kind === "stale" || (lastUpdate > 0 && now - lastUpdate > timeoutMs) ? "stale" : kind

    radius: Theme.rPill
    implicitHeight: Theme.hXs
    implicitWidth: row.implicitWidth + Theme.sp4
    color: shown === "ok" ? Theme.okSubtle : shown === "warn" ? Theme.warnSubtle
         : shown === "danger" ? Theme.dangerSubtle : Theme.surface2

    Timer { interval: 500; repeat: true; running: chip.lastUpdate > 0 && chip.visible; onTriggered: chip.now = Date.now() }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spNudge
        Rectangle {
            visible: chip.shown !== "stale"
            width: 8; height: 8; radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: chip.shown === "ok" ? Theme.ok : chip.shown === "warn" ? Theme.warn
                 : chip.shown === "danger" ? Theme.danger : Theme.text3
        }
        Text {
            visible: chip.shown === "stale"
            anchors.verticalCenter: parent.verticalCenter
            text: "?"
            color: Theme.text3
            font.pixelSize: Theme.fsSmall
            font.weight: Theme.wStrong
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            color: chip.shown === "ok" ? Theme.okText : chip.shown === "warn" ? Theme.warnText
                 : chip.shown === "danger" ? Theme.dangerText : chip.shown === "stale" ? Theme.text3 : Theme.text2
            font.family: chip.mono ? Theme.monoFamily : Theme.fontFamily
            font.pixelSize: Theme.fsSmall
            font.features: Theme.numFeatures
        }
    }
}
