import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// 세그먼트 버튼 묶음 (카메라 · 모드)
Rectangle {
    id: root
    property var options: []
    property string current
    signal picked(string value)

    color: Theme.surface2
    radius: Theme.rMd
    implicitWidth: row.implicitWidth + 2 * Theme.sp1
    implicitHeight: row.implicitHeight + 2 * Theme.sp1

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.sp1
        Repeater {
            model: root.options
            Button {
                id: item
                required property string modelData
                readonly property bool selected: root.current === modelData
                text: modelData
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                height: 28
                leftPadding: Theme.sp3
                rightPadding: Theme.sp3
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsBody
                font.weight: selected ? Theme.wMedium : Theme.wRegular
                onClicked: root.picked(modelData)
                contentItem: Text {
                    text: item.text
                    font: item.font
                    color: item.selected ? Theme.accentText : item.hovered ? Theme.text1 : Theme.text2
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.rXs
                    color: item.selected ? Theme.accentSubtle : "transparent"
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.text1
                        opacity: item.down ? Theme.stPressed : item.hovered && !item.selected ? Theme.stHover : 0
                    }
                }
            }
        }
    }
}
