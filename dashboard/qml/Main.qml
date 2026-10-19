import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

ApplicationWindow {
    id: win
    objectName: "win"
    width: 1440
    height: 980
    visible: true
    title: "로봇 대시보드"
    color: "#101318"
    font.family: "Malgun Gothic"

    property int page: 0   // 0 대시보드 · 1 데이터 점검 · 2 연결
    readonly property string mono: "Consolas"

    Robot { id: robot; objectName: "robot" }

    // 창 크기는 다음 실행에도 그대로 (위치는 모니터 구성이 바뀌면 화면 밖에 뜰 수 있어 저장하지 않는다)
    Settings {
        category: "window"
        property alias width: win.width
        property alias height: win.height
    }

    // 켜면 마지막 주소로 자동 연결하고, 4 s 안에 붙지 않으면 연결 화면을 띄운다.
    Component.onCompleted: robot.connectTo(robot.lastHost, robot.lastPort)
    Timer {
        interval: 4000
        running: true
        onTriggered: if (!robot.bridge.connected) win.page = 2
    }

    header: Rectangle {
        color: "#161A21"
        implicitHeight: hdr.implicitHeight + 24
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#2B323C" }
        RowLayout {
            id: hdr
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 20
            Row {
                spacing: 10
                Text { text: "로봇 대시보드"; color: "#E6E9EE"; font.pixelSize: 18; font.bold: true }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "robot1 · ROS 2 Foxy"; color: "#9AA3AF"; font.family: win.mono; font.pixelSize: 12
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    readonly property bool ok: robot.bridge.connected
                    radius: 999
                    color: ok ? "#17302A" : "#3A1A1C"
                    width: chipRow.implicitWidth + 20
                    height: 26
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: parent.parent.ok ? "#5BC48A" : "#D93A40" }
                        Text {
                            color: parent.parent.ok ? "#8EE2B4" : "#F4B4B6"
                            font.pixelSize: 13
                            text: "rosbridge " + robot.bridge.host + ":" + robot.bridge.port + " · "
                                  + (parent.parent.ok ? "연결됨 · " + robot.bridge.latencyMs + " ms" : "연결 끊김 · 재연결 중")
                        }
                    }
                }
                Repeater {
                    model: robot.rates
                    Rectangle {
                        required property string modelData
                        radius: 999; color: "#1D232C"; height: 26; width: rt.implicitWidth + 20
                        Text { id: rt; anchors.centerIn: parent; text: modelData; color: "#C5CBD3"; font.family: win.mono; font.pixelSize: 12 }
                    }
                }
                Rectangle {
                    radius: 999; height: 26; width: wd.implicitWidth + 20
                    color: "#1D232C"
                    Text {
                        id: wd; anchors.centerIn: parent; font.pixelSize: 12
                        text: robot.watchdogOk ? "워치독 정상" : "워치독 확인 안 됨"
                        color: robot.watchdogOk ? "#8EE2B4" : "#F5D27A"
                    }
                }
            }
            Row {
                spacing: 6
                Repeater {
                    model: ["대시보드", "데이터 점검", "연결"]
                    Button {
                        required property string modelData
                        required property int index
                        text: modelData
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 14
                        onClicked: win.page = index
                        contentItem: Text { text: parent.text; font: parent.font; color: win.page === index ? "#E6F6FD" : "#C5CBD3" }
                        background: Rectangle { radius: 8; color: win.page === index ? "#23405A" : "#1D232C" }
                    }
                }
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: win.page
        DashboardPage { robot: robot; active: win.page === 0 }
        InspectorPage { robot: robot }
        ConnectPage { robot: robot; onDone: win.page = 0 }
    }

    // 연결 · 하트비트가 끊기면 화면 테두리를 빨갛게
    Rectangle {
        parent: win.contentItem.parent
        anchors.fill: parent
        z: 1000
        color: "transparent"
        border.color: "#D93A40"
        border.width: robot.linkLost ? 6 : 0
    }
}
