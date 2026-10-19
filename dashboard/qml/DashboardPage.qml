import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs
import RobotDashboard

FocusScope {
    id: page
    required property Robot robot
    property bool active: true
    readonly property string mono: "Consolas"
    focus: true
    onActiveChanged: if (active) forceActiveFocus()
    Component.onCompleted: forceActiveFocus()

    // ---- 키보드 조종: 누르고 있는 동안만 움직인다 (↑↓←→ / WASD, K · 스페이스 정지) ----
    property var held: ({})
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
        if (e.isAutoRepeat) { e.accepted = true; return }
        const d = keyDir(e.key)
        if (d) { held[d] = true; applyKeys(); e.accepted = true }
        else if (e.key === Qt.Key_K || e.key === Qt.Key_Space) { held = {}; robot.drive(0, 0); e.accepted = true }
    }
    Keys.onReleased: (e) => {
        if (e.isAutoRepeat) return
        const d = keyDir(e.key)
        if (d) { held[d] = false; applyKeys(); e.accepted = true }
    }
    onActiveFocusChanged: if (!activeFocus) { held = {}; robot.drive(0, 0) }

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
                        headerExtra: Btn { text: "연결 시험"; kind: "accent"; implicitHeight: 34; font.pixelSize: 13; onClicked: robot.probe() }
                        Repeater {
                            model: robot.sensors
                            delegate: Item {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 44
                                Rectangle { width: parent.width; height: 1; color: "#222932" }
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
                                    Text { text: modelData.label; color: "#E6E9EE"; font.pixelSize: 13 }
                                    Text {
                                        width: parent.width; elide: Text.ElideRight
                                        text: modelData.topic + " · " + modelData.detail
                                        color: "#9AA3AF"; font.family: page.mono; font.pixelSize: 11
                                    }
                                }
                                Rectangle {
                                    id: badge
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 999
                                    height: 22
                                    width: bt.implicitWidth + 18
                                    color: modelData.state === "수신 중" ? "#17302A" : modelData.state === "꺼짐" ? "#232A33" : "#3A3320"
                                    Text {
                                        id: bt; anchors.centerIn: parent; text: modelData.state; font.pixelSize: 12
                                        color: modelData.state === "수신 중" ? "#8EE2B4" : modelData.state === "꺼짐" ? "#AEB6C0" : "#F5D27A"
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
                        GraphsView {
                            id: wheelGraph
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            marginLeft: 0; marginRight: 0; marginTop: 4; marginBottom: 0
                            theme: GraphsTheme {
                                backgroundVisible: false
                                plotAreaBackgroundColor: "#161A21"
                                grid.mainColor: "#232A33"
                                labelTextColor: "#9AA3AF"
                                axisXLabelFont.pixelSize: 10
                                axisYLabelFont.pixelSize: 10
                            }
                            axisX: ValueAxis { id: wx; min: 0; max: 10; visible: false; gridVisible: false; subGridVisible: false }
                            axisY: ValueAxis { min: -2.5; max: 2.5; tickInterval: 1; subGridVisible: false; labelDecimals: 0 }
                            LineSeries { id: sCmdL; color: "#F2A33A"; width: 1.5 }
                            LineSeries { id: sCmdR; color: "#C98A35"; width: 1.5 }
                            LineSeries { id: sActL; color: "#4CC3E8"; width: 2.2 }
                            LineSeries { id: sActR; color: "#B48CF2"; width: 2.2 }
                        }
                        GridLayout {
                            columns: 3
                            Layout.fillWidth: true
                            Text { color: "#C5CBD3"; font.pixelSize: 12; textFormat: Text.StyledText; text: "<font color='#F2A33A'>┄</font> 목표 <b>" + robot.wheelCmdL.toFixed(2) + " / " + robot.wheelCmdR.toFixed(2) + "</b>" }
                            Text { color: "#C5CBD3"; font.pixelSize: 12; textFormat: Text.StyledText; text: "<font color='#4CC3E8'>━</font> 좌 실측 <b>" + robot.wheelL.toFixed(2) + "</b>" }
                            Text { color: "#C5CBD3"; font.pixelSize: 12; textFormat: Text.StyledText; text: "<font color='#B48CF2'>━</font> 우 실측 <b>" + robot.wheelR.toFixed(2) + "</b>" }
                        }
                        Text {
                            readonly property real avg: (robot.wheelL + robot.wheelR) / 2
                            readonly property real diff: Math.abs(avg) > 0.2 ? Math.abs(robot.wheelL - robot.wheelR) / Math.abs(avg) * 100 : -1
                            text: diff < 0 ? "정지 중" : "좌우 차이 " + diff.toFixed(0) + "% — 직진이 휘는 원인을 바로 확인"
                            color: diff >= 5 ? "#F2C27A" : "#9AA3AF"
                            font.pixelSize: 12
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
                                    radius: 10
                                    color: "#1D232C"
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        anchors.leftMargin: 10
                                        Text { text: modelData[0]; color: "#9AA3AF"; font.pixelSize: 11 }
                                        Text { text: modelData[1]; color: "#E6E9EE"; font.family: page.mono; font.pixelSize: 18 }
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
                            color: "#9AA3AF"; font.family: page.mono; font.pixelSize: 12
                        }
                        GraphsView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 90
                            marginLeft: 0; marginRight: 0; marginTop: 4; marginBottom: 0
                            theme: GraphsTheme {
                                backgroundVisible: false
                                plotAreaBackgroundColor: "#161A21"
                                grid.mainColor: "#232A33"
                                labelTextColor: "#9AA3AF"
                                axisYLabelFont.pixelSize: 10
                            }
                            axisX: ValueAxis { id: ix; min: 0; max: 10; visible: false; gridVisible: false; subGridVisible: false }
                            axisY: ValueAxis { min: -1; max: 1; tickInterval: 1; subGridVisible: false; labelDecimals: 0 }
                            LineSeries { id: sGyro; color: "#5BC48A"; width: 2 }
                            LineSeries { id: sAcc; color: "#7FD3F0"; width: 1.6 }
                        }
                        Row {
                            spacing: 14
                            Text { color: "#C5CBD3"; font.pixelSize: 12; textFormat: Text.StyledText; text: "<font color='#5BC48A'>━</font> gyro z " + robot.gyroZ.toFixed(2) }
                            Text { color: "#C5CBD3"; font.pixelSize: 12; textFormat: Text.StyledText; text: "<font color='#7FD3F0'>━</font> accel x " + robot.accelX.toFixed(2) }
                        }
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
                                    c.fillStyle = "#11161C"; c.strokeStyle = "#2B323C"
                                    c.beginPath(); c.arc(55, 55, r, 0, Math.PI * 2); c.fill(); c.stroke()
                                    c.strokeStyle = "#232A33"
                                    c.beginPath(); c.arc(55, 55, r / 2, 0, Math.PI * 2); c.stroke()
                                    c.fillStyle = "#E0457B"
                                    for (let i = 0; i < pts.length; i += 2) {
                                        const p = pts[i]
                                        const px = 55 - p.y * k, py = 55 - p.x * k   // 위쪽이 로봇 앞
                                        if (Math.hypot(px - 55, py - 55) < r) c.fillRect(px - 1, py - 1, 2, 2)
                                    }
                                    c.strokeStyle = "#E6E9EE"; c.lineWidth = 3
                                    c.beginPath(); c.moveTo(55, 55); c.lineTo(55, 41); c.stroke()
                                    c.fillStyle = "#C9CFD6"; c.beginPath(); c.arc(55, 55, 5, 0, Math.PI * 2); c.fill()
                                }
                            }
                            Column {
                                spacing: 2
                                Text { text: "LiDAR"; color: "#E6E9EE"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Text {
                                    color: "#E6E9EE"; font.pixelSize: 13; textFormat: Text.StyledText
                                    text: "점 <b>" + robot.scanCount.toLocaleString(Qt.locale("en_US"), 'f', 0) + "</b> · " + (robot.rates.length > 1 ? robot.rates[1].replace("/scan ", "") : "")
                                }
                                Text {
                                    color: "#E6E9EE"; font.pixelSize: 13; textFormat: Text.StyledText
                                    text: robot.nearestRange > 0 ? "가장 가까운 장애물 <b>" + robot.nearestRange.toFixed(2) + " m</b> @ " + robot.nearestAngle.toFixed(0) + "°" : "가장 가까운 장애물 —"
                                }
                                Text { text: "로봇 몸체 점(0.3 m 이내) 숨김"; color: "#9AA3AF"; font.pixelSize: 12 }
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
            color: "#161A21"
            Rectangle { width: parent.width; height: 1; color: "#2B323C" }
            RowLayout {
                id: foot
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 24

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
                        onPressed: robot.drive(lin, ang)
                        onReleased: robot.drive(0, 0)
                        onCanceled: robot.drive(0, 0)
                        contentItem: Text { text: glyph; color: "#E6E9EE"; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 10; color: parent.down ? "#26303B" : "#1D232C"; border.color: "#2E3744" }
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

                Column {
                    spacing: 6
                    Text {
                        color: "#C5CBD3"; font.pixelSize: 13; textFormat: Text.StyledText
                        text: "최대 속도 <b>" + robot.maxSpeed.toFixed(2) + " m/s</b> <font color='#9AA3AF'>(펌웨어 한계 0.10)</font>"
                    }
                    Slider {
                        width: 220
                        from: 0.02; to: 0.10; stepSize: 0.01
                        value: robot.maxSpeed
                        focusPolicy: Qt.NoFocus
                        onMoved: robot.maxSpeed = value
                    }
                }

                Row {
                    spacing: 8
                    Btn { text: "1 m 직진 시험"; kind: "accent"; enabled: !robot.straightRunning; onClicked: robot.straight(1.0) }
                    Btn { text: "1 m 후진"; enabled: !robot.straightRunning; onClicked: robot.straight(-1.0) }
                    Btn { text: robot.recording ? "기록 중지 " + robot.recordElapsed : "기록 시작"; onClicked: robot.toggleRecording() }
                }

                Item { Layout.fillWidth: true }

                Btn {
                    text: "비상 정지"
                    kind: "danger"
                    implicitHeight: 56
                    leftPadding: 28; rightPadding: 28
                    font.pixelSize: 17; font.bold: true
                    onClicked: robot.estop()
                }
            }
        }
    }

    // 최근 10 s 그래프 (C++ 이 30 Hz 로 표본을 준다)
    Connections {
        target: robot
        function onSampled(t, cmdL, cmdR, actL, actR, gz, ax) {
            const add = (s, v) => { s.append(t, v); if (s.count > 300) s.remove(0) }
            add(sCmdL, cmdL); add(sCmdR, cmdR); add(sActL, actL); add(sActR, actR)
            add(sGyro, gz); add(sAcc, ax)
            wx.min = ix.min = t - 10
            wx.max = ix.max = t
        }
    }
}
