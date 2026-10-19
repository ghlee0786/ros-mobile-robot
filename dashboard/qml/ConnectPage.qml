import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import RobotDashboard

// 연결: 주소 · 포트, 저장된 로봇, 네트워크에서 찾기, 연결 단계 확인
Item {
    id: page
    required property Robot robot
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
        implicitHeight: Theme.hLg
        color: Theme.text1
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fsLabel
        leftPadding: Theme.sp3
        background: Rectangle {
            radius: Theme.rSm
            color: Theme.base
            border.color: parent.activeFocus ? Theme.accent : Theme.borderDefault
        }
    }
    component FieldLabel: Text { color: Theme.text2; font.pixelSize: Theme.fsSmall; font.weight: Theme.wMedium }

    Rectangle {
        anchors.centerIn: parent
        width: 720
        height: col.implicitHeight + 2 * Theme.sp8
        radius: Theme.rXl
        color: Theme.surface1
        border.color: Theme.borderSubtle

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: Theme.sp8
            spacing: Theme.sp5

            Column {
                spacing: Theme.sp1
                Text { text: "로봇에 연결"; color: Theme.text1; font.pixelSize: Theme.fsHeading; font.weight: Theme.wStrong; font.letterSpacing: -0.3 }
                Text { text: "앱을 켜면 마지막 주소로 자동 연결합니다. 실패하면 이 화면이 뜹니다."; color: Theme.text2; font.pixelSize: Theme.fsBody }
            }

            GridLayout {
                columns: 2
                columnSpacing: Theme.sp3
                rowSpacing: Theme.spNudge
                Layout.fillWidth: true
                FieldLabel { text: "로봇 주소" }
                FieldLabel { text: "rosbridge 포트" }
                Field { id: host; text: robot.lastHost }
                Field { id: port; text: robot.lastPort; validator: IntValidator { bottom: 1; top: 65535 } }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spNudge
                FieldLabel { text: "저장된 로봇" }
                Repeater {
                    model: robot.savedRobots.concat(robot.bridge.found.map(h => ({ name: "찾은 rosbridge", host: h })))
                    Button {
                        id: pick
                        required property var modelData
                        readonly property bool selected: host.text === modelData.host
                        Layout.fillWidth: true
                        implicitHeight: Theme.hLg + Theme.sp1
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        leftPadding: Theme.sp3
                        rightPadding: Theme.sp3
                        onClicked: host.text = modelData.host
                        contentItem: RowLayout {
                            Text { text: pick.modelData.name; color: Theme.text1; font.pixelSize: Theme.fsLabel; Layout.fillWidth: true }
                            Text { text: pick.modelData.host; color: pick.selected ? Theme.accentText : Theme.text2; font.family: Theme.monoFamily; font.pixelSize: Theme.fsBody }
                        }
                        background: Rectangle {
                            radius: Theme.rSm
                            color: pick.selected ? Theme.accentSubtle : Theme.surface2
                            border.color: pick.selected ? Theme.accent : Theme.borderDefault
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: Theme.text1
                                opacity: pick.down ? Theme.stPressed : pick.hovered ? Theme.stHover : 0
                            }
                        }
                    }
                }
            }

            // 연결 단계: 상태마다 점(정상 · 기다림) 또는 아이콘(실패)과 원인을 같은 행에 붙인다
            Rectangle {
                Layout.fillWidth: true
                radius: Theme.rMd
                color: Theme.base
                border.color: Theme.borderSubtle
                implicitHeight: checks.implicitHeight + 2 * Theme.sp3
                Column {
                    id: checks
                    x: Theme.sp4; y: Theme.sp3
                    width: parent.width - 2 * Theme.sp4
                    spacing: Theme.sp2
                    component Check: Row {
                        id: chk
                        property bool ok
                        property bool fail
                        property url failIcon: "qrc:/icons/octagon-alert.svg"
                        property string label
                        property string cause
                        readonly property bool waiting: !ok && !fail   // 시험 actions 의 wait 대상(connectCheckTopics.waiting)
                        readonly property color tone: ok ? Theme.okText : fail ? Theme.dangerText : Theme.warnText
                        spacing: Theme.sp2
                        Item {
                            width: 16; height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                visible: !chk.fail
                                anchors.centerIn: parent
                                width: 8; height: 8; radius: width / 2
                                color: chk.ok ? Theme.ok : Theme.warn
                            }
                            IconImage {
                                visible: chk.fail
                                anchors.fill: parent
                                source: chk.failIcon
                                sourceSize: Qt.size(16, 16)
                                color: chk.tone
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chk.label
                            color: chk.fail ? Theme.dangerText : Theme.text1
                            font.pixelSize: Theme.fsBody
                            font.features: Theme.numFeatures
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: chk.cause !== ""
                            text: chk.cause
                            color: Theme.text2
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fsSmall
                        }
                    }
                    Check {
                        ok: robot.bridge.connected
                        fail: !robot.bridge.connected && robot.bridge.error !== ""
                        failIcon: "qrc:/icons/plug.svg"
                        label: robot.bridge.connected ? "웹소켓 연결 확인"
                             : fail ? "웹소켓 연결 실패" : "웹소켓 연결 기다리는 중…"
                        cause: fail ? robot.bridge.error : ""
                    }
                    // 끊기면 앞서 받은 목록 · 센서도 다시 기다림으로 보인다
                    Check {
                        objectName: "connectCheckTopics"
                        ok: robot.bridge.connected && robot.bridge.topicListReceived
                        label: ok ? "토픽 목록 수신 (" + robot.bridge.topicCount + "개)" : "토픽 목록 기다리는 중…"
                    }
                    Check {
                        objectName: "connectCheckRequired"
                        ok: robot.bridge.connected && robot.missingRequired.length === 0
                        // 토픽 목록까지 받았는데 필수 센서가 안 오면 실패로 보인다
                        fail: robot.bridge.connected && robot.bridge.topicListReceived && robot.missingRequired.length > 0
                        failIcon: "qrc:/icons/wifi-off.svg"
                        label: ok ? "필수 토픽 수신 (/odom, /scan, /wheel_states)"
                             : fail ? "센서 수신 안 됨" : "필수 토픽 기다리는 중…"
                        cause: ok ? "" : robot.missingRequired.join(", ")
                    }
                    Check {
                        visible: robot.bridge.searching || robot.bridge.found.length > 0
                        ok: !robot.bridge.searching
                        label: robot.bridge.searching ? "네트워크에서 :" + port.text + " 찾는 중…" : "찾은 주소 " + robot.bridge.found.length + "개"
                    }
                }
            }

            Row {
                spacing: Theme.sp2
                Btn {
                    text: "연결"
                    kind: "accent"
                    size: "lg"
                    icon.source: "qrc:/icons/plug.svg"
                    onClicked: { page.waiting = true; robot.connectTo(host.text.trim(), parseInt(port.text)) }
                }
                Btn {
                    text: "네트워크에서 찾기"
                    size: "lg"
                    icon.source: "qrc:/icons/search.svg"
                    enabled: !robot.bridge.searching
                    onClicked: robot.bridge.discover()
                }
            }
        }
    }
}
