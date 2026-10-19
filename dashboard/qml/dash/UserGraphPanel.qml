import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 사용자 그래프: 메시지 경로(자동완성 = 토픽 목록 + 받은 메시지의 숫자 경로, fuzzyScore 순)로 계열을 더한다.
// 계열마다 색 · 이름 · 변환 · 켜기/끄기 · 삭제. 구성(경로 · 색 · 이름 · 변환 · 켜기 · 창)은 Settings 에 JSON 으로 둔다.
Panel {
    id: up
    required property Robot robot
    property GraphLink link: null
    property bool active: true
    title: "사용자 그래프"
    subtitle: "메시지 경로"

    readonly property var transforms: ["없음", "d/dt", "avg(5)", "int", "scale(100,0)", "@rad2deg"]
    property var objs: []            // PlotSeries
    property var fieldPaths: ({})    // 토픽 → 숫자 경로 목록(받은 메시지를 펼친 것)
    property bool loading: true

    Settings { id: store; category: "dashUserGraph"; property string json: "" }

    Component {
        id: seriesComp
        PlotSeries {
            property string basePath
            property string label: "없음"
            bridge: up.robot.bridge
            path: label === "@rad2deg" ? basePath + ".@rad2deg" : basePath
            transform: label === "없음" || label === "@rad2deg" ? "" : label
            onEnabledChanged: up.save()
            onNameChanged: up.save()
            onColorChanged: up.save()
            onLabelChanged: up.save()
        }
    }

    function nextColor() {
        const used = objs.map(o => o.color.toString())
        for (const c of Theme.series)
            if (!used.includes(Qt.color(c).toString()))
                return c
        return Theme.series[objs.length % Theme.series.length]
    }
    function addSeries(c) {
        const o = seriesComp.createObject(up, {
            basePath: c.path, label: transforms.includes(c.transform) ? c.transform : "없음",
            name: c.name || c.path.split(".").slice(-1)[0], color: c.color || nextColor(), enabled: c.enabled !== false })
        objs = objs.concat([o])
        save()
    }
    function removeSeries(o) {
        objs = objs.filter(x => x !== o)
        o.destroy()
        save()
    }
    function save() {
        if (loading)
            return
        store.json = JSON.stringify({ window: graph.windowSec, series: objs.map(o => ({
            path: o.basePath, name: o.name, color: o.color.toString(), transform: o.label, enabled: o.enabled })) })
    }
    Component.onCompleted: {
        let cfg = null
        try { cfg = JSON.parse(store.json) } catch (e) { cfg = null }
        if (!cfg || !Array.isArray(cfg.series))
            cfg = { window: 10, series: [
                { path: "/odom.twist.twist.linear.x", name: "v", transform: "없음" },
                { path: "/odom.twist.twist.angular.z", name: "ω", transform: "없음" }] }
        graph.windowSec = cfg.window !== undefined ? cfg.window : 10
        for (const c of cfg.series)
            if (c && typeof c.path === "string")
                addSeries(c)
        loading = false
    }
    Connections { target: graph.feeder; function onWindowSecChanged() { up.save() } }

    // ---- 경로 입력 · 자동완성 ----
    PlotSeries { id: probe; path: input.text.trim() }   // 문법 검사만(bridge 없음)
    function candidates(q) {
        const all = []
        for (const r of up.robot.topicRows)
            all.push(r.name)
        for (const t in fieldPaths)
            for (const p of fieldPaths[t])
                all.push(p)
        const scored = []
        for (const c of all) {
            const s = graph.feeder.fuzzyScore(q, c)
            if (s >= 0 && c !== q)
                scored.push({ text: c, score: s })
        }
        scored.sort((a, b) => b.score - a.score || a.text.length - b.text.length)
        return scored.slice(0, 8).map(x => x.text)
    }
    Connections {
        target: up.robot.bridge
        enabled: input.activeFocus
        function onMessage(topic, msg) {
            if (!(topic in up.fieldPaths))
                up.fieldPaths[topic] = graph.feeder.flattenNumericPaths(topic, msg, 8)
        }
    }
    function accept() {
        const p = input.text.trim()
        if (p === "" || probe.error !== "")
            return
        addSeries({ path: p })
        input.text = ""
        sugg.close()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp2
        TextField {
            id: input
            Layout.fillWidth: true
            implicitHeight: Theme.hSm
            placeholderText: "/토픽.필드 (예: /imu/data_raw.angular_velocity.z)"
            placeholderTextColor: Theme.text3
            color: Theme.text1
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsSmall
            leftPadding: Theme.sp2
            background: Rectangle { radius: Theme.rSm; color: Theme.surface2; border.color: input.activeFocus ? Theme.accent : Theme.borderDefault }
            property var items: []
            onTextEdited: {
                items = text.trim() === "" ? [] : up.candidates(text.trim())
                list.currentIndex = 0
                if (items.length > 0) sugg.open(); else sugg.close()
            }
            Keys.onPressed: (e) => {
                if (sugg.opened && (e.key === Qt.Key_Down || e.key === Qt.Key_Up)) {
                    list.currentIndex = Math.max(0, Math.min(items.length - 1, list.currentIndex + (e.key === Qt.Key_Down ? 1 : -1)))
                    e.accepted = true
                } else if (sugg.opened && e.key === Qt.Key_Tab) {
                    text = items[list.currentIndex]; sugg.close(); e.accepted = true
                } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    if (sugg.opened) { text = items[list.currentIndex]; sugg.close() } else up.accept()
                    e.accepted = true
                } else if (e.key === Qt.Key_Escape) {
                    sugg.close(); focus = false; e.accepted = true
                }
            }
            Popup {
                id: sugg
                y: input.height + 2
                width: input.width
                padding: Theme.sp1
                focus: false
                closePolicy: Popup.CloseOnPressOutsideParent | Popup.CloseOnEscape
                background: Rectangle { radius: Theme.rSm; color: Theme.surface3; border.color: Theme.borderDefault }
                contentItem: ListView {
                    id: list
                    implicitHeight: contentHeight
                    model: input.items
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: list.width
                        height: Theme.hXs
                        radius: Theme.rXs
                        color: index === list.currentIndex ? Theme.surface2 : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: Theme.sp1; width: parent.width - Theme.sp2
                            elide: Text.ElideMiddle
                            text: parent.modelData
                            color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsCaption
                        }
                        MouseArea { anchors.fill: parent; onClicked: { input.text = parent.modelData; sugg.close(); input.forceActiveFocus() } }
                    }
                }
            }
        }
        Btn { text: "추가"; size: "sm"; enabled: input.text.trim() !== "" && probe.error === ""; onClicked: up.accept() }
    }
    Text {
        Layout.fillWidth: true
        visible: input.text.trim() !== "" && probe.error !== ""
        wrapMode: Text.Wrap
        text: "경로 오류: " + probe.error
        color: Theme.dangerText; font.pixelSize: Theme.fsCaption
    }

    // ---- 계열 목록 ----
    Repeater {
        model: up.objs
        ColumnLayout {
            id: row
            required property var modelData
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp2
                Rectangle {
                    implicitWidth: 14; implicitHeight: 14; radius: Theme.rXs
                    color: row.modelData.color
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {   // Theme.series 안에서 다음 색으로
                            const cs = Theme.series.map(c => Qt.color(c).toString())
                            const i = cs.indexOf(row.modelData.color.toString())
                            row.modelData.color = Theme.series[(i + 1) % Theme.series.length]
                        }
                    }
                }
                TextField {
                    implicitWidth: 64
                    implicitHeight: Theme.hXs
                    padding: Theme.sp1
                    text: row.modelData.name
                    color: Theme.text1; font.pixelSize: Theme.fsSmall
                    background: Rectangle { radius: Theme.rXs; color: Theme.surface2; border.color: parent.activeFocus ? Theme.accent : "transparent" }
                    onEditingFinished: row.modelData.name = text
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: row.modelData.basePath
                    color: Theme.text2; font.family: Theme.monoFamily; font.pixelSize: Theme.fsCaption
                }
                ComboBox {
                    id: tf
                    implicitWidth: 104
                    implicitHeight: Theme.hXs
                    focusPolicy: Qt.NoFocus
                    model: up.transforms
                    currentIndex: Math.max(0, up.transforms.indexOf(row.modelData.label))
                    onActivated: (i) => row.modelData.label = up.transforms[i]
                    font.pixelSize: Theme.fsCaption
                    contentItem: Text {
                        leftPadding: Theme.sp2
                        text: tf.displayText; font: tf.font; color: Theme.text1
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: Theme.rXs; color: Theme.surface2; border.color: Theme.borderDefault }
                    popup.background: Rectangle { radius: Theme.rSm; color: Theme.surface3; border.color: Theme.borderDefault }
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: tf.width
                        implicitHeight: Theme.hXs
                        highlighted: tf.highlightedIndex === index
                        contentItem: Text { text: modelData; color: Theme.text1; font: tf.font; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: parent.highlighted ? Theme.surface2 : "transparent" }
                    }
                }
                IconBtn {
                    implicitWidth: Theme.hXs; implicitHeight: Theme.hXs
                    icon.source: row.modelData.enabled ? "qrc:/icons/eye.svg" : "qrc:/icons/eye-off.svg"
                    tip: row.modelData.enabled ? "끄기" : "켜기"
                    onClicked: row.modelData.enabled = !row.modelData.enabled
                }
                Btn {
                    size: "sm"; kind: "ghost"
                    implicitHeight: Theme.hXs
                    leftPadding: Theme.sp1; rightPadding: Theme.sp1
                    text: "삭제"
                    onClicked: up.removeSeries(row.modelData)
                }
            }
            Text {
                Layout.fillWidth: true
                visible: row.modelData.error !== ""
                wrapMode: Text.Wrap
                text: row.modelData.error
                color: Theme.dangerText; font.pixelSize: Theme.fsCaption
            }
        }
    }

    TimeGraph {
        id: graph
        objectName: "userGraph"
        Layout.fillWidth: true
        name: "user_graph"
        robot: up.robot
        link: up.link
        active: up.active
        series: up.objs
        emptyText: up.objs.length === 0 ? "경로를 더하면 그려집니다" : "데이터를 기다리는 중"
    }
}
