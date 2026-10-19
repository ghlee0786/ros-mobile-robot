import QtQuick
import QtQuick.Controls.Basic
import RobotDashboard

// 3D 뷰 왼쪽 위 레이어 목록: 행마다 표시 토글 · 색이나 크기 · 상태 문구. 값은 SceneView(scene)의 속성에 두고
// SceneView 가 QtCore Settings 에 저장한다.
Rectangle {
    id: root
    required property var scene
    required property Robot robot
    required property ScanAccumulator scanAcc
    required property CostmapLayer globalCostmap
    required property CostmapLayer localCostmap
    required property FootprintLayer footprint
    required property TfTree tf
    required property AmclPose amcl
    required property AmclPose odomCov
    property real maxHeight: 400
    readonly property string navMissing: "수신 없음 · PC Nav2(도메인 8) 연결 필요"

    // /plan 이 마지막으로 온 시각(상태 문구용, 1 s 마다 다시 본다)
    property double planSeenMs: 0
    property double nowMs: Date.now()
    Connections { target: root.robot; function onPlanChanged() { if (root.robot.plan.length > 0) root.planSeenMs = Date.now() } }
    Timer { interval: 1000; running: root.visible; repeat: true; onTriggered: root.nowMs = Date.now() }

    radius: Theme.rLg
    color: Theme.hudFill
    width: root.scene.layerPanelOpen ? 280 : 120
    height: Math.min(maxHeight, body.implicitHeight + 2 * Theme.sp2)
    clip: true

    // 작은 체크 표시 + 글자 (행의 주 토글과 세부 옵션이 같이 쓴다)
    component Check: Item {
        id: ck
        property bool on
        property string text
        property bool strong: false
        signal flip()
        implicitWidth: box.width + Theme.sp2 + lbl.implicitWidth
        implicitHeight: 24
        Rectangle {
            id: box
            width: 14; height: 14; radius: Theme.rXs
            anchors.verticalCenter: parent.verticalCenter
            color: ck.on ? Theme.accent : "transparent"
            border.color: ck.on ? Theme.accent : Theme.hudText2
            Text { anchors.centerIn: parent; visible: ck.on; text: "✓"; color: Theme.textOnAccent; font.pixelSize: Theme.fsCaption }
        }
        Text {
            id: lbl
            anchors.left: box.right; anchors.leftMargin: Theme.sp2; anchors.verticalCenter: parent.verticalCenter
            text: ck.text; color: Theme.hudText1
            font.family: Theme.fontFamily; font.pixelSize: ck.strong ? Theme.fsBody : Theme.fsSmall
            font.weight: ck.strong ? Theme.wMedium : Theme.wRegular
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ck.flip() }
    }
    // 레이어 한 행: 주 토글 · 색 견본 · 상태 문구 · (켜졌을 때) 세부 옵션
    component LayerRow: Column {
        id: lr
        property alias text: main.text
        property alias on: main.on
        property color swatch: "transparent"
        property string status
        property bool warn: false
        default property alias details: det.data
        signal flip()
        width: parent ? parent.width : 0
        spacing: 2
        Item {
            width: parent.width; height: main.implicitHeight
            Check { id: main; strong: true; onFlip: lr.flip() }
            Rectangle {
                visible: lr.swatch.a > 0
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 16; height: 4; radius: 2; color: lr.swatch
            }
        }
        Text {
            visible: lr.status !== ""
            leftPadding: 22
            width: parent.width
            wrapMode: Text.Wrap
            text: lr.status
            color: lr.warn ? Theme.warnText : Theme.hudText2
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsCaption
        }
        Column { id: det; visible: lr.on && children.length > 0; leftPadding: 22; spacing: Theme.sp1; width: parent.width }
    }
    component Labeled: Row {
        property alias label: l.text
        spacing: Theme.sp2
        Text { id: l; width: 44; anchors.verticalCenter: parent.verticalCenter; color: Theme.hudText2; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSmall }
    }
    component SmallSlider: Slider {
        width: 150; height: 24
        focusPolicy: Qt.NoFocus
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.sp2
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body
            width: parent.width - Theme.sp1
            spacing: Theme.spNudge

            Item {
                width: parent.width; height: 24
                Text { anchors.verticalCenter: parent.verticalCenter; text: "레이어"; color: Theme.hudText1; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBody; font.weight: Theme.wStrong }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: root.scene.layerPanelOpen ? "접기" : "펼치기"; color: Theme.accentText; font.pixelSize: Theme.fsSmall
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.scene.layerPanelOpen = !root.scene.layerPanelOpen }
                }
            }

            Column {
                visible: root.scene.layerPanelOpen
                width: parent.width
                spacing: Theme.spNudge

                LayerRow {
                    text: "LiDAR 점"; swatch: Theme.lidar; on: root.scene.showScan; onFlip: root.scene.showScan = !on
                    status: !(root.robot.sensorOn.scan ?? false) ? "센서 꺼짐" : root.robot.scanCount === 0 ? "/scan 수신 없음"
                          : root.scene.scanDecay > 0 ? "잔상 " + root.scanAcc.pointCount + " 점" : ""
                    warn: status !== "" && root.scene.scanDecay === 0
                    Labeled {
                        label: "잔상"
                        Seg {
                            readonly property var secs: [0, 1, 3, 10]
                            options: ["끔", "1 s", "3 s", "10 s"]
                            current: options[Math.max(0, secs.indexOf(root.scene.scanDecay))]
                            onPicked: (v) => root.scene.scanDecay = secs[options.indexOf(v)]
                        }
                    }
                    Labeled {
                        label: "색"
                        Seg {
                            readonly property var modes: ["flat", "range", "intensity"]
                            options: ["단색", "거리", "강도"]
                            current: options[Math.max(0, modes.indexOf(root.scene.scanColorMode))]
                            onPicked: (v) => root.scene.scanColorMode = modes[options.indexOf(v)]
                        }
                    }
                    Labeled {
                        label: "크기"
                        SmallSlider { from: 0.008; to: 0.06; value: root.scene.scanPointSize; onMoved: root.scene.scanPointSize = value }
                    }
                }

                LayerRow {
                    text: "이동 경로"; swatch: Theme.trail; on: root.scene.showTrail; onFlip: root.scene.showTrail = !on
                    status: root.planSeenMs > 0 && root.nowMs - root.planSeenMs > 5000 ? "/plan 5 s 이상 없음" : ""
                    warn: true
                    Check { text: "그라디언트 (오래된 쪽 흐리게)"; on: root.scene.trailGradient; onFlip: root.scene.trailGradient = !on }
                }

                LayerRow {
                    text: "좌표축 (base_link)"; on: root.scene.showAxes; onFlip: root.scene.showAxes = !on
                    status: !(root.robot.sensorOn.tf ?? false) ? "TF 센서 꺼짐" : ""
                }

                LayerRow {
                    text: "지도"; on: root.scene.showMap; onFlip: root.scene.showMap = !on
                    status: on && !root.robot.hasMap ? "/map 수신 없음" : ""
                    warn: true
                    Labeled {
                        label: "팔레트"
                        Seg { options: ["map", "costmap", "raw"]; current: root.robot.mapScheme; onPicked: (v) => root.robot.mapScheme = v }
                    }
                    Labeled {
                        label: "투명도"
                        SmallSlider { from: 0.1; to: 1; value: root.scene.mapOpacity; onMoved: root.scene.mapOpacity = value }
                    }
                    Check { text: "Draw Behind (다른 것 뒤에 그림)"; on: root.scene.mapDrawBehind; onFlip: root.scene.mapDrawBehind = !on }
                }

                LayerRow {
                    text: "전역 코스트맵"; on: root.scene.showGlobalCostmap; onFlip: root.scene.showGlobalCostmap = !on
                    status: on && !root.globalCostmap.received ? root.navMissing : ""; warn: true
                }
                LayerRow {
                    text: "지역 코스트맵"; on: root.scene.showLocalCostmap; onFlip: root.scene.showLocalCostmap = !on
                    status: on && !root.localCostmap.received ? root.navMissing : ""; warn: true
                }
                LayerRow {
                    text: "Footprint"; swatch: Theme.series[1]; on: root.scene.showFootprint; onFlip: root.scene.showFootprint = !on
                    status: on && !root.footprint.received ? root.navMissing : ""; warn: true
                }

                LayerRow {
                    text: "TF 축"; swatch: Theme.axisX; on: root.scene.showTf; onFlip: root.scene.showTf = !on
                    status: !on ? "" : !root.tf.received ? "TF 수신 없음"
                          : (root.robot.mapOdomFresh ? "" : "map→odom 끊김 (10 s 넘게 없음) · ") + root.tf.frameCount + " 프레임"
                    warn: on && (!root.tf.received || !root.robot.mapOdomFresh)
                    Check { text: "이름 라벨"; on: root.scene.tfLabels; onFlip: root.scene.tfLabels = !on }
                    Labeled {
                        label: "크기"
                        SmallSlider { from: 0.5; to: 4; value: root.scene.tfScale; onMoved: root.scene.tfScale = value }
                    }
                }

                LayerRow {
                    text: "AMCL 자세 (/amcl_pose)"; swatch: Theme.series[0]; on: root.scene.showAmcl; onFlip: root.scene.showAmcl = !on
                    status: on && !root.amcl.received ? root.navMissing : ""; warn: true
                }
                LayerRow {
                    text: "오도메트리 공분산"; swatch: Theme.series[2]; on: root.scene.showOdomCov; onFlip: root.scene.showOdomCov = !on
                    status: on && !root.odomCov.received ? "/odom 수신 없음" : on && !root.odomCov.valid ? "/odom 공분산이 모두 0 (그릴 것 없음)" : ""
                }
            }
        }
    }
}
