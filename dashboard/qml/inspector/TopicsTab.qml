import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs
import RobotDashboard

// 토픽 탭: 퍼지 검색(토픽명 · 본 적 있는 필드 경로) · 전체 수신 Mbps · 지연 60 s 스파크라인 · 토픽 표.
// 행을 누르면 오른쪽에 원문 메시지 트리를 연다(그 토픽을 'inspect' 이유로 200 ms throttle 구독, 닫으면 끔).
RowLayout {
    id: tab
    required property Robot robot
    required property MessageTreeModel tree
    property bool active: false
    property var knownPaths: ({})   // 토픽 → 숫자 필드 경로(원문을 연 적 있는 토픽만)
    readonly property bool live: robot.bridge.connected   // 끊기면 마지막 값 대신 '끊김' · '—' 를 보인다
    spacing: Theme.sp4

    function shownState(state) { return tab.live ? state : "끊김" }

    PlotFeeder { id: fuzzy; active: false }   // fuzzyScore · flattenNumericPaths 만 쓴다

    function matches(name) {
        const q = search.text.trim()
        if (q === "")
            return true
        if (fuzzy.fuzzyScore(q, name) >= 0)
            return true
        for (const p of tab.knownPaths[name] ?? [])
            if (fuzzy.fuzzyScore(q, p) >= 0)
                return true
        return false
    }

    Connections {
        target: tab.tree
        function onMessageChanged() {
            if (tab.tree.topic === "" || tab.tree.messageCount === 0)
                return
            const m = Object.assign({}, tab.knownPaths)
            m[tab.tree.topic] = fuzzy.flattenNumericPaths(tab.tree.topic, tab.tree.message, 32)
            tab.knownPaths = m
        }
    }
    onActiveChanged: if (!active) tree.topic = ""

    component Spark: ColumnLayout {
        id: spark
        property string label
        property string value
        property var points: []
        property color lineColor
        spacing: 2
        Layout.fillWidth: true
        RowLayout {
            Text { text: spark.label; color: Theme.text2; font.pixelSize: Theme.fsSmall; Layout.fillWidth: true }
            Text { text: spark.value; color: Theme.text1; font.pixelSize: Theme.fsBody; font.features: Theme.numFeatures }
        }
        GraphsView {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            marginLeft: 0; marginRight: 0; marginTop: 2; marginBottom: 0
            theme: GraphsTheme {
                backgroundVisible: false
                plotAreaBackgroundColor: Theme.surface1
            }
            axisX: ValueAxis { min: 0; max: 59; visible: false; lineVisible: false; gridVisible: false; subGridVisible: false }
            axisY: ValueAxis {
                min: 0; max: Math.max(0.001, ...spark.points) * 1.15
                visible: false; lineVisible: false; gridVisible: false; subGridVisible: false
            }
            LineSeries {
                color: spark.lineColor; width: 1.5
                property var pts: spark.points
                onPtsChanged: {
                    clear()
                    const off = 60 - pts.length
                    for (let i = 0; i < pts.length; ++i) append(off + i, pts[i])
                }
            }
        }
    }

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: "토픽"
        subtitle: tab.robot.topicModel.count + "개"
        headerExtra: InField {
            id: search
            width: 260
            placeholderText: "토픽 · 필드 경로 찾기"
            mono: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp4
            Spark {
                label: "전체 수신 (60 s)"
                value: tab.live ? (tab.robot.bridge.rxBytesPerSec * 8 / 1e6).toFixed(2) + " Mbps" : "—"
                points: tab.active ? tab.robot.bridge.rxHistory.map(b => b * 8 / 1e6) : []
                lineColor: Theme.series[0]
            }
            Spark {
                label: "rosbridge 지연 (60 s)"
                value: tab.live && tab.robot.bridge.latencyMs >= 0 ? tab.robot.bridge.latencyMs + " ms" : "—"
                points: tab.active ? tab.robot.bridge.latencyHistory : []
                lineColor: Theme.series[1]
            }
        }

        component Head: Text {
            color: Theme.text2
            font.pixelSize: Theme.fsSmall
            leftPadding: Theme.sp2
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Head { text: "이름"; Layout.fillWidth: true; Layout.preferredWidth: 200 }
            Head { text: "형식"; Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.maximumWidth: 380 }
            Head { text: "Hz"; Layout.preferredWidth: 64 }
            Head { text: "지연"; Layout.preferredWidth: 76 }
            Head { text: "KB/s"; Layout.preferredWidth: 64 }
            Head { text: "상태"; Layout.preferredWidth: 104 }
            Head { text: "기록"; Layout.preferredWidth: 44 }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tab.robot.topicModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property string name
                required property string type
                required property double hz
                required property double latencyMs
                required property double kbps
                required property string state
                required property bool rec
                required property bool clockSkew
                readonly property bool shown: tab.matches(name)
                readonly property bool selected: tab.tree.topic === name
                width: ListView.view.width
                height: shown ? 32 : 0
                visible: shown

                Rectangle {
                    anchors.fill: parent
                    color: row.selected ? Theme.accentSubtle : Theme.text1
                    opacity: row.selected ? 1 : hover.hovered ? Theme.stHover : 0
                }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
                HoverHandler { id: hover }
                TapHandler { onTapped: tab.tree.topic = row.selected ? "" : row.name }

                component Cell: Text {
                    Layout.fillHeight: true
                    leftPadding: Theme.sp2
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    color: Theme.text1
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fsBody
                    font.features: Theme.numFeatures
                }
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Cell { text: row.name; Layout.fillWidth: true; Layout.preferredWidth: 200 }
                    Cell {
                        text: row.type; color: Theme.text2; font.pixelSize: Theme.fsSmall
                        Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.maximumWidth: 380
                    }
                    Cell { text: tab.live && row.hz > 0 ? row.hz.toFixed(1) : "—"; Layout.preferredWidth: 64 }
                    Cell {
                        Layout.preferredWidth: 76
                        readonly property bool skew: tab.live && row.clockSkew
                        text: skew ? "시계 차" : tab.live && row.latencyMs >= 0 ? row.latencyMs + " ms" : "—"
                        color: skew ? Theme.warnText : Theme.text1
                        font.family: skew ? Theme.fontFamily : Theme.monoFamily
                    }
                    Cell { text: tab.live && row.kbps > 0 ? (row.kbps / 8).toFixed(1) : "—"; Layout.preferredWidth: 64 }
                    Item {
                        Layout.preferredWidth: 104
                        Layout.fillHeight: true
                        StatusChip {
                            anchors.verticalCenter: parent.verticalCenter
                            x: Theme.sp1
                            text: tab.shownState(row.state)
                            kind: text === "정상" ? "ok" : text === "구독 안 함" ? "neutral" : "warn"
                        }
                    }
                    Item {
                        Layout.preferredWidth: 44
                        Layout.fillHeight: true
                        InCheck {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: row.rec
                            onToggled: tab.robot.setRecordTopic(row.name, checked)
                        }
                    }
                }
            }
        }
        EmptyState {
            Layout.alignment: Qt.AlignHCenter
            visible: tab.robot.topicModel.count === 0
            title: "토픽 목록이 없음"
            description: tab.robot.bridge.connected ? "rosapi 토픽 목록을 기다리는 중" : "rosbridge 에 연결되지 않음"
        }
    }

    Panel {
        Layout.preferredWidth: Math.max(440, tab.width * 0.32)   // 넓은 창에서는 원문 키 열도 넓힌다
        Layout.fillHeight: true
        visible: tab.tree.topic !== ""
        title: "원문"
        subtitle: tab.tree.topic
        headerExtra: Btn { text: "닫기"; size: "sm"; kind: "ghost"; onClicked: tab.tree.topic = "" }

        RowLayout {
            Layout.fillWidth: true
            InCheck { text: "직전 메시지와 비교"; checked: tab.tree.compare; onToggled: tab.tree.compare = checked; Layout.fillWidth: true }
            StatusChip {
                visible: tab.tree.compare && tab.tree.hasPrevious
                kind: tab.tree.diffCount === 0 ? "neutral" : "warn"
                text: tab.tree.diffCount === 0 ? "차이 없음" : tab.tree.diffCount + "개 바뀜"
            }
        }
        LoadingBar { Layout.fillWidth: true; running: tab.tree.messageCount === 0 }
        Text {
            visible: tab.tree.messageCount === 0
            text: "메시지를 기다리는 중 (200 ms 간격)"
            color: Theme.text2
            font.pixelSize: Theme.fsSmall
        }
        MessageTree {
            Layout.fillWidth: true
            Layout.fillHeight: true
            tree: tab.tree
        }
        Text {
            text: "숫자 행 오른쪽 클릭 → 경로 복사(사용자 그래프에 붙여 넣기)"
            color: Theme.text3
            font.pixelSize: Theme.fsCaption
        }
    }
}
