import QtQuick
import RobotDashboard

// 대시보드 묶음(P6)이 헤더에 칩을 더하는 자리. StatusBar 가 만든다.
Row {
    required property Robot robot
    spacing: Theme.sp2

    StatusChip {
        objectName: "armStatusChip"
        kind: parent.robot.teleopArmed ? "ok" : "neutral"
        text: parent.robot.teleopArmed ? "조종 켜짐" : "조종 잠김"
    }
}
