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
    color: Theme.canvas
    font.family: Theme.fontFamily

    property int page: 0   // 0 대시보드 · 1 데이터 점검 · 2 연결
    readonly property string mono: Theme.monoFamily

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
        color: Theme.base
        implicitHeight: hdr.implicitHeight + 2 * Theme.sp3
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
        RowLayout {
            id: hdr
            anchors.fill: parent
            anchors.leftMargin: Theme.sp5
            anchors.rightMargin: Theme.sp5
            spacing: Theme.sp5
            Row {
                spacing: Theme.sp2
                Text { id: appTitle; text: "로봇 대시보드"; color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Theme.wStrong }
                Text {
                    anchors.baseline: appTitle.baseline
                    text: "robot1 · ROS 2 Foxy"; color: Theme.text3; font.family: Theme.monoFamily; font.pixelSize: Theme.fsSmall
                }
            }
            StatusBar { robot: robot; Layout.fillWidth: true }
            Row {
                spacing: Theme.sp1
                Repeater {
                    model: ["대시보드", "데이터 점검", "연결"]
                    Button {
                        id: tab
                        required property string modelData
                        required property int index
                        readonly property bool selected: win.page === index
                        text: modelData
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        implicitHeight: Theme.hSm
                        leftPadding: Theme.sp3; rightPadding: Theme.sp3
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fsLabel
                        font.weight: selected ? Theme.wMedium : Theme.wRegular
                        onClicked: win.page = index
                        contentItem: Text {
                            text: tab.text; font: tab.font; verticalAlignment: Text.AlignVCenter
                            color: tab.selected || tab.hovered ? Theme.text1 : Theme.text2
                        }
                        background: Rectangle {
                            radius: Theme.rSm
                            color: tab.selected ? Theme.accentSubtle : Theme.text1
                            opacity: tab.selected ? 1 : tab.down ? Theme.stPressed : tab.hovered ? Theme.stHover : 0
                        }
                    }
                }
            }
        }
    }

    footer: ActionBar { robot: robot }

    StackLayout {
        anchors.fill: parent
        currentIndex: win.page
        DashboardPage { robot: robot; active: win.page === 0 }
        InspectorPage { robot: robot }
        ConnectPage { robot: robot; onDone: win.page = 0 }
    }

    // 연결 · 하트비트가 끊기면 화면 테두리를 빨갛게 하고 위쪽에 경과 시간 배너를 띄운다
    Rectangle {
        parent: win.contentItem.parent
        anchors.fill: parent
        z: 1000
        color: "transparent"
        border.color: Theme.danger
        border.width: robot.linkLost ? 6 : 0
    }
    Banner {
        parent: win.contentItem
        z: 1001
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.sp3
        active: robot.linkLost
        text: "연결 끊김"
    }

    CommandPalette {
        robot: robot
        onPageRequested: (i) => win.page = i
    }

    // 동봉 글꼴(Pretendard · JetBrains Mono). 읽지 못하면 Theme 의 기본값(Malgun Gothic · Consolas)을 그대로 쓴다.
    FontLoader { id: uiFont; source: "qrc:/fonts/PretendardVariable.ttf" }
    FontLoader { id: monoFont; source: "qrc:/fonts/JetBrainsMono-Regular.ttf" }
    Binding { target: Theme; property: "fontFamily"; value: uiFont.name; when: uiFont.status === FontLoader.Ready }
    Binding { target: Theme; property: "monoFamily"; value: monoFont.name; when: monoFont.status === FontLoader.Ready }
}
