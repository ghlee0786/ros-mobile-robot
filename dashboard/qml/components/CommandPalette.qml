import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// Ctrl+K 명령 팔레트. 모델은 [{label, hint, keys, run}] JS 배열, 퍼지 필터 · ↑↓ · Enter · Esc.
// 열려 있을 때만 키보드를 가져간다(modal + focus) — 닫혀 있으면 조종 키에 끼어들지 않는다.
Popup {
    id: pal
    objectName: "palette"
    required property Robot robot
    property Updater updater
    property var pageNames: ["대시보드", "데이터 점검", "연결"]
    signal pageRequested(int index)

    property var items: []
    property var shown: []

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: 560
    padding: Theme.sp2
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        onActivated: pal.opened ? pal.close() : pal.open()
    }

    function commands() {
        const r = robot, out = []
        pageNames.forEach((n, i) => out.push({ label: n + " 화면으로", hint: "페이지", keys: "", run: () => pal.pageRequested(i) }))
        r.sensors.forEach(s => out.push({ label: (s.on ? "센서 끄기 · " : "센서 켜기 · ") + s.label, hint: s.topic, keys: "",
                                          run: () => r.setSensor(s.key, !s.on) }))
        out.push({ label: r.mapWanted ? "지도 숨기기" : "지도 보기", hint: "/map", keys: "", run: () => r.mapWanted = !r.mapWanted })
        out.push({ label: "연결 시험", hint: "센서 수신 확인", keys: "", run: () => r.probe() })
        out.push({ label: r.recording ? "기록 중지" : "기록 시작", hint: "rosbag", keys: "", run: () => r.toggleRecording() })
        out.push({ label: "기록 폴더 열기", hint: "", keys: "", run: () => r.openRecords() })
        out.push({ label: "경로 지우기", hint: "3D 이동 경로", keys: "", run: () => r.clearTrail() })
        // 대시보드의 직진 버튼과 같은 조건(DashboardPage.canStraight). 잠금은 Robot::straight 가 지킨다.
        if (r.bridge.connected && !r.linkLost && !r.straightRunning && r.mode === "키보드 조종") {
            out.push({ label: "1 m 직진 시험", hint: "주행", keys: "", run: () => r.straight(1.0) })
            out.push({ label: "1 m 후진", hint: "주행", keys: "", run: () => r.straight(-1.0) })
        }
        out.push({ label: "비상 정지", hint: "주행", keys: "", run: () => r.estop() })
        out.push({ label: Theme.largeText ? "큰 글자 끄기" : "큰 글자 켜기", hint: "보기", keys: "", run: () => Theme.largeText = !Theme.largeText })
        out.push({ label: Theme.reduceMotion ? "움직임 줄이기 끄기" : "움직임 줄이기", hint: "보기", keys: "", run: () => Theme.reduceMotion = !Theme.reduceMotion })
        out.push({ label: Theme.dark ? "라이트 테마로 전환" : "다크 테마로 전환", hint: "보기", keys: "", run: () => Theme.toggleDark() })
        if (updater) {
            const u = updater
            out.push({ label: "업데이트 확인", hint: "현재 v" + u.currentVersion, keys: "", run: () => u.check(true) })
            out.push({ label: u.checkEnabled ? "시작할 때 업데이트 확인 끄기" : "시작할 때 업데이트 확인 켜기", hint: "업데이트", keys: "",
                       run: () => u.checkEnabled = !u.checkEnabled })
        }
        out.push({ label: "정보 · ROS 모니터 v" + Qt.application.version, hint: "버전", keys: "", run: () => {} })
        return out
    }
    // 글자가 순서대로 다 들어 있으면 맞는 것으로 보고, 붙어 있을수록 · 앞쪽일수록 점수를 준다.
    function score(text, q) {
        text = text.toLowerCase()
        let pos = -1, s = 0
        for (const ch of q) {
            if (ch === " ") continue
            const i = text.indexOf(ch, pos + 1)
            if (i < 0) return -1
            s += i === pos + 1 ? 3 : 1
            pos = i
        }
        return s - pos * 0.01
    }
    function refilter() {
        const q = field.text.trim().toLowerCase()
        shown = q === "" ? items
              : items.map(c => ({ c, s: score(c.label + " " + c.hint, q) })).filter(e => e.s >= 0)
                     .sort((a, b) => b.s - a.s).map(e => e.c)
        list.currentIndex = shown.length > 0 ? 0 : -1
    }
    function runCurrent() {
        const c = shown[list.currentIndex]
        if (!c) return
        close()
        c.run()
    }

    onAboutToShow: { items = commands(); field.text = ""; refilter() }
    onOpened: field.forceActiveFocus()

    background: Rectangle {
        radius: Theme.rXl
        color: Theme.surface2
        border.color: Theme.borderDefault
    }
    Overlay.modal: Rectangle { color: Theme.shadow; opacity: 0.5 }

    contentItem: Column {
        spacing: Theme.sp1
        TextField {
            id: field
            objectName: "paletteField"
            width: parent.width
            implicitHeight: Theme.hLg
            placeholderText: "명령 찾기…"
            placeholderTextColor: Theme.text3
            color: Theme.text1
            font.pixelSize: Theme.fsLabel
            leftPadding: Theme.sp3
            background: Rectangle { radius: Theme.rXl - Theme.sp2; color: "transparent" }
            onTextChanged: pal.refilter()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onReturnPressed: pal.runCurrent()
            Keys.onEnterPressed: pal.runCurrent()
        }
        Rectangle { width: parent.width; height: 1; color: Theme.borderDefault }
        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight, Theme.hMd * 9)
            clip: true
            model: pal.shown
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: Theme.hMd
                hoverEnabled: true
                onClicked: { list.currentIndex = index; pal.runCurrent() }
                contentItem: Item {
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: Theme.text1
                        font.pixelSize: Theme.fsBody
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.sp2
                        Text { text: row.modelData.hint; color: Theme.text2; font.pixelSize: Theme.fsSmall; anchors.verticalCenter: parent.verticalCenter }
                        Kbd { visible: row.modelData.keys !== ""; text: row.modelData.keys; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                background: Rectangle {
                    radius: Theme.rSm
                    color: row.ListView.isCurrentItem ? Theme.surface3 : row.hovered ? Theme.surface3 : "transparent"
                    opacity: row.ListView.isCurrentItem ? 1 : 0.6
                }
            }
        }
        Text {
            visible: pal.shown.length === 0
            width: parent.width
            topPadding: Theme.sp2
            bottomPadding: Theme.sp2
            horizontalAlignment: Text.AlignHCenter
            text: "맞는 명령이 없습니다"
            color: Theme.text2
            font.pixelSize: Theme.fsSmall
        }
    }
}
