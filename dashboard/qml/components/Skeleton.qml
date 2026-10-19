import QtQuick
import RobotDashboard

// 내용이 오기 전 자리 표시 (surface2 펄스). reduceMotion 이면 멈춰 있다.
Rectangle {
    color: Theme.surface2
    radius: Theme.rSm
    SequentialAnimation on opacity {
        running: visible && !Theme.reduceMotion
        loops: Animation.Infinite
        NumberAnimation { from: 1; to: 0.5; duration: 700; easing.type: Easing.InOutQuad }
        NumberAnimation { from: 0.5; to: 1; duration: 700; easing.type: Easing.InOutQuad }
    }
}
