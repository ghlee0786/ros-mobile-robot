import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// ghost 아이콘 버튼 (16px). tip 을 주면 ToolTip.
Button {
    id: b
    property string tip
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    implicitWidth: Theme.hSm
    implicitHeight: Theme.hSm
    padding: 0
    display: AbstractButton.IconOnly
    icon.width: 16
    icon.height: 16
    icon.color: b.hovered || b.checked ? Theme.text1 : Theme.text2
    opacity: enabled ? 1 : Theme.opDisabled
    background: Rectangle {
        radius: Theme.rSm
        color: Theme.text1
        opacity: b.down ? Theme.stPressed : b.hovered ? Theme.stHover : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }
    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 500
}
