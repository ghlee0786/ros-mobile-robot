import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 진단 탭: /diagnostics 를 'hardware_id: name' 으로 묶은 목록(그룹 고정 → Stale → Error → Warn → Ok)과 key/value 상세.
// 진단이 한 번도 안 왔으면 robot.sensors 로 합성한 항목을 '합성됨' 으로 보인다.
RowLayout {
    id: tab
    required property Robot robot
    required property DiagnosticsModel diag
    property string selectedKey
    property var selectedValues: []
    spacing: Theme.sp4

    function kindOf(level) { return ["ok", "warn", "danger", "stale"][level] ?? "neutral" }

    // 합성 항목은 센서 상태가 바뀔 때마다 다시 만든다(탭이 보일 때만)
    Connections {
        target: tab.robot
        enabled: tab.diag.active
        function onStatusChanged() { tab.diag.setSynthetic(tab.robot.sensors) }
    }
    Connections {
        target: tab.diag
        function onActiveChanged() { if (tab.diag.active) tab.diag.setSynthetic(tab.robot.sensors) }
    }

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: "진단"
        subtitle: tab.diag.count + "개"
        headerExtra: [
            StatusChip { visible: tab.diag.synthetic; kind: "neutral"; text: "합성됨 · /diagnostics 없음" },
            InField { width: 220; placeholderText: "이름 · 하드웨어 거르기"; onTextChanged: tab.diag.filter = text }
        ]

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tab.diag
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            section.property: "group"
            section.delegate: Text {
                required property string section
                topPadding: Theme.sp2
                bottomPadding: Theme.sp1
                text: section + " · " + (tab.diag.groupCounts[section] ?? 0)
                color: Theme.text2
                font.pixelSize: Theme.fsSmall
                font.weight: Theme.wMedium
            }
            delegate: Rectangle {
                id: item
                required property string key
                required property string name
                required property string hardwareId
                required property int level
                required property string levelName
                required property string message
                required property var values
                required property bool pinned
                readonly property bool sel: tab.selectedKey === key
                width: ListView.view.width
                height: 36
                color: sel ? Theme.accentSubtle : "transparent"
                onValuesChanged: if (sel) tab.selectedValues = values
                TapHandler { onTapped: { tab.selectedKey = item.key; tab.selectedValues = item.values } }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.sp1
                    spacing: Theme.sp2
                    Btn {
                        size: "sm"; kind: "ghost"
                        text: item.pinned ? "고정됨" : "고정"
                        onClicked: tab.diag.togglePin(item.key)
                    }
                    StatusChip { kind: tab.kindOf(item.level); text: item.levelName }
                    Text {
                        Layout.preferredWidth: 260
                        text: item.key; elide: Text.ElideRight
                        color: Theme.text1; font.pixelSize: Theme.fsBody
                    }
                    Text {
                        Layout.fillWidth: true
                        text: item.message; elide: Text.ElideRight
                        color: Theme.text2; font.pixelSize: Theme.fsSmall
                    }
                }
            }
        }
        EmptyState {
            Layout.alignment: Qt.AlignHCenter
            visible: tab.diag.count === 0
            title: "진단 항목이 없음"
            description: "/diagnostics 도 센서 상태도 아직 없음"
        }
    }

    Panel {
        Layout.preferredWidth: 380
        Layout.fillHeight: true
        title: "상세"
        subtitle: tab.selectedKey
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tab.selectedValues
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                Text {
                    Layout.preferredWidth: 150
                    text: modelData.key; elide: Text.ElideRight
                    color: Theme.text2; font.pixelSize: Theme.fsSmall
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.value; wrapMode: Text.WrapAnywhere
                    color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsSmall
                }
            }
        }
        EmptyState {
            Layout.alignment: Qt.AlignHCenter
            visible: tab.selectedKey === ""
            title: "항목을 고르면 key/value 를 보인다"
        }
    }
}
