import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// 점검 화면 체크 상자(Theme 색, 16px)
CheckBox {
    id: box
    padding: Theme.sp1
    spacing: Theme.sp2
    font.pixelSize: Theme.fsBody
    indicator: Rectangle {
        x: box.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 16; height: 16
        radius: Theme.rXs
        color: box.checked ? Theme.accent : Theme.surface2
        border.color: box.checked ? Theme.accent : Theme.borderStrong
        Text {
            anchors.centerIn: parent
            visible: box.checked
            text: "✓"
            color: Theme.textOnAccent
            font.pixelSize: Theme.fsSmall
        }
    }
    contentItem: Text {
        leftPadding: 16 + (box.text !== "" ? box.spacing : 0)   // 표시 상자 폭을 implicitWidth 에 넣는다
        text: box.text
        font: box.font
        color: Theme.text1
        verticalAlignment: Text.AlignVCenter
    }
}
