import QtQuick
import QtQuick.Layouts

// 오른쪽 센서 패널 · 점검 화면의 카드
Rectangle {
    id: root
    property string title
    property string subtitle
    default property alias content: body.data
    property alias headerExtra: extra.data

    color: "#161A21"
    border.color: "#2B323C"
    radius: 14
    implicitHeight: col.implicitHeight + 28

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 14
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            visible: root.title !== ""
            Text {
                text: root.title
                color: "#E6E9EE"
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                text: root.subtitle
                color: "#9AA3AF"
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            Row { id: extra; spacing: 6 }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
        }
    }
}
