import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// 점검 화면 입력칸(Theme 색 · 반경)
TextField {
    property bool mono: false
    implicitHeight: Theme.hSm
    color: Theme.text1
    placeholderTextColor: Theme.text3
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.family: mono ? Theme.monoFamily : Theme.fontFamily
    font.pixelSize: Theme.fsBody
    leftPadding: Theme.sp2
    rightPadding: Theme.sp2
    background: Rectangle {
        radius: Theme.rSm
        color: Theme.surface2
        border.color: parent.activeFocus ? Theme.accent : Theme.borderDefault
    }
}
