import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 연결: 주소 · 포트, 저장된 로봇, 네트워크에서 찾기, 연결 단계 확인
Item {
    id: page
    required property Robot robot
    readonly property string mono: "Consolas"
    property bool waiting: false
    signal done()

    Connections {
        target: robot.bridge
        function onTopicListChanged() {
            if (page.waiting && robot.bridge.topicListReceived) { page.waiting = false; page.done() }
        }
    }

    component Field: TextField {
        Layout.fillWidth: true
        implicitHeight: 44
        color: "#E6E9EE"
        font.family: page.mono
        font.pixelSize: 15
        leftPadding: 12
        background: Rectangle { radius: 10; color: "#0F1318"; border.color: "#2E3744" }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 720
        height: col.implicitHeight + 72
        color: "#161A21"
        border.color: "#2B323C"

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 36
            anchors.leftMargin: 40
            anchors.rightMargin: 40
            spacing: 20

            Column {
                Text { text: "로봇에 연결"; color: "#E6E9EE"; font.pixelSize: 22; font.bold: true }
                Text { text: "앱을 켜면 마지막 주소로 자동 연결합니다. 실패하면 이 화면이 뜹니다."; color: "#9AA3AF"; font.pixelSize: 13 }
            }

            GridLayout {
                columns: 2
                columnSpacing: 14
                rowSpacing: 6
                Layout.fillWidth: true
                Text { text: "로봇 주소"; color: "#C5CBD3"; font.pixelSize: 13 }
                Text { text: "rosbridge 포트"; color: "#C5CBD3"; font.pixelSize: 13 }
                Field { id: host; text: robot.lastHost }
                Field { id: port; text: robot.lastPort; validator: IntValidator { bottom: 1; top: 65535 } }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: "저장된 로봇"; color: "#C5CBD3"; font.pixelSize: 13 }
                Repeater {
                    model: robot.savedRobots.concat(robot.bridge.found.map(h => ({ name: "찾은 rosbridge", host: h })))
                    Button {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 48
                        onClicked: host.text = modelData.host
                        contentItem: RowLayout {
                            Text { text: modelData.name; color: "#E6E9EE"; font.pixelSize: 14; Layout.fillWidth: true }
                            Text { text: modelData.host; color: "#E6E9EE"; font.family: page.mono; font.pixelSize: 13 }
                        }
                        background: Rectangle {
                            radius: 10
                            color: host.text === modelData.host ? "#1B3446" : "#1D232C"
                            border.color: host.text === modelData.host ? "#2E5A73" : "#2E3744"
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: "#0F1318"
                implicitHeight: checks.implicitHeight + 24
                Column {
                    id: checks
                    x: 14; y: 12
                    spacing: 4
                    component Check: Text {
                        property bool ok
                        property bool fail
                        property string label
                        textFormat: Text.StyledText
                        font.pixelSize: 13
                        color: "#E6E9EE"
                        text: "<font color='" + (ok ? "#5BC48A" : fail ? "#D93A40" : "#F2A33A") + "'>●</font> " + label
                    }
                    Check {
                        ok: robot.bridge.connected
                        fail: !robot.bridge.connected && robot.bridge.error !== ""
                        label: robot.bridge.connected ? "웹소켓 연결 확인"
                             : robot.bridge.error !== "" ? "웹소켓 연결 실패 · " + robot.bridge.error : "웹소켓 연결 기다리는 중…"
                    }
                    Check {
                        ok: robot.bridge.topicListReceived
                        label: robot.bridge.topicListReceived ? "토픽 목록 수신 (" + robot.bridge.topicCount + "개)" : "토픽 목록 기다리는 중…"
                    }
                    Check {
                        ok: robot.missingRequired.length === 0
                        label: robot.missingRequired.length === 0 ? "필수 토픽 수신 (/odom, /scan, /wheel_states)" : robot.missingRequired.join(", ") + " 기다리는 중…"
                    }
                    Check {
                        visible: robot.bridge.searching || robot.bridge.found.length > 0
                        ok: !robot.bridge.searching
                        label: robot.bridge.searching ? "네트워크에서 :" + port.text + " 찾는 중…" : "찾은 주소 " + robot.bridge.found.length + "개"
                    }
                }
            }

            Row {
                spacing: 10
                Btn {
                    text: "연결"
                    implicitHeight: 48
                    font.pixelSize: 15; font.bold: true
                    leftPadding: 22; rightPadding: 22
                    background: Rectangle { radius: 10; color: parent.down ? "#236D91" : "#2B7FA8" }
                    onClicked: { page.waiting = true; robot.connectTo(host.text.trim(), parseInt(port.text)) }
                }
                Btn {
                    text: "네트워크에서 찾기"
                    implicitHeight: 48
                    enabled: !robot.bridge.searching
                    onClicked: robot.bridge.discover()
                }
            }
        }
    }
}
