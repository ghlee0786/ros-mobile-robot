import QtQuick
import QtQuick.Controls.impl
import RobotDashboard

// 조종 잠금: 자물쇠 칩 + '잠금 해제' 버튼(0.6 s 길게 누르기, 또는 Ctrl+Enter). 켜져 있으면 버튼은 한 번 눌러 잠근다.
Row {
    id: arm
    required property Robot robot
    spacing: Theme.sp2

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.sp1
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            source: arm.robot.teleopArmed ? "qrc:/icons/unlock.svg" : "qrc:/icons/lock.svg"
            sourceSize: Qt.size(16, 16)
            color: arm.robot.teleopArmed ? Theme.okText : Theme.warnText
        }
        StatusChip {
            objectName: "armChip"
            anchors.verticalCenter: parent.verticalCenter
            kind: arm.robot.teleopArmed ? "ok" : "warn"
            text: arm.robot.teleopArmed ? "조종 켜짐" : "조종 잠김"
        }
    }
    Btn {
        id: btn
        objectName: "armButton"
        anchors.verticalCenter: parent.verticalCenter
        size: "sm"
        kind: arm.robot.teleopArmed ? "normal" : "accent"
        icon.source: arm.robot.teleopArmed ? "qrc:/icons/lock.svg" : "qrc:/icons/unlock.svg"
        text: arm.robot.teleopArmed ? "잠그기" : "길게 눌러 잠금 해제"
        property bool wasArmed: false   // 길게 눌러 푼 직후의 clicked 로 다시 잠그지 않게
        onPressed: { wasArmed = arm.robot.teleopArmed; if (!wasArmed) hold.restart() }
        onReleased: hold.stop()
        onCanceled: hold.stop()
        onClicked: if (wasArmed) arm.robot.armTeleop(false)
        Timer { id: hold; interval: 600; onTriggered: arm.robot.armTeleop(true) }
        // 누르는 동안 채워지는 진행 막대
        Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.bottom
            height: 2; radius: 1
            color: Theme.textOnAccent
            width: hold.running ? parent.width : 0
            Behavior on width { enabled: hold.running; NumberAnimation { duration: 600 } }
        }
    }
}
