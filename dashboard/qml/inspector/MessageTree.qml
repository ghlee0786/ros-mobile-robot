import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// MessageTreeModel 을 키 · 값 · 형식 3열로 보이는 트리(원문 메시지 · 서비스 응답).
// 바뀐 필드는 accentSubtle 로 칠하고, 숫자 행 오른쪽 클릭 메뉴 '경로 복사' 로 메시지 경로를 클립보드에 넣는다.
ColumnLayout {
    id: root
    required property MessageTreeModel tree
    spacing: 0

    Row {
        Layout.fillWidth: true
        Repeater {
            model: ["키", "값", "형식"]
            Text {
                required property string modelData
                required property int index
                width: view.columnWidthProvider(index)
                leftPadding: Theme.sp2
                text: modelData
                color: Theme.text2
                font.pixelSize: Theme.fsSmall
            }
        }
    }

    TreeView {
        id: view
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.tree
        boundsBehavior: Flickable.StopAtBounds
        columnWidthProvider: c => [0.45, 0.37, 0.18][c] * width
        onWidthChanged: forceLayout()
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: cell
            required property TreeView treeView
            required property bool expanded
            required property bool hasChildren
            required property int depth
            required property int row
            required property int column
            required property string display
            required property bool changed
            required property bool numeric
            required property bool more
            implicitHeight: 24

            Rectangle { anchors.fill: parent; color: cell.changed ? Theme.accentSubtle : "transparent" }
            Text {
                anchors.fill: parent
                leftPadding: cell.column === 0 ? Theme.sp2 + cell.depth * Theme.sp4 : Theme.sp2
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: (cell.column === 0 && cell.hasChildren ? (cell.expanded ? "▾ " : "▸ ") : "") + cell.display
                color: cell.more || cell.column === 2 ? Theme.text3 : Theme.text1
                font.family: cell.column === 0 ? Theme.fontFamily : Theme.monoFamily
                font.pixelSize: cell.column === 2 ? Theme.fsSmall : Theme.fsBody
                font.features: Theme.numFeatures
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: if (cell.hasChildren) cell.treeView.toggleExpanded(cell.row)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                enabled: cell.numeric
                onTapped: {
                    menu.target = cell.treeView.index(cell.row, 0)
                    menu.popup()
                }
            }
        }
    }

    Menu {
        id: menu
        property var target
        background: Rectangle {
            implicitWidth: 160
            radius: Theme.rSm
            color: Theme.surface3
            border.color: Theme.borderDefault
        }
        MenuItem {
            id: copyItem
            text: "경로 복사"
            onTriggered: root.tree.copyPath(menu.target)
            contentItem: Text { text: copyItem.text; color: Theme.text1; font.pixelSize: Theme.fsBody }
            background: Rectangle { color: copyItem.highlighted ? Theme.surface2 : "transparent"; radius: Theme.rXs }
        }
    }
}
