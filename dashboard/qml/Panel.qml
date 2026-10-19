import QtQuick
import QtQuick.Layouts
import RobotDashboard

// 오른쪽 센서 패널 · 점검 화면의 카드
Rectangle {
    id: root
    property string title
    property string subtitle
    default property alias content: body.data
    property alias headerExtra: extra.data

    color: Theme.surface1
    border.color: Theme.borderSubtle
    radius: Theme.rLg
    implicitHeight: col.implicitHeight + 2 * Theme.sp3

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.topMargin: Theme.sp3
        anchors.bottomMargin: Theme.sp3
        anchors.leftMargin: Theme.sp4
        anchors.rightMargin: Theme.sp4
        spacing: Theme.sp2
        RowLayout {
            Layout.fillWidth: true
            visible: root.title !== ""
            spacing: Theme.sp2
            Text {
                text: root.title
                color: Theme.text1
                font.pixelSize: Theme.fsTitle
                font.weight: Theme.wStrong
            }
            Text {
                text: root.subtitle
                color: Theme.text2
                font.pixelSize: Theme.fsSmall
                Layout.fillWidth: true
            }
            Row { id: extra; spacing: Theme.spNudge }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spNudge
        }
    }
}
