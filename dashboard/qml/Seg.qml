import QtQuick
import QtQuick.Controls.Basic

// 세그먼트 버튼 묶음 (카메라 · 모드)
Rectangle {
    id: root
    property var options: []
    property string current
    signal picked(string value)

    color: "#1A1F27"
    radius: 10
    implicitWidth: row.implicitWidth + 8
    implicitHeight: row.implicitHeight + 8

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        Repeater {
            model: root.options
            Button {
                required property string modelData
                text: modelData
                focusPolicy: Qt.NoFocus
                height: 36
                leftPadding: 14
                rightPadding: 14
                font.pixelSize: 13
                onClicked: root.picked(modelData)
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: root.current === modelData ? "#E6F6FD" : "#AEB6C0"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    color: root.current === modelData ? "#23405A" : "transparent"
                }
            }
        }
    }
}
