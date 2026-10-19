import QtQuick
import RobotDashboard

// 300 ms 뒤에 보이는 1px 로딩 바 (28% 폭이 왼쪽에서 오른쪽으로 반복). reduceMotion 이면 멈춰 있다.
Item {
    id: root
    property bool running: false
    property bool shown: false
    implicitHeight: 1
    clip: true
    visible: shown
    onRunningChanged: if (!running) shown = false
    Timer { interval: 300; running: root.running; onTriggered: root.shown = true }

    Rectangle {
        id: bar
        width: root.width * 0.28
        height: root.height
        color: Theme.accent
        NumberAnimation on x {
            running: root.shown && !Theme.reduceMotion
            from: -bar.width; to: root.width
            duration: 1200
            loops: Animation.Infinite
        }
    }
}
