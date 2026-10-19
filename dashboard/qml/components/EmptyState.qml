import QtQuick
import QtQuick.Controls.impl
import RobotDashboard

// 빈 상태: 이유 한 줄 + 동작 하나
Column {
    id: root
    property url icon
    property string title
    property string description
    property string actionText
    signal action()

    spacing: Theme.sp2
    IconImage {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.icon.toString() !== ""
        source: root.icon
        sourceSize: Qt.size(24, 24)
        color: Theme.text3
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.title
        color: Theme.text1
        font.pixelSize: Theme.fsBody
        font.weight: Theme.wMedium
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: text !== ""
        text: root.description
        color: Theme.text2
        font.pixelSize: Theme.fsSmall
    }
    Btn {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.actionText !== ""
        text: root.actionText
        size: "sm"
        onClicked: root.action()
    }
}
