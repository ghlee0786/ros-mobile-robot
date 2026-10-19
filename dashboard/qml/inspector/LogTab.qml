import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 로그 탭: /rosout 링 버퍼(LogModel)와 필터(LogFilterModel).
// 최소 심각도(AND) · 검색어 칩(노드 이름 · 메시지 소문자 포함, 여러 개면 OR) · 노드 체크 · 강조/제외 규칙 · 일시정지 · TSV 복사.
RowLayout {
    id: tab
    required property LogModel log
    required property LogFilterModel filter
    readonly property var levels: ["DEBUG", "INFO", "WARN", "ERROR", "FATAL"]
    property var selected: []   // 필터 모델의 행 번호
    spacing: Theme.sp4

    function levelColor(level) {
        return level >= 40 ? Theme.dangerText : level >= 30 ? Theme.warnText : level <= 10 ? Theme.text2 : Theme.text1
    }
    function toggleSelect(row, add) {
        const s = add ? tab.selected.slice() : []
        const i = s.indexOf(row)
        i >= 0 ? s.splice(i, 1) : s.push(row)
        tab.selected = s
    }
    Connections { target: tab.filter; function onFilterChanged() { tab.selected = [] } }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: tab.visible && tab.selected.length > 0
        onActivated: tab.filter.copyTsv(tab.selected)
    }

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: "로그"
        subtitle: tab.filter.count + " / " + tab.log.count + "줄" + (tab.log.paused ? " · 일시정지" : "")
        headerExtra: [
            Btn { size: "sm"; text: tab.log.paused ? "이어서" : "일시정지"; onClicked: tab.log.paused = !tab.log.paused },
            Btn { size: "sm"; text: "선택 복사"; enabled: tab.selected.length > 0; onClicked: tab.filter.copyTsv(tab.selected) },
            Btn { size: "sm"; kind: "ghost"; text: "지우기"; onClicked: { tab.selected = []; tab.log.clear() } }
        ]

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp2
            Seg {
                options: tab.levels
                current: tab.levels[Math.max(0, Math.min(4, tab.filter.minLevel / 10 - 1))]
                onPicked: v => tab.filter.minLevel = (tab.levels.indexOf(v) + 1) * 10
            }
            InField {
                id: termInput
                Layout.preferredWidth: 220
                placeholderText: "검색어 + Enter"
                onAccepted: {
                    const t = text.trim().toLowerCase()
                    if (t !== "" && tab.filter.terms.indexOf(t) < 0)
                        tab.filter.terms = tab.filter.terms.concat([t])
                    text = ""
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.sp1
                Repeater {
                    model: tab.filter.terms
                    Btn {
                        required property string modelData
                        size: "sm"
                        text: modelData + "  ×"
                        onClicked: tab.filter.terms = tab.filter.terms.filter(t => t !== modelData)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: "시각"; Layout.preferredWidth: 96; color: Theme.text2; font.pixelSize: Theme.fsSmall; leftPadding: Theme.sp2 }
            Text { text: "심각도"; Layout.preferredWidth: 64; color: Theme.text2; font.pixelSize: Theme.fsSmall }
            Text { text: "노드"; Layout.preferredWidth: 160; color: Theme.text2; font.pixelSize: Theme.fsSmall }
            Text { text: "메시지"; Layout.fillWidth: true; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tab.filter
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // 맨 아래에 있을 때만 새 줄을 따라간다
            property bool follow: true
            onMovementEnded: follow = atYEnd
            onCountChanged: if (follow && !tab.log.paused) positionViewAtEnd()
            delegate: Rectangle {
                id: logRow
                required property int index
                required property string timeText
                required property int level
                required property string levelName
                required property string name
                required property string msg
                required property string file
                required property int line
                required property bool highlight
                readonly property bool sel: tab.selected.indexOf(index) >= 0
                width: ListView.view.width
                height: 24
                color: sel ? Theme.surface3 : highlight ? Theme.accentSubtle : "transparent"
                TapHandler {
                    onTapped: tab.toggleSelect(logRow.index, (point.modifiers & Qt.ControlModifier) !== 0)
                }
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    component Cell: Text {
                        Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fsSmall
                        font.features: Theme.numFeatures
                    }
                    Cell { text: logRow.timeText; Layout.preferredWidth: 96; leftPadding: Theme.sp2; color: Theme.text2 }
                    Cell { text: logRow.levelName; Layout.preferredWidth: 64; color: tab.levelColor(logRow.level) }
                    Cell { text: logRow.name; Layout.preferredWidth: 160; color: Theme.text2 }
                    Cell {
                        text: logRow.msg
                        Layout.fillWidth: true
                        color: tab.levelColor(logRow.level)
                        ToolTip.visible: tipHover.hovered && logRow.file !== ""
                        ToolTip.delay: 600
                        ToolTip.text: logRow.file + ":" + logRow.line
                        HoverHandler { id: tipHover }
                    }
                }
            }
        }
        EmptyState {
            Layout.alignment: Qt.AlignHCenter
            visible: tab.log.count === 0
            title: "로그가 없음"
            description: "/rosout 을 이 탭이 열려 있는 동안만 구독한다"
        }
    }

    ColumnLayout {
        Layout.preferredWidth: 340
        Layout.fillWidth: false
        Layout.fillHeight: true
        spacing: Theme.sp3

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "노드"
            subtitle: tab.log.nodes.length + "개"
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 160
                clip: true
                model: tab.log.nodes
                delegate: RowLayout {
                    required property string modelData
                    width: ListView.view.width
                    InCheck {
                        checked: tab.filter.hiddenNodes.indexOf(modelData) < 0
                        onToggled: tab.filter.hiddenNodes = checked
                                   ? tab.filter.hiddenNodes.filter(n => n !== modelData)
                                   : tab.filter.hiddenNodes.concat([modelData])
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData; elide: Text.ElideRight
                        color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsSmall
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: "강조 · 제외 규칙"
            Repeater {
                model: tab.filter.rules
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    StatusChip {
                        kind: modelData.kind === "exclude" ? "neutral" : "warn"
                        text: modelData.kind === "exclude" ? "제외" : "강조"
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: "/" + modelData.pattern + "/" + (modelData.node ? " · " + modelData.node : "")
                              + (modelData.minLevel > 0 ? " · " + tab.levels[modelData.minLevel / 10 - 1] + "↑" : "")
                        color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsSmall
                    }
                    Btn {
                        size: "sm"; kind: "ghost"; text: "×"
                        onClicked: { const r = tab.filter.rules.slice(); r.splice(index, 1); tab.filter.rules = r }
                    }
                }
            }
            Seg { id: ruleKind; options: ["강조", "제외"]; current: "강조"; onPicked: v => current = v }
            InField { id: rulePattern; Layout.fillWidth: true; placeholderText: "정규식"; mono: true }
            InField { id: ruleNode; Layout.fillWidth: true; placeholderText: "노드 이름(비우면 모두)"; mono: true }
            Seg { id: ruleLevel; options: ["모두"].concat(tab.levels.slice(1)); current: "모두"; onPicked: v => current = v }
            Btn {
                text: "규칙 더하기"
                size: "sm"
                enabled: rulePattern.text !== ""
                onClicked: {
                    tab.filter.rules = tab.filter.rules.concat([{
                        kind: ruleKind.current === "제외" ? "exclude" : "highlight",
                        pattern: rulePattern.text, node: ruleNode.text.trim(),
                        minLevel: Math.max(0, tab.levels.indexOf(ruleLevel.current) + 1) * 10 }])
                    rulePattern.text = ""
                }
            }
        }
    }
}
