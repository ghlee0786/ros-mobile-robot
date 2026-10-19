import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs
import RobotDashboard

// 기록 · 시험 탭: CSV 기록, 직진 시험 결과, 이벤트(예전 점검 화면 오른쪽 열을 그대로 옮김)
RowLayout {
    id: tab
    required property Robot robot
    readonly property string mono: Theme.monoFamily
    spacing: Theme.sp4

    ColumnLayout {
        Layout.preferredWidth: 420
        Layout.fillWidth: false
        Layout.fillHeight: true
        Layout.alignment: Qt.AlignTop
        spacing: Theme.sp3

        Panel {
            Layout.fillWidth: true
            title: "기록 (CSV)"
            Text {
                color: Theme.text2; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                text: "선택한 토픽 <b>" + tab.robot.recordTopics.length + "개</b> · 경과 <b>" + tab.robot.recordElapsed + "</b>"
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                text: tab.robot.recordPath === "" ? "records/<시각>/<토픽>.csv 로 저장 · 토픽 탭의 기록 칸으로 고른다" : tab.robot.recordPath
                color: Theme.text2; font.family: tab.mono; font.pixelSize: Theme.fsSmall
            }
            Row {
                spacing: 8
                Btn { text: tab.robot.recording ? "기록 중지" : "기록 시작"; kind: tab.robot.recording ? "danger" : "accent"; onClicked: tab.robot.toggleRecording() }
                Btn { text: "파일 열기"; onClicked: tab.robot.openRecords() }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: "직진 시험 결과"
            subtitle: tab.robot.straightRunning ? "진행 중" : (tab.robot.straightResult.reason ?? "")
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
                    readonly property real span: Math.max(2, ...tab.robot.straightTrack.map(p => Math.abs(p.y)))
                    min: -span; max: span; tickInterval: span; labelDecimals: 1; subGridVisible: false
                }
                LineSeries { color: Theme.series[1]; width: 2; XYPoint { x: 0; y: 0 } XYPoint { x: 1; y: 0 } }
                LineSeries {
                    color: Theme.series[0]; width: 2.5
                    property var pts: tab.robot.straightTrack
                    onPtsChanged: { clear(); for (const p of pts) append(p.x, p.y) }
                }
            }
            GridLayout {
                columns: 3
                columnSpacing: 8
                Layout.fillWidth: true
                Repeater {
                    model: [
                        ["옆 오차", tab.robot.straightResult.cross !== undefined ? (tab.robot.straightResult.cross >= 0 ? "+" : "") + tab.robot.straightResult.cross.toFixed(1) + " cm" : "—"],
                        ["방향 오차", tab.robot.straightResult.heading !== undefined ? (tab.robot.straightResult.heading >= 0 ? "+" : "") + tab.robot.straightResult.heading.toFixed(1) + "°" : "—"],
                        ["소요", tab.robot.straightResult.duration !== undefined ? tab.robot.straightResult.duration.toFixed(1) + " s" : "—"]
                    ]
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 52
                        radius: Theme.rLg; color: Theme.surface2
                        Column {
                            anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 10
                            Text { text: modelData[0]; color: Theme.text2; font.pixelSize: Theme.fsSmall }
                            Text { text: modelData[1]; color: Theme.text1; font.family: tab.mono; font.pixelSize: Theme.fsTitle }
                        }
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
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
            model: tab.robot.events
            delegate: Text {
                required property string modelData
                width: ListView.view.width
                elide: Text.ElideRight
                text: modelData
                font.family: tab.mono
                font.pixelSize: Theme.fsBody
                color: modelData.indexOf("지연") >= 0 || modelData.indexOf("끊김") >= 0 || modelData.indexOf("비상") >= 0 ? Theme.warnText : Theme.text1
            }
        }
    }
}
