import QtQuick
import RobotDashboard

// 키캡
Rectangle {
    property alias text: label.text
    radius: Theme.rXs
    color: Theme.surface3
    border.color: Theme.borderDefault
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + Theme.sp2)
    implicitHeight: label.implicitHeight + 2
    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.text2
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fsCaption
    }
}
