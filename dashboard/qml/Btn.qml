import QtQuick
import QtQuick.Controls.Basic

// 와이어프레임의 버튼 세 가지: 기본(어두움) · accent(파랑) · danger(빨강)
Button {
    id: b
    property string kind: "normal"
    focusPolicy: Qt.NoFocus
    implicitHeight: 44
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14
    contentItem: Text {
        text: b.text
        font: b.font
        color: b.kind === "normal" ? "#E6E9EE" : b.kind === "accent" ? "#CFEFFB" : "#FFFFFF"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: b.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: 10
        color: b.kind === "danger" ? (b.down ? "#B32E33" : "#D93A40")
             : b.kind === "accent" ? (b.down ? "#24485F" : "#1B3446")
             : (b.down ? "#26303B" : "#1D232C")
        border.color: b.kind === "danger" ? "transparent" : b.kind === "accent" ? "#2E5A73" : "#2E3744"
    }
}
