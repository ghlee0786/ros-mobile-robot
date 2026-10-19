import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 노드 탭: /rosapi/nodes · /rosapi/node_details 로 노드의 발행 · 구독 · 서비스를 보인다.
// 필수 토픽 누락(robot.missingRequired)의 원인(그 토픽을 발행하는 노드가 있는지)을 찾는 데 쓴다.
RowLayout {
    id: tab
    required property Robot robot
    required property RosApiClient api
    property bool active: false
    spacing: Theme.sp4

    onActiveChanged: if (active && api.nodes.length === 0) api.refreshNodes()

    Panel {
        Layout.preferredWidth: 340
        Layout.fillHeight: true
        title: "노드"
        subtitle: tab.api.nodes.length + "개"
        headerExtra: Btn { size: "sm"; text: "새로고침"; onClicked: tab.api.refreshNodes() }

        LoadingBar { Layout.fillWidth: true; running: tab.api.callsPending > 0 }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "파이의 rosbridge 는 파이 도메인만 본다. PC 에서 도는 Nav2(WSL 도메인 8) 노드는 이 목록에 없을 수 있다."
            color: Theme.text3
            font.pixelSize: Theme.fsCaption
        }
        Text {
            Layout.fillWidth: true
            visible: tab.api.error !== ""
            wrapMode: Text.WordWrap
            text: "실패: " + tab.api.error
            color: Theme.dangerText
            font.pixelSize: Theme.fsSmall
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tab.api.nodes
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property string modelData
                readonly property bool sel: tab.api.node === modelData
                width: ListView.view.width
                height: 30
                radius: Theme.rXs
                color: sel ? Theme.accentSubtle : "transparent"
                Text {
                    anchors.fill: parent
                    leftPadding: Theme.sp2
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: modelData
                    color: parent.sel ? Theme.accentText : Theme.text1
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fsBody
                }
                TapHandler { onTapped: tab.api.selectNode(modelData) }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Theme.sp3

        Panel {
            Layout.fillWidth: true
            visible: tab.robot.missingRequired.length > 0
            title: "필수 토픽 누락"
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: tab.robot.missingRequired.join(", ")
                    + " — 노드를 골라 발행 목록에 이 토픽이 있는지 본다. 어느 노드에도 없으면 그 토픽을 내는 노드가 꺼진 것이다."
                color: Theme.warnText
                font.pixelSize: Theme.fsBody
            }
        }

        component TopicList: Panel {
            id: tl
            property var items: []
            Layout.fillWidth: true
            Layout.fillHeight: true
            subtitle: items.length + "개"
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 80
                clip: true
                model: tl.items
                delegate: Text {
                    required property string modelData
                    width: ListView.view.width
                    elide: Text.ElideRight
                    text: modelData
                    color: tab.robot.missingRequired.indexOf(modelData) >= 0 ? Theme.warnText : Theme.text1
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fsSmall
                }
            }
        }
        TopicList { title: "발행"; items: tab.api.publishing }
        TopicList { title: "구독"; items: tab.api.subscribing }
        TopicList { title: "서비스"; items: tab.api.nodeServices }
        EmptyState {
            Layout.alignment: Qt.AlignHCenter
            visible: tab.api.node === ""
            title: "노드를 고르면 발행 · 구독 · 서비스를 보인다"
        }
    }
}
