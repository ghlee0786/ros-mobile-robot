import QtQuick
import RobotDashboard

// 창 아래 띠: 왼쪽 rosbridge 지연 · 마지막 이벤트 토스트, 오른쪽 조작 키캡
Rectangle {
    id: bar
    required property Robot robot
    readonly property string lastEvent: robot.events.length > 0 ? robot.events[robot.events.length - 1] : ""

    implicitHeight: 26
    color: Theme.base
    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.sp4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.sp4
        Text {
            text: bar.robot.bridge.connected ? "rosbridge " + bar.robot.bridge.latencyMs + " ms" : "rosbridge 끊김"
            color: bar.robot.bridge.connected ? Theme.text2 : Theme.dangerText
            font.pixelSize: Theme.fsCaption
            font.features: Theme.numFeatures
        }
        Text {
            id: toast
            text: bar.lastEvent
            color: Theme.text2
            font.pixelSize: Theme.fsCaption
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: Theme.durSlow } }
        }
    }
    // 새 이벤트는 6 s 동안 보였다가 흐려진다
    onLastEventChanged: if (lastEvent !== "") { toast.opacity = 1; fade.restart() }
    Timer { id: fade; interval: 6000; onTriggered: toast.opacity = 0.35 }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.sp4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.sp1
        component Hint: Text { color: Theme.text3; font.pixelSize: Theme.fsCaption; anchors.verticalCenter: parent.verticalCenter }
        Kbd { text: "W" } Kbd { text: "A" } Kbd { text: "S" } Kbd { text: "D" }
        Hint { text: " 조종  ·  " }
        Kbd { text: "Space" }
        Hint { text: " 정지  ·  " }
        Kbd { text: "Ctrl" } Kbd { text: "K" }
        Hint { text: " 명령" }
    }
}
