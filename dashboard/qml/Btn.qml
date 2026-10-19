import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import RobotDashboard

// 버튼: kind normal(surface2) · accent(채움, 화면당 하나) · danger(채움, 비상정지 · 취소) · ghost(투명)
// size sm 32 · md 36 · lg 40 · estop 44. hover · 눌림은 text1 상태 레이어, 비활성은 opacity 0.4.
Button {
    id: b
    property string kind: "normal"
    property string size: "md"
    readonly property bool filled: kind === "accent" || kind === "danger"
    readonly property color fg: filled ? Theme.textOnAccent : kind === "ghost" && !hovered ? Theme.text2 : Theme.text1

    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    implicitHeight: size === "sm" ? Theme.hSm : size === "lg" ? Theme.hLg : size === "estop" ? Theme.hEstop : Theme.hMd
    leftPadding: size === "sm" ? Theme.sp3 : Theme.sp4
    rightPadding: leftPadding
    spacing: Theme.sp2
    font.family: Theme.fontFamily
    font.pixelSize: size === "sm" ? Theme.fsBody : Theme.fsLabel
    font.weight: filled ? Theme.wStrong : Theme.wMedium
    icon.width: 16
    icon.height: 16
    icon.color: fg
    opacity: enabled ? 1 : Theme.opDisabled

    contentItem: IconLabel {
        spacing: b.spacing
        display: b.display
        icon: b.icon
        text: b.text
        font: b.font
        color: b.fg
    }
    background: Rectangle {
        radius: Theme.rSm
        color: b.kind === "danger" ? Theme.danger : b.kind === "accent" ? Theme.accent
             : b.kind === "ghost" ? "transparent" : Theme.surface2
        border.color: b.kind === "normal" ? Theme.borderDefault : "transparent"
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.text1
            opacity: b.down ? Theme.stPressed : b.hovered ? Theme.stHover : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        }
    }
}
