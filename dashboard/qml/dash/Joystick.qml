import QtQuick
import RobotDashboard

// 가상 조이스틱: 원형 패드를 끌면 반지름 비율을 robot.drive(linear, angular)(-1..1)로 보낸다. 데드존 10 %.
// 값이 바뀔 때만 drive 를 부른다 — 누르는 동안 100 ms 마다 다시 보내는 일은 Robot 의 10 Hz 주기가 맡는다
// (여기서도 보내면 cmd_vel 이 20 Hz 가 된다). 놓으면 0 을 한 번 보낸다(rqt_robot_steering 규칙).
Item {
    id: joy
    required property Robot robot
    property real size: 88
    readonly property bool live: robot.mode === "키보드 조종"
    property real lin: 0
    property real ang: 0
    signal blocked()   // 잠긴 채로 눌렀다

    implicitWidth: size + label.implicitWidth + Theme.sp2
    implicitHeight: size
    enabled: live
    opacity: live ? (robot.teleopArmed ? 1 : 0.6) : Theme.opDisabled

    function send(l, a) {
        l = Math.round(l * 100) / 100
        a = Math.round(a * 100) / 100
        if (l === lin && a === ang)
            return
        lin = l
        ang = a
        robot.drive(l, a)
    }

    Rectangle {
        id: pad
        width: joy.size; height: joy.size; radius: width / 2
        color: Theme.surface2
        border.color: area.pressed ? Theme.accent : Theme.borderDefault
        Rectangle { anchors.centerIn: parent; width: parent.width * 0.2; height: width; radius: width / 2; color: "transparent"; border.color: Theme.borderSubtle }
        Rectangle { anchors.centerIn: parent; width: 1; height: parent.height - 16; color: Theme.borderSubtle }
        Rectangle { anchors.centerIn: parent; height: 1; width: parent.width - 16; color: Theme.borderSubtle }
        Rectangle {
            id: knob
            readonly property real r: (pad.width - width) / 2
            width: joy.size * 0.34; height: width; radius: width / 2
            x: r - joy.ang * r
            y: r - joy.lin * r
            color: area.pressed ? Theme.accent : Theme.surface3
            border.color: Theme.borderStrong
        }
        MouseArea {
            id: area
            anchors.fill: parent
            preventStealing: true
            function update(mx, my) {
                const r = knob.r
                let nx = (mx - pad.width / 2) / r, ny = (my - pad.height / 2) / r
                const d = Math.hypot(nx, ny)
                if (d > 1) { nx /= d; ny /= d }
                if (d < 0.1) { nx = 0; ny = 0 }   // 데드존 10 %
                joy.send(-ny, -nx)
            }
            onPressed: (m) => { if (!joy.robot.teleopArmed) joy.blocked(); update(m.x, m.y) }
            onPositionChanged: (m) => update(m.x, m.y)
            onReleased: joy.send(0, 0)
            onCanceled: joy.send(0, 0)
        }
    }
    Column {
        id: label
        anchors.left: pad.right
        anchors.leftMargin: Theme.sp2
        anchors.verticalCenter: pad.verticalCenter
        spacing: 2
        Text { text: "조이스틱"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        Text {
            text: "최대 " + joy.robot.maxSpeed.toFixed(2) + " m/s"
            color: Theme.text1; font.pixelSize: Theme.fsSmall; font.features: Theme.numFeatures
        }
        Text {
            text: "전진 " + Math.round(joy.lin * 100) + "% · 회전 " + Math.round(joy.ang * 100) + "%"
            color: Theme.text3; font.pixelSize: Theme.fsCaption; font.features: Theme.numFeatures
        }
    }
}
