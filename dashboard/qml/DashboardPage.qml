import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

FocusScope {
    id: page
    objectName: "dash"
    required property Robot robot
    property bool active: true
    readonly property string mono: Theme.monoFamily
    focus: true
    // 페이지를 떠나면 누르고 있던 키를 놓은 것으로 본다(StackLayout 은 포커스를 그대로 둔다)
    onActiveChanged: if (active) forceActiveFocus(); else { held = {}; robot.drive(0, 0) }
    Component.onCompleted: forceActiveFocus()
    // 직진 · 기록 버튼(팔레트도 같은 조건): 연결이 정상이고, 직진은 키보드 조종 모드에서 진행 중이 아닐 때
    readonly property bool linkOk: robot.bridge.connected && !robot.linkLost
    readonly property bool canStraight: linkOk && !robot.straightRunning && robot.mode === "키보드 조종"
    readonly property bool canRecord: robot.recording || robot.bridge.connected

    // ---- 키보드 조종: 누르고 있는 동안만 움직인다 (↑↓←→ / WASD, K · 스페이스 정지) ----
    // 조종이 잠겨 있으면(robot.teleopArmed false) Robot 이 입력을 버린다. Ctrl+Enter 로 바로 풀 수 있다.
    property var held: ({})
    property bool lockHint: false
    readonly property bool wheelPaused: wheelGraph.paused   // 시험 상태(state.json dash)용
    // 시험 상태용: 바퀴 그래프 시간 창 · X 폭, 멈춘 뒤 IMU 그래프보다 X 끝이 뒤처진 s, 사용자 그래프 계열 수 · 마지막 계열 경로 · 점 수
    readonly property real wheelWindowSec: wheelGraph.windowSec
    readonly property real wheelSpanSec: wheelGraph.feeder.xMax - wheelGraph.feeder.xMin
    readonly property real wheelLagSec: imuGraph.feeder.xMax - wheelGraph.feeder.xMax
    readonly property int userSeriesCount: userGraphPanel.objs.length
    readonly property string userLastPath: userGraphPanel.objs.length ? userGraphPanel.objs[userGraphPanel.objs.length - 1].basePath : ""
    readonly property int userLastCount: userGraphPanel.objs.length ? userGraphPanel.objs[userGraphPanel.objs.length - 1].count : 0
    function showLockHint() { if (!robot.teleopArmed) { lockHint = true; lockHintTimer.restart() } }
    Timer { id: lockHintTimer; interval: 3000; onTriggered: page.lockHint = false }
    // 입력칸에 포커스가 있으면 입력칸이 처리하지 않은 키(↑↓ 등)라도 조종으로 쓰지 않는다
    function typing() { const f = Window.activeFocusItem; return !!f && f.cursorPosition !== undefined }
    function applyKeys() {
        const h = held
        robot.drive((h.f ? 1 : 0) - (h.b ? 1 : 0), (h.l ? 1 : 0) - (h.r ? 1 : 0))
    }
    function keyDir(k) {
        if (k === Qt.Key_Up || k === Qt.Key_W) return "f"
        if (k === Qt.Key_Down || k === Qt.Key_S) return "b"
        if (k === Qt.Key_Left || k === Qt.Key_A) return "l"
        if (k === Qt.Key_Right || k === Qt.Key_D) return "r"
        return ""
    }
    Keys.onPressed: (e) => {
        if (!page.active || typing()) return   // 데이터 점검 · 연결 페이지에서는 조종 키를 쓰지 않는다
        if (e.isAutoRepeat) { e.accepted = true; return }
        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) {
            robot.armTeleop(true); e.accepted = true; return
        }
        const d = keyDir(e.key)
        if (d) { held[d] = true; applyKeys(); if (robot.mode === "키보드 조종") showLockHint(); e.accepted = true }
        else if (e.key === Qt.Key_K || e.key === Qt.Key_Space) { held = {}; robot.drive(0, 0); e.accepted = true }
    }
    Keys.onReleased: (e) => {
        if (!page.active || e.isAutoRepeat || typing()) return
        const d = keyDir(e.key)
        if (d) { held[d] = false; applyKeys(); e.accepted = true }
    }
    onActiveFocusChanged: if (!activeFocus) { held = {}; robot.drive(0, 0) }

    GraphLink { id: graphLink }

    // 기본 그래프는 Robot::sampled(30 Hz)를 push 한다. 목표(cmd)는 계단으로 그린다.
    // 목표(/wheel_cmd)는 기본 숨김 — 범례에서 켜면 그때 구독한다(파이 rosbridge 메시지 수를 던다)
    PlotSeries { id: sCmdL; name: "목표 L"; color: Theme.series[1]; steps: true; enabled: false }
    PlotSeries { id: sCmdR; name: "목표 R"; color: Theme.series[4]; steps: true; enabled: false }
    readonly property bool wheelCmdShown: sCmdL.enabled || sCmdR.enabled
    onWheelCmdShownChanged: robot.setWheelCmdWanted(wheelCmdShown)
    PlotSeries { id: sActL; name: "좌 실측"; color: Theme.series[0] }
    PlotSeries { id: sActR; name: "우 실측"; color: Theme.series[3] }
    PlotSeries { id: sGyro; name: "gyro z"; color: Theme.series[2] }
    PlotSeries { id: sAcc; name: "accel x"; color: Theme.series[0] }

    // 수치가 오래됐는지: 토픽을 마지막으로 받은 시각(ms)
    property real nowMs: Date.now()
    property real wheelSeen: 0
    property real imuSeen: 0
    readonly property bool wheelStale: wheelSeen > 0 && nowMs - wheelSeen > 1500
    readonly property bool imuStale: imuSeen > 0 && nowMs - imuSeen > 1500
    Timer { interval: 500; repeat: true; running: page.active; onTriggered: page.nowMs = Date.now() }
    Connections {
        target: robot.bridge
        function onReceived(topic) {
            if (topic === "/wheel_states") page.wheelSeen = Date.now()
            else if (topic === "/imu/data_raw") page.imuSeen = Date.now()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 16

            SceneView {
                objectName: "scene"
                robot: page.robot
                Layout.fillWidth: true
                Layout.fillHeight: true
                onClickedView: page.forceActiveFocus()
            }

            ScrollView {
                objectName: "dashScroll"
                Layout.preferredWidth: 400
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    // ---- 센서 연결 ----
                    Panel {
                        Layout.fillWidth: true
                        title: "센서 연결"
                        subtitle: {
                            const n = robot.sensors.filter(s => s.state === "수신 중").length
                            return n + " / " + robot.sensors.length + " 수신 중" + (robot.probed ? " · 방금 시험함" : "")
                        }
                        headerExtra: Btn { text: "연결 시험"; kind: "accent"; implicitHeight: 34; font.pixelSize: Theme.fsBody; onClicked: robot.probe() }
                        Repeater {
                            model: robot.sensors
                            delegate: Item {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 44
                                Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }
                                CheckBox {
                                    id: cb
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    focusPolicy: Qt.NoFocus
                                    checked: modelData.on
                                    onToggled: robot.setSensor(modelData.key, checked)
                                    padding: 0
                                }
                                Column {
                                    anchors.left: cb.right
                                    anchors.leftMargin: 8
                                    anchors.right: badge.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text { text: modelData.label; color: Theme.text1; font.pixelSize: Theme.fsBody }
                                    Text {
                                        width: parent.width; elide: Text.ElideRight
                                        text: modelData.topic + " · " + modelData.detail
                                        color: Theme.text2; font.family: page.mono; font.pixelSize: Theme.fsCaption
                                    }
                                }
                                Rectangle {
                                    id: badge
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: Theme.rPill
                                    height: 22
                                    width: bt.implicitWidth + 18
                                    color: modelData.state === "수신 중" ? Theme.okSubtle : modelData.state === "꺼짐" ? Theme.surface3 : Theme.warnSubtle
                                    Text {
                                        id: bt; anchors.centerIn: parent; text: modelData.state; font.pixelSize: Theme.fsSmall
                                        color: modelData.state === "수신 중" ? Theme.okText : modelData.state === "꺼짐" ? Theme.text2 : Theme.warnText
                                    }
                                }
                            }
                        }
                    }

                    // ---- 바퀴 속도 ----
                    Panel {
                        Layout.fillWidth: true
                        title: "바퀴 속도"
                        subtitle: "rad/s"
                        headerExtra: Btn {
                            size: "sm"; kind: graphLink.linked ? "normal" : "ghost"
                            implicitHeight: Theme.hXs
                            leftPadding: Theme.sp2; rightPadding: Theme.sp2
                            text: graphLink.linked ? "X 묶음 켜짐" : "X 묶음"
                            onClicked: graphLink.linked = !graphLink.linked
                        }
                        TimeGraph {
                            id: wheelGraph
                            objectName: "wheelGraph"
                            Layout.fillWidth: true
                            name: "wheel"
                            robot: page.robot
                            link: graphLink
                            active: page.active
                            series: [sCmdL, sCmdR, sActL, sActR]
                            emptyText: "/wheel_states 를 기다리는 중"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp4
                            readonly property int age: Math.round((page.nowMs - page.wheelSeen) / 1000)
                            Metric { label: "목표 L / R"; valueSize: Theme.fsBody; stale: page.wheelStale; ageSec: parent.age; value: page.wheelSeen > 0 && page.wheelCmdShown ? robot.wheelCmdL.toFixed(2) + " / " + robot.wheelCmdR.toFixed(2) : "—" }
                            Metric { label: "좌 실측"; valueSize: Theme.fsBody; stale: page.wheelStale; ageSec: parent.age; value: page.wheelSeen > 0 ? robot.wheelL.toFixed(2) : "—" }
                            Metric { label: "우 실측"; valueSize: Theme.fsBody; stale: page.wheelStale; ageSec: parent.age; value: page.wheelSeen > 0 ? robot.wheelR.toFixed(2) : "—" }
                            Item { Layout.fillWidth: true }
                        }
                        Text {
                            readonly property real avg: (robot.wheelL + robot.wheelR) / 2
                            readonly property real diff: Math.abs(avg) > 0.2 ? Math.abs(robot.wheelL - robot.wheelR) / Math.abs(avg) * 100 : -1
                            text: diff < 0 ? "정지 중" : "좌우 차이 " + diff.toFixed(0) + "% — 직진이 휘는 원인을 바로 확인"
                            color: diff >= 5 ? Theme.warnText : Theme.text2
                            font.pixelSize: Theme.fsSmall
                        }
                    }

                    // ---- 오도메트리 ----
                    Panel {
                        Layout.fillWidth: true
                        title: "오도메트리"
                        GridLayout {
                            columns: 3
                            columnSpacing: 8
                            rowSpacing: 8
                            Layout.fillWidth: true
                            Repeater {
                                model: [
                                    ["x", robot.x.toFixed(3)], ["y", robot.y.toFixed(3)], ["θ", (robot.theta * 180 / Math.PI).toFixed(1) + "°"],
                                    ["v (m/s)", robot.v.toFixed(3)], ["ω (rad/s)", robot.w.toFixed(2)], ["누적 거리", robot.distance.toFixed(2) + " m"]
                                ]
                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 52
                                    radius: Theme.rLg
                                    color: Theme.surface2
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        anchors.leftMargin: 10
                                        Text { text: modelData[0]; color: Theme.text2; font.pixelSize: Theme.fsCaption }
                                        Text { text: modelData[1]; color: Theme.text1; font.family: page.mono; font.pixelSize: Theme.fsTitle }
                                    }
                                }
                            }
                        }
                    }

                    // ---- IMU ----
                    Panel {
                        Layout.fillWidth: true
                        title: "IMU"
                        headerExtra: Text {
                            text: "roll " + robot.roll.toFixed(1) + "° · pitch " + robot.pitch.toFixed(1) + "° · yaw " + robot.yaw.toFixed(1) + "°"
                            color: Theme.text2; font.family: page.mono; font.pixelSize: Theme.fsSmall
                        }
                        TimeGraph {
                            id: imuGraph
                            objectName: "imuGraph"
                            Layout.fillWidth: true
                            graphHeight: 110
                            name: "imu"
                            robot: page.robot
                            link: graphLink
                            active: page.active
                            series: [sGyro, sAcc]
                            emptyText: "/imu/data_raw 를 기다리는 중"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp4
                            readonly property int age: Math.round((page.nowMs - page.imuSeen) / 1000)
                            Metric { label: "gyro z"; unit: "rad/s"; valueSize: Theme.fsBody; stale: page.imuStale; ageSec: parent.age; value: page.imuSeen > 0 ? robot.gyroZ.toFixed(2) : "—" }
                            Metric { label: "accel x"; unit: "m/s²"; valueSize: Theme.fsBody; stale: page.imuStale; ageSec: parent.age; value: page.imuSeen > 0 ? robot.accelX.toFixed(2) : "—" }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    UserGraphPanel {
                        id: userGraphPanel
                        objectName: "userGraphPanel"
                        Layout.fillWidth: true
                        robot: page.robot
                        link: graphLink
                        active: page.active
                    }

                    // ---- LiDAR ----
                    Panel {
                        Layout.fillWidth: true
                        RowLayout {
                            spacing: 14
                            Canvas {
                                id: polar
                                width: 110
                                height: 110
                                property var pts: robot.scanPoints
                                onPtsChanged: requestPaint()
                                onPaint: {
                                    const c = getContext("2d"), r = 54, k = r / 3.0   // 반지름 3 m
                                    c.reset()
                                    c.fillStyle = Theme.base; c.strokeStyle = Theme.borderDefault
                                    c.beginPath(); c.arc(55, 55, r, 0, Math.PI * 2); c.fill(); c.stroke()
                                    c.strokeStyle = Theme.borderDefault
                                    c.beginPath(); c.arc(55, 55, r / 2, 0, Math.PI * 2); c.stroke()
                                    c.fillStyle = Theme.lidar
                                    for (let i = 0; i < pts.length; i += 2) {
                                        const p = pts[i]
                                        const px = 55 - p.y * k, py = 55 - p.x * k   // 위쪽이 로봇 앞
                                        if (Math.hypot(px - 55, py - 55) < r) c.fillRect(px - 1, py - 1, 2, 2)
                                    }
                                    c.strokeStyle = Theme.text1; c.lineWidth = 3
                                    c.beginPath(); c.moveTo(55, 55); c.lineTo(55, 41); c.stroke()
                                    c.fillStyle = Theme.text2; c.beginPath(); c.arc(55, 55, 5, 0, Math.PI * 2); c.fill()
                                }
                            }
                            Column {
                                spacing: 2
                                Text { text: "LiDAR"; color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Font.DemiBold }
                                Text {
                                    color: Theme.text1; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                                    text: "점 <b>" + robot.scanCount.toLocaleString(Qt.locale("en_US"), 'f', 0) + "</b> · " + (robot.rates.length > 1 ? robot.rates[1].replace(/^\S+ /, "") : "")
                                }
                                Text {
                                    color: Theme.text1; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                                    text: robot.nearestRange > 0 ? "가장 가까운 장애물 <b>" + robot.nearestRange.toFixed(2) + " m</b> @ " + robot.nearestAngle.toFixed(0) + "°" : "가장 가까운 장애물 —"
                                }
                                Text { text: "로봇 몸체 점(0.3 m 이내) 숨김"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
                            }
                        }
                    }
                }
            }
        }

        // ---- 조종 ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: foot.implicitHeight + 32
            color: Theme.surface1
            Rectangle { width: parent.width; height: 1; color: Theme.borderDefault }
            RowLayout {
                id: foot
                // 1280 · 1440 · 1920 모두 비상 정지까지 한 줄에 들어가게: 좁으면 조이스틱 글자를 숨긴다
                readonly property bool narrow: width < 1300
                anchors.fill: parent
                anchors.leftMargin: Theme.sp5
                anchors.rightMargin: Theme.sp5
                spacing: Theme.sp4

                Seg {
                    options: ["키보드 조종", "자율주행", "정지"]
                    current: robot.mode
                    onPicked: (v) => { robot.mode = v; page.forceActiveFocus() }
                }

                Grid {
                    columns: 3
                    spacing: 6
                    component Pad: Button {
                        property real lin
                        property real ang
                        property string glyph
                        width: 48; height: 48
                        focusPolicy: Qt.NoFocus
                        onPressed: { page.showLockHint(); robot.drive(lin, ang) }
                        onReleased: robot.drive(0, 0)
                        onCanceled: robot.drive(0, 0)
                        contentItem: Text { text: glyph; color: Theme.text1; font.pixelSize: Theme.fsTitle; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: Theme.rLg; color: parent.down ? Theme.surface3 : Theme.surface2; border.color: Theme.borderDefault }
                    }
                    Item { width: 48; height: 48 }
                    Pad { glyph: "↑"; lin: 1 }
                    Item { width: 48; height: 48 }
                    Pad { glyph: "←"; ang: 1 }
                    Pad { glyph: "K" }
                    Pad { glyph: "→"; ang: -1 }
                    Item { width: 48; height: 48 }
                    Pad { glyph: "↓"; lin: -1 }
                    Item { width: 48; height: 48 }
                }

                Joystick {
                    objectName: "joystick"
                    robot: page.robot
                    showLabel: !foot.narrow
                    onBlocked: page.showLockHint()
                }

                ArmControl { robot: page.robot }

                Column {
                    spacing: 6
                    Text {
                        color: Theme.text2; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                        text: "최대 속도 <b>" + robot.maxSpeed.toFixed(2) + " m/s</b>"
                    }
                    Text { text: "펌웨어 한계 0.25"; color: Theme.text3; font.pixelSize: Theme.fsSmall }
                    Seg {
                        objectName: "rampSeg"
                        options: ["가감속 켬", "끔"]
                        current: robot.rampEnabled ? "가감속 켬" : "끔"
                        enabled: robot.rampKnown
                        opacity: enabled ? 1 : Theme.opDisabled
                        onPicked: (v) => { robot.rampEnabled = (v === "가감속 켬"); page.forceActiveFocus() }
                    }
                    Slider {
                        width: 170
                        from: 0.02; to: 0.25; stepSize: 0.01
                        value: robot.maxSpeed
                        focusPolicy: Qt.NoFocus
                        onMoved: robot.maxSpeed = value
                    }
                }

                ColumnLayout {   // 세 버튼을 위아래로(가로 폭을 아낀다)
                    Layout.fillWidth: false
                    spacing: Theme.sp1
                    Btn { Layout.fillWidth: true; size: "sm"; text: "1 m 직진 시험"; kind: "accent"; enabled: page.canStraight; onClicked: { page.showLockHint(); robot.straight(1.0) } }
                    Btn { Layout.fillWidth: true; size: "sm"; text: "1 m 후진"; enabled: page.canStraight; onClicked: { page.showLockHint(); robot.straight(-1.0) } }
                    Btn { Layout.fillWidth: true; size: "sm"; text: robot.recording ? "기록 중지 " + robot.recordElapsed : "기록 시작"; enabled: page.canRecord; onClicked: robot.toggleRecording() }
                }

                Item { Layout.fillWidth: true }

                Btn {
                    text: "비상 정지"
                    kind: "danger"
                    implicitHeight: 56
                    leftPadding: 28; rightPadding: 28
                    font.pixelSize: Theme.fsTitle; font.bold: true
                    onClicked: robot.estop()
                }
            }
        }
    }

    // 바퀴 · IMU 그래프 표본 (C++ 이 30 Hz 로 준다). 시각은 PlotSeries 공통 시계를 쓴다.
    Connections {
        target: robot
        function onSampled(t, cmdL, cmdR, actL, actR, gz, ax) {
            const now = sCmdL.now()
            sCmdL.push(now, cmdL); sCmdR.push(now, cmdR); sActL.push(now, actL); sActR.push(now, actR)
            sGyro.push(now, gz); sAcc.push(now, ax)
        }
    }

    // 잠긴 채로 조종하려 하면 푸는 방법을 알린다
    Rectangle {
        visible: page.lockHint && !robot.teleopArmed
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 140
        width: hintText.implicitWidth + 2 * Theme.sp4
        height: Theme.hMd
        radius: Theme.rXl
        color: Theme.warnSubtle
        border.color: Theme.warn
        Text {
            id: hintText
            anchors.centerIn: parent
            text: "조종이 잠겨 있음 — Ctrl+Enter 또는 '길게 눌러 잠금 해제' 로 켜기"
            color: Theme.warnText
            font.pixelSize: Theme.fsBody
        }
    }
}
