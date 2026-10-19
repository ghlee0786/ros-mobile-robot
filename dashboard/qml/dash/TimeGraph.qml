import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs
import RobotDashboard

// 시간 그래프: series(PlotSeries 목록)를 PlotFeeder 로 GraphsView 에 그린다.
// 창 10/30/60 s/전체(버퍼는 120 s 따로), 자동 Y(PlotFeeder: 넓힐 때 즉시 · 좁힐 때 천천히) 또는 고정 Y.
// ⏸ 는 화면만 멈춘다(수신 · 버퍼 기록은 계속). 멈춘 동안에만 휠 확대 · 끌기 확대 · 이동 · 트래커를 켠다.
// link(페이지 전역 GraphLink)가 linked 이면 X 확대 · 이동 · 트래커 · 재개를 다른 그래프와 같이 한다.
ColumnLayout {
    id: g
    property var series: []
    property GraphLink link: null
    property string name: "graph"        // 저장 파일 이름 앞부분
    property string emptyText: "데이터를 기다리는 중"
    property real graphHeight: 130
    property bool active: true
    property alias windowSec: feeder.windowSec
    property alias feeder: feeder
    property bool fixedY: false          // 사용자가 고른 고정 Y
    property real fixedMin: -1
    property real fixedMax: 1
    property real nowRef: 0              // X 눈금 기준(지금, 멈추면 멈춘 시각)
    property real trackT: NaN
    property var history: []             // 확대 이력 [{x0, x1, y0, y1}]
    property bool showStats: false
    property string savedText
    readonly property bool paused: feeder.paused
    readonly property bool hasData: series.some(s => s.count > 0)

    spacing: Theme.sp1

    PlotFeeder {
        id: feeder
        active: g.active && g.visible
        pixelWidth: Math.max(2, Math.round(view.width))
        onRefreshed: if (!paused) g.nowRef = xMax
        onPausedChanged: if (!paused) { g.history = []; g.trackT = NaN; clearReference(); g.applyY() }
    }

    Component {
        id: lineComp
        LineSeries {
            property PlotSeries src
            color: src ? src.color : Theme.text1
            width: src && src.steps ? 1.4 : 1.8
            visible: !!src && src.enabled
        }
    }

    // GraphsView(Qt 6.8)는 removeSeries 한 선을 다음 polish 때까지 날 포인터로 들고 있어, 바로 destroy 하면
    // polish 에서 죽는다. 선은 지우지 않고 다시 쓰고, 남는 선은 src 를 비워 숨긴다.
    property var lines: []
    function rebuild() {
        while (lines.length < series.length) {
            const l = lineComp.createObject(g)
            view.addSeries(l)
            lines.push(l)
        }
        lines.forEach((l, i) => { l.src = i < series.length ? series[i] : null; if (!l.src) l.clear() })
        feeder.series = series
        feeder.targets = lines.slice(0, series.length)
        feeder.refresh()
    }
    onSeriesChanged: rebuild()
    Component.onCompleted: rebuild()

    function applyY() {
        if (fixedY) {
            feeder.autoY = false
            feeder.yMin = fixedMin
            feeder.yMax = fixedMax
        } else {
            feeder.zoomOut(false, true)
        }
    }

    // ---- 좌표 변환 (view 안의 픽셀 ↔ 데이터) ----
    function tAt(px) { return feeder.xMin + px / view.width * (feeder.xMax - feeder.xMin) }
    function vAt(py) { return feeder.yMaxOut - py / view.height * (feeder.yMaxOut - feeder.yMinOut) }
    function pxOf(t) { return (t - feeder.xMin) / (feeder.xMax - feeder.xMin) * view.width }
    function pyOf(v) { return (feeder.yMaxOut - v) / (feeder.yMaxOut - feeder.yMinOut) * view.height }

    function niceStep(span, n) {
        const raw = Math.max(span, 1e-9) / n
        const p = Math.pow(10, Math.floor(Math.log10(raw)))
        const m = raw / p
        return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p
    }
    function fmt(v, step) {
        const d = Math.max(0, Math.min(4, -Math.floor(Math.log10(step) + 1e-9)))
        return v.toFixed(d)
    }

    // ---- 조작 ----
    function pushHistory() {
        const h = history.slice()
        h.push({ x0: feeder.xMin, x1: feeder.xMax, y0: feeder.yMinOut, y1: feeder.yMaxOut })
        history = h
    }
    function popHistory() {
        if (history.length === 0)
            return
        const h = history.slice(), z = h.pop()
        history = h
        zoom(z.x0, z.x1, z.y0, z.y1, false)
    }
    // X 를 바꾸면 링크된 그래프에도 알린다. y0 · y1 이 NaN 이면 Y 는 그대로(자동이면 자동).
    function zoom(x0, x1, y0, y1, record) {
        if (!(x1 - x0 > 1e-6))
            return
        if (record)
            pushHistory()
        feeder.zoomTo(x0, x1, y0, y1)
        if (link && link.linked)
            link.zoomed(g, x0, x1)
    }
    function fullView() {
        let first = feeder.xMax
        for (const v of feeder.valuesAt(-1e12))
            first = Math.min(first, v.t)
        history = []
        zoom(Math.min(first, nowRef - 1), nowRef, NaN, NaN, false)
        applyY()
    }
    function setPaused(p) {
        feeder.paused = p
        if (link && link.linked)
            link.pauseSet(g, p)
    }
    function setTrack(t) {
        trackT = t
        if (link && link.linked)
            link.tracked(g, t)
    }
    function saveCsv() {
        const path = robot.recordsDir() + "/" + name + "_" + Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss") + ".csv"
        savedText = feeder.exportCsv(path, true) ? "저장함: " + path : "저장 실패: " + path
        savedTimer.restart()
    }
    function savePng() {
        const path = robot.recordsDir() + "/" + name + "_" + Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss") + ".png"
        plotBox.grabToImage(r => {
            g.savedText = r.saveToFile(path) ? "저장함: " + path : "저장 실패: " + path
            savedTimer.restart()
        })
    }
    property Robot robot: null

    Timer { id: savedTimer; interval: 5000; onTriggered: g.savedText = "" }

    Connections {
        target: g.link
        enabled: !!g.link
        function onZoomed(src, x0, x1) {
            if (src !== g && g.link.linked)
                feeder.zoomTo(x0, x1, NaN, NaN)
        }
        function onTracked(src, t) {
            if (src !== g && g.link.linked)
                g.trackT = t
        }
        function onPauseSet(src, p) {
            if (src !== g && g.link.linked)
                feeder.paused = p
        }
    }

    // ---- 머리: 창 · Y · ⏸ · 메뉴 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp1
        Seg {
            options: ["10 s", "30 s", "60 s", "전체"]
            current: feeder.windowSec > 0 ? feeder.windowSec + " s" : "전체"
            onPicked: (v) => feeder.windowSec = v === "전체" ? 0 : parseInt(v)
        }
        Item { Layout.fillWidth: true }
        Btn {
            size: "sm"
            kind: "ghost"
            leftPadding: Theme.sp2; rightPadding: Theme.sp2
            text: g.fixedY ? "Y 고정" : "Y 자동"
            onClicked: {
                if (!g.fixedY) {
                    g.fixedMin = Number(feeder.yMinOut.toPrecision(3))
                    g.fixedMax = Number(feeder.yMaxOut.toPrecision(3))
                }
                g.fixedY = !g.fixedY
                g.applyY()
            }
        }
        IconBtn {
            objectName: "pause"
            icon.source: g.paused ? "qrc:/icons/play.svg" : "qrc:/icons/pause.svg"
            tip: g.paused ? "다시 흐르게 (조작은 멈춘 동안만)" : "일시정지 — 멈춘 동안 확대 · 이동 · 트래커"
            onClicked: g.setPaused(!g.paused)
        }
        IconBtn {
            icon.source: "qrc:/icons/chevron-down.svg"
            tip: "메뉴"
            onClicked: menu.popup()
            Menu {
                id: menu
                MenuItem { text: "보이는 데이터 CSV 저장"; onTriggered: g.saveCsv() }
                MenuItem { text: "PNG 저장"; onTriggered: g.savePng() }
                MenuSeparator {}
                MenuItem { text: "전체 보기"; enabled: g.paused; onTriggered: g.fullView() }
                MenuItem { text: "확대 되돌리기"; enabled: g.history.length > 0; onTriggered: g.popHistory() }
                MenuItem {
                    text: feeder.hasReference ? "기준 해제" : "트래커 위치를 기준으로 고정"
                    enabled: feeder.hasReference || !isNaN(g.trackT)
                    onTriggered: feeder.hasReference ? feeder.clearReference() : feeder.setReference(g.trackT)
                }
                MenuItem { text: "트래커 지우기"; enabled: !isNaN(g.trackT); onTriggered: g.setTrack(NaN) }
            }
        }
    }

    // ---- 고정 Y 입력 ----
    RowLayout {
        Layout.fillWidth: true
        visible: g.fixedY
        spacing: Theme.sp2
        component Num: TextField {
            property real val
            signal edited(real v)
            implicitWidth: 72
            implicitHeight: Theme.hXs
            padding: Theme.sp1
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsSmall
            color: Theme.text1
            text: String(val)
            validator: DoubleValidator {}
            background: Rectangle { radius: Theme.rXs; color: Theme.surface2; border.color: parent.activeFocus ? Theme.accent : Theme.borderDefault }
            onEditingFinished: { const v = Number(text); if (isFinite(v)) edited(v) }
        }
        Text { text: "Y 최소"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        Num { val: g.fixedMin; onEdited: (v) => { g.fixedMin = v; g.applyY() } }
        Text { text: "최대"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        Num { val: g.fixedMax; onEdited: (v) => { g.fixedMax = v; g.applyY() } }
        Item { Layout.fillWidth: true }
    }

    // ---- 그래프 ----
    Item {
        id: plotBox
        Layout.fillWidth: true
        Layout.preferredHeight: g.graphHeight
        readonly property int yLabelW: 34
        readonly property int xLabelH: 16

        readonly property real yStep: g.niceStep(feeder.yMaxOut - feeder.yMinOut, 3)
        readonly property var yTicks: {
            const out = [], s = yStep
            for (let v = Math.ceil(feeder.yMinOut / s) * s; v <= feeder.yMaxOut + 1e-9 && out.length < 12; v += s)
                out.push(v)
            return out
        }
        readonly property real xStep: g.niceStep(feeder.xMax - feeder.xMin, 4)
        readonly property var xTicks: {
            const out = [], s = xStep
            for (let k = Math.ceil((feeder.xMin - g.nowRef) / s - 1e-9); g.nowRef + k * s <= feeder.xMax + 1e-9 && out.length < 12; ++k)
                out.push(g.nowRef + k * s)
            return out
        }

        Rectangle { anchors.fill: view; color: Theme.surface2; opacity: 0.4; radius: Theme.rXs }
        // 격자 · 눈금
        Repeater {
            model: plotBox.yTicks
            Item {
                required property real modelData
                readonly property real py: g.pyOf(modelData)
                Rectangle { x: view.x; y: view.y + parent.py; width: view.width; height: 1; color: Theme.borderDefault }
                Text {
                    x: 0; width: plotBox.yLabelW - Theme.sp1; y: view.y + parent.py - height / 2
                    horizontalAlignment: Text.AlignRight
                    text: g.fmt(parent.modelData, plotBox.yStep)
                    color: Theme.text3; font.pixelSize: Theme.fsCaption; font.features: Theme.numFeatures
                }
            }
        }
        Repeater {
            model: plotBox.xTicks
            Item {
                required property real modelData
                readonly property real px: g.pxOf(modelData)
                Rectangle { x: view.x + parent.px; y: view.y; width: 1; height: view.height; color: Theme.borderSubtle }
                Text {
                    x: view.x + parent.px - width / 2; y: view.y + view.height + 1
                    readonly property real rel: parent.modelData - g.nowRef
                    text: Math.abs(rel) < 1e-6 ? "0 s" : g.fmt(rel, plotBox.xStep)
                    color: Theme.text3; font.pixelSize: Theme.fsCaption; font.features: Theme.numFeatures
                }
            }
        }

        GraphsView {
            id: view
            x: plotBox.yLabelW
            y: 0
            width: plotBox.width - plotBox.yLabelW
            height: plotBox.height - plotBox.xLabelH
            marginLeft: 0; marginRight: 0; marginTop: 0; marginBottom: 0
            theme: GraphsTheme {
                backgroundVisible: false
                plotAreaBackgroundVisible: false
                gridVisible: false
            }
            axisX: ValueAxis { min: feeder.xMin; max: feeder.xMax; visible: false; lineVisible: false; gridVisible: false; subGridVisible: false; labelsVisible: false }
            axisY: ValueAxis { min: feeder.yMinOut; max: feeder.yMaxOut; visible: false; lineVisible: false; gridVisible: false; subGridVisible: false; labelsVisible: false }
        }

        // 트래커 · 기준선
        Rectangle {
            visible: feeder.hasReference && feeder.reference >= feeder.xMin && feeder.reference <= feeder.xMax
            x: view.x + g.pxOf(feeder.reference); y: view.y; width: 1; height: view.height
            color: Theme.text3
        }
        Rectangle {
            id: trackLine
            visible: !isNaN(g.trackT) && g.trackT >= feeder.xMin && g.trackT <= feeder.xMax
            x: view.x + g.pxOf(g.trackT); y: view.y; width: 1; height: view.height
            color: Theme.text1
        }
        Rectangle {
            id: tip
            visible: trackLine.visible
            readonly property var rows: { feeder.xMin; feeder.yMinOut; feeder.reference; return isNaN(g.trackT) ? [] : feeder.valuesAt(g.trackT) }
            x: Math.min(trackLine.x + Theme.sp2, plotBox.width - width)
            y: view.y + Theme.sp1
            width: tipCol.implicitWidth + 2 * Theme.sp2
            height: tipCol.implicitHeight + 2 * Theme.sp1
            radius: Theme.rSm
            color: Theme.surface3
            border.color: Theme.borderDefault
            Column {
                id: tipCol
                x: Theme.sp2; y: Theme.sp1
                Text {
                    text: (g.trackT - g.nowRef).toFixed(2) + " s"
                          + (feeder.hasReference ? "  ·  Δt " + (g.trackT - feeder.reference).toFixed(3) + " s" : "")
                    color: Theme.text2; font.pixelSize: Theme.fsCaption; font.features: Theme.numFeatures
                }
                Repeater {
                    model: tip.rows
                    Text {
                        required property var modelData
                        textFormat: Text.StyledText
                        text: "<font color='" + modelData.color + "'>■</font> " + modelData.name + "  <b>" + modelData.value.toFixed(3) + "</b>"
                              + (modelData.delta !== undefined ? "  Δ " + modelData.delta.toFixed(3) : "")
                        color: Theme.text1; font.pixelSize: Theme.fsCaption; font.features: Theme.numFeatures
                    }
                }
            }
        }
        // 끌기 사각형
        Rectangle {
            id: band
            visible: false
            color: Theme.accentSubtle
            opacity: 0.6
            border.color: Theme.accent
        }

        MouseArea {
            id: mouse
            anchors.fill: view
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            property string drag: ""     // band · pan
            property point start
            property point last
            onPressed: (m) => {
                if (!g.paused) { m.accepted = false; return }
                start = last = Qt.point(m.x, m.y)
                if (m.button === Qt.RightButton) { g.popHistory(); return }
                if (m.button === Qt.LeftButton && (m.modifiers & Qt.ShiftModifier)) { g.setTrack(g.tAt(m.x)); return }
                if (m.button === Qt.MiddleButton || (m.modifiers & Qt.ControlModifier)) { g.pushHistory(); drag = "pan"; return }
                drag = "band"
            }
            onPositionChanged: (m) => {
                if (drag === "pan") {
                    const dx = -(m.x - last.x) / view.width * (feeder.xMax - feeder.xMin)
                    const dy = (m.y - last.y) / view.height * (feeder.yMaxOut - feeder.yMinOut)
                    feeder.pan(dx, dy)
                    if (g.link && g.link.linked)
                        g.link.zoomed(g, feeder.xMin, feeder.xMax)
                    last = Qt.point(m.x, m.y)
                } else if (drag === "band") {
                    band.x = view.x + Math.min(start.x, m.x); band.y = view.y + Math.min(start.y, m.y)
                    band.width = Math.abs(m.x - start.x); band.height = Math.abs(m.y - start.y)
                    band.visible = true
                }
            }
            onReleased: (m) => {
                if (drag === "band" && band.width >= 8 && band.height >= 8) {   // 너무 작으면 무시
                    const x0 = g.tAt(band.x - view.x), x1 = g.tAt(band.x - view.x + band.width)
                    const y1 = g.vAt(band.y - view.y), y0 = g.vAt(band.y - view.y + band.height)
                    g.zoom(x0, x1, y0, y1, true)
                }
                band.visible = false
                drag = ""
            }
            onDoubleClicked: (m) => { if (g.paused && m.button === Qt.LeftButton) g.fullView() }
            onWheel: (w) => {
                if (!g.paused) { w.accepted = false; return }
                const d = w.angleDelta.y !== 0 ? w.angleDelta.y : w.angleDelta.x
                if (d === 0) return
                const f = d > 0 ? 0.8 : 1.25
                const onlyX = w.modifiers & Qt.ShiftModifier, onlyY = w.modifiers & Qt.AltModifier
                const tc = g.tAt(w.x), vc = g.vAt(w.y)
                let x0 = feeder.xMin, x1 = feeder.xMax, y0 = NaN, y1 = NaN
                if (!onlyY) { x0 = tc - (tc - feeder.xMin) * f; x1 = tc + (feeder.xMax - tc) * f }
                if (!onlyX) { y0 = vc - (vc - feeder.yMinOut) * f; y1 = vc + (feeder.yMaxOut - vc) * f }
                g.zoom(x0, x1, y0, y1, true)
            }
        }

        EmptyState {
            anchors.centerIn: view
            visible: !g.hasData
            title: g.emptyText
            description: "첫 값이 오면 그려집니다"
        }
        StatusChip {
            anchors.right: view.right; anchors.top: view.top; anchors.margins: Theme.sp1
            visible: g.paused
            kind: "warn"
            text: "멈춤"
        }
    }
    Text {
        Layout.fillWidth: true
        visible: g.paused
        text: "휠 확대(Shift X · Alt Y) · 끌기 확대 · Ctrl/가운데 끌기 이동 · Shift+클릭 트래커 · 오른쪽 클릭 되돌리기 · 더블클릭 전체 보기"
        wrapMode: Text.WordWrap
        color: Theme.text3; font.pixelSize: Theme.fsCaption
    }

    // ---- 범례 (클릭하면 숨김 · 자동 Y 에서도 뺀다) · 통계 토글 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp2
        Flow {
            Layout.fillWidth: true
            spacing: Theme.sp3
            Repeater {
                model: g.series
                Item {
                    id: lg
                    required property var modelData
                    implicitWidth: lgRow.implicitWidth
                    implicitHeight: Theme.hXs
                    opacity: modelData.enabled ? 1 : Theme.opDisabled
                    Row {
                        id: lgRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.sp1
                        Rectangle { width: 10; height: lg.modelData.steps ? 2 : 3; anchors.verticalCenter: parent.verticalCenter; color: lg.modelData.color }
                        Text {
                            text: lg.modelData.name + (lg.modelData.count > 0 ? " " + lg.modelData.lastValue.toFixed(2) : "")
                            color: Theme.text2; font.pixelSize: Theme.fsSmall; font.features: Theme.numFeatures
                            font.strikeout: !lg.modelData.enabled
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: lg.modelData.enabled = !lg.modelData.enabled
                    }
                }
            }
        }
        Btn {
            size: "sm"
            kind: g.showStats ? "normal" : "ghost"
            leftPadding: Theme.sp2; rightPadding: Theme.sp2
            text: "통계"
            onClicked: g.showStats = !g.showStats
        }
    }

    Text {
        visible: g.savedText !== ""
        Layout.fillWidth: true
        elide: Text.ElideMiddle
        text: g.savedText
        color: Theme.text2; font.pixelSize: Theme.fsCaption
    }

    // ---- 보이는 구간 통계 ----
    GridLayout {
        id: stats
        visible: g.showStats
        Layout.fillWidth: true
        columns: 8
        columnSpacing: Theme.sp2
        rowSpacing: 2
        property var rows: []
        Timer {
            interval: 500; repeat: true; triggeredOnStart: true
            running: g.showStats && g.visible
            onTriggered: {
                const all = feeder.statsInView(), out = []
                for (let i = 0; i < all.length && i < g.series.length; ++i)
                    if (g.series[i].enabled)
                        out.push(all[i])
                stats.rows = out
            }
        }
        Repeater {
            model: ["", "count", "min", "max", "mean", "rms", "std", "hz"]
            Text { required property string modelData; text: modelData; color: Theme.text3; font.pixelSize: Theme.fsCaption }
        }
        Repeater {
            model: stats.rows.length * 8
            Text {
                required property int index
                readonly property var r: stats.rows[Math.floor(index / 8)]
                readonly property string key: ["name", "count", "min", "max", "mean", "rms", "std", "hz"][index % 8]
                Layout.fillWidth: index % 8 === 0
                elide: Text.ElideRight
                text: !r ? "" : key === "name" ? r.name : key === "count" ? String(r.count) : Number(r[key]).toFixed(key === "hz" ? 1 : 3)
                color: index % 8 === 0 ? Theme.text2 : Theme.text1
                font.pixelSize: Theme.fsCaption
                font.family: index % 8 === 0 ? Theme.fontFamily : Theme.monoFamily
            }
        }
    }
}
