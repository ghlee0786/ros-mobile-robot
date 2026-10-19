import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs
import RobotDashboard

// 데이터 점검: 토픽별 주기 · 지연 · 상태, CSV 기록, 직진 시험 결과, 이벤트
Item {
    id: page
    required property Robot robot
    readonly property string mono: Theme.monoFamily

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 16

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignTop
            title: "토픽 상태"
            headerExtra: Column {
                spacing: 4
                Text { text: "토픽 찾기"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
                TextField {
                    id: filter
                    width: 220; height: 36
                    placeholderText: "/odom"
                    color: Theme.text1; placeholderTextColor: Theme.text3
                    font.family: page.mono
                    background: Rectangle { radius: Theme.rMd; color: Theme.base; border.color: Theme.borderDefault }
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                GridLayout {
                    id: table
                    width: parent.width
                    columns: 6
                    columnSpacing: 0
                    rowSpacing: 0
                    component Cell: Text {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.text1
                        font.pixelSize: Theme.fsBody
                        elide: Text.ElideRight
                    }
                    Cell { text: "토픽"; color: Theme.text2; Layout.preferredWidth: 260 }
                    Cell { text: "형식"; color: Theme.text2; Layout.preferredWidth: 240 }
                    Cell { text: "주기"; color: Theme.text2; Layout.preferredWidth: 90 }
                    Cell { text: "지연"; color: Theme.text2; Layout.preferredWidth: 90 }
                    Cell { text: "상태"; color: Theme.text2; Layout.preferredWidth: 100 }
                    Cell { text: "기록"; color: Theme.text2; Layout.preferredWidth: 60 }
                    Repeater {   // 토픽마다 6 칸

                        model: {
                            const cells = []
                            for (const t of robot.topicRows)
                                if (filter.text === "" || t.name.indexOf(filter.text) >= 0)
                                    for (let c = 0; c < 6; ++c) cells.push({ t: t, c: c })
                            return cells
                        }
                        delegate: Item {
                            required property var modelData
                            readonly property var t: modelData.t
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
                            Text {
                                visible: modelData.c < 4
                                anchors.fill: parent
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                font.family: page.mono
                                font.pixelSize: modelData.c === 1 ? Theme.fsSmall : Theme.fsBody
                                color: modelData.c === 1 ? Theme.text2 : Theme.text1
                                text: [t.name, t.type, t.hz, t.lat][modelData.c] ?? ""
                            }
                            Rectangle {
                                visible: modelData.c === 4
                                x: 8; anchors.verticalCenter: parent.verticalCenter
                                radius: Theme.rPill; height: 22; width: st.implicitWidth + 18
                                color: t.state === "정상" ? Theme.okSubtle : t.state === "구독 안 함" ? Theme.surface3 : Theme.warnSubtle
                                Text {
                                    id: st; anchors.centerIn: parent; text: t.state; font.pixelSize: Theme.fsSmall
                                    color: t.state === "정상" ? Theme.okText : t.state === "구독 안 함" ? Theme.text2 : Theme.warnText
                                }
                            }
                            CheckBox {
                                visible: modelData.c === 5
                                x: 4; anchors.verticalCenter: parent.verticalCenter
                                checked: t.rec
                                onToggled: robot.setRecordTopic(t.name, checked)
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 12

            Panel {
                Layout.fillWidth: true
                title: "기록 (CSV)"
                Text {
                    color: Theme.text2; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                    text: "선택한 토픽 <b>" + robot.recordTopics.length + "개</b> · 경과 <b>" + robot.recordElapsed + "</b>"
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: robot.recordPath === "" ? "records/<시각>/<토픽>.csv 로 저장" : robot.recordPath
                    color: Theme.text2; font.family: page.mono; font.pixelSize: Theme.fsSmall
                }
                Row {
                    spacing: 8
                    Btn { text: robot.recording ? "기록 중지" : "기록 시작"; kind: robot.recording ? "danger" : "accent"; onClicked: robot.toggleRecording() }
                    Btn { text: "파일 열기"; onClicked: robot.openRecords() }
                }
            }

            Panel {
                Layout.fillWidth: true
                title: "직진 시험 결과"
                subtitle: robot.straightRunning ? "진행 중" : (robot.straightResult.reason ?? "")
                GraphsView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 130
                    marginLeft: 0; marginRight: 4; marginTop: 4; marginBottom: 0
                    theme: GraphsTheme {
                        backgroundVisible: false
                        plotAreaBackgroundColor: Theme.surface1
                        grid.mainColor: Theme.borderDefault
                        labelTextColor: Theme.text2
                        axisXLabelFont.pixelSize: Theme.fsCaption
                        axisYLabelFont.pixelSize: Theme.fsCaption
                    }
                    axisX: ValueAxis { min: 0; max: 1.0; tickInterval: 0.25; labelDecimals: 2; subGridVisible: false }
                    axisY: ValueAxis {   // 옆 오차 cm
                        readonly property real span: Math.max(2, ...robot.straightTrack.map(p => Math.abs(p.y)))
                        min: -span; max: span; tickInterval: span; labelDecimals: 1; subGridVisible: false
                    }
                    LineSeries { color: Theme.series[1]; width: 2; XYPoint { x: 0; y: 0 } XYPoint { x: 1; y: 0 } }
                    LineSeries {
                        id: track
                        color: Theme.series[0]; width: 2.5
                        property var pts: robot.straightTrack
                        onPtsChanged: { clear(); for (const p of pts) append(p.x, p.y) }
                    }
                }
                GridLayout {
                    columns: 3
                    columnSpacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: [
                            ["옆 오차", robot.straightResult.cross !== undefined ? (robot.straightResult.cross >= 0 ? "+" : "") + robot.straightResult.cross.toFixed(1) + " cm" : "—"],
                            ["방향 오차", robot.straightResult.heading !== undefined ? (robot.straightResult.heading >= 0 ? "+" : "") + robot.straightResult.heading.toFixed(1) + "°" : "—"],
                            ["소요", robot.straightResult.duration !== undefined ? robot.straightResult.duration.toFixed(1) + " s" : "—"]
                        ]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: Theme.rLg; color: Theme.surface2
                            Column {
                                anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 10
                                Text { text: modelData[0]; color: Theme.text2; font.pixelSize: Theme.fsSmall }
                                Text { text: modelData[1]; color: Theme.text1; font.family: page.mono; font.pixelSize: Theme.fsTitle }
                            }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "이벤트"
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 260
                    clip: true
                    spacing: 6
                    model: robot.events
                    delegate: Text {
                        required property string modelData
                        width: ListView.view.width
                        elide: Text.ElideRight
                        text: modelData
                        font.family: page.mono
                        font.pixelSize: Theme.fsBody
                        color: modelData.indexOf("지연") >= 0 || modelData.indexOf("끊김") >= 0 || modelData.indexOf("비상") >= 0 ? Theme.warnText : Theme.text1
                    }
                }
            }
        }
    }
}
