import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick3D
import QtQuick3D.Helpers
import RobotDashboard

// 3D 뷰: 바닥 격자 · 로봇 모델 · LiDAR 점 · 이동 경로 · 목표/계획 경로 · 좌표축 · 지도 · 거리 측정
ColumnLayout {
    id: root
    required property Robot robot
    property string cam: "추적"
    property string tool: ""   // "" · "측정" · "목표"
    readonly property bool measuring: tool === "측정"
    property var measureA: null   // 측정 점 (odom 프레임, m)
    property var measureB: null
    readonly property real measureLen: measureA && measureB ? measureB.minus(measureA).length() : 0
    property var goalA: null      // Nav2 이동 목표 (odom 프레임, m)
    property real goalYaw: 0      // rad
    property bool goalDragging: false
    // 자유 시점: 로봇 + panOffset 을 중심으로 camYaw · camPitch(deg) 각도, camDist(1 m = 100) 거리에서 본다
    property real camYaw: 0
    property real camPitch: -30
    property real camDist: 250
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    // 지금 보이는 카메라의 각도 (뷰 큐브 표시 · 자유 시점으로 넘어갈 때 시작 각도)
    readonly property real viewYaw: cam === "추적" ? Math.atan2(-Math.cos(robot.theta), Math.sin(robot.theta)) * 180 / Math.PI
                                  : cam === "위에서" ? 0 : camYaw
    readonly property real viewPitch: cam === "추적" ? -Math.atan2(0.9, 1.3) * 180 / Math.PI : cam === "위에서" ? -90 : camPitch
    readonly property string mono: Theme.monoFamily
    signal clickedView()
    spacing: 10

    // ROS(x 앞, y 왼쪽, z 위, m) → Qt Quick 3D 장면(y 위, 1 m = 100)
    function scenePos(x, y, z) { return Qt.vector3d(x * 100, z * 100, -y * 100) }
    // 두 점 사이를 2 cm 간격 점으로 채운다(선처럼 보이게)
    function dense(a, b) {
        const n = Math.max(1, Math.floor(b.minus(a).length() / 0.02)), out = []
        for (let i = 0; i <= n; ++i) out.push(a.plus(b.minus(a).times(i / n)))
        return out
    }

    // 바닥(z = 0) 위 클릭 위치 (odom 프레임 m), 바닥이 아니면 null
    function floorAt(x, y) {
        const r = view.pick(x, y)
        return r.objectHit === floor ? Qt.vector3d(r.scenePosition.x / 100, -r.scenePosition.z / 100, 0.015) : null
    }
    // 추적 · 위에서 시점에서 조작을 시작하면 지금 보이는 각도 그대로 자유 시점으로 넘어간다
    function enterFree() {
        if (cam === "자유")
            return
        camDist = cam === "추적" ? 100 * Math.hypot(1.3, 0.9) : 400
        camYaw = viewYaw
        camPitch = viewPitch
        panOffset = Qt.vector3d(0, 0, 0)
        cam = "자유"
    }
    // 뷰 큐브 · 메뉴의 정해진 각도 [pitch, yaw] (앞 = odom +x, 왼쪽 = odom +y)
    readonly property var views: ({
        "위": [-90, 90], "아래": [90, 90], "앞": [0, 90], "뒤": [0, -90], "왼쪽": [0, 180], "오른쪽": [0, 0], "홈": [-30, 45]
    })
    function setView(name) {
        enterFree()
        const v = views[name]
        viewAnim.stop()
        yawAnim.to = v[1] + 360 * Math.round((camYaw - v[1]) / 360)   // 가까운 쪽으로 돈다
        pitchAnim.to = v[0]
        if (name === "홈")
            panOffset = Qt.vector3d(0, 0, 0), camDist = 250
        viewAnim.start()
    }
    ParallelAnimation {
        id: viewAnim
        NumberAnimation { id: yawAnim; target: root; property: "camYaw"; duration: 300; easing.type: Easing.OutCubic }
        NumberAnimation { id: pitchAnim; target: root; property: "camPitch"; duration: 300; easing.type: Easing.OutCubic }
    }

    // 카메라 · 표시 레이어는 다음 실행에도 그대로
    Settings {
        category: "view"
        property alias cam: root.cam
        property alias camYaw: root.camYaw
        property alias camPitch: root.camPitch
        property alias camDist: root.camDist
        property alias scanLayer: scanLayer.checked
        property alias pathLayer: pathLayer.checked
        property alias axesLayer: axesLayer.checked
        property alias mapLayer: mapLayer.checked
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "3D 뷰"; color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Btn {
            text: "이동 목표"; kind: root.tool === "목표" ? "accent" : "normal"; implicitHeight: 36; font.pixelSize: Theme.fsBody
            onClicked: root.tool = root.tool === "목표" ? "" : "목표"
        }
        Btn {
            text: "거리 측정"; kind: root.measuring ? "accent" : "normal"; implicitHeight: 36; font.pixelSize: Theme.fsBody
            onClicked: { root.tool = root.measuring ? "" : "측정"; root.measureA = root.measureB = null }
        }
        Seg { options: ["추적", "위에서", "자유"]; current: root.cam; onPicked: (v) => root.cam = v }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.rLg
        color: Theme.canvas3d
        border.color: Theme.borderDefault
        clip: true

        View3D {
            id: view
            anchors.fill: parent
            anchors.margins: 1
            camera: root.cam === "위에서" ? topCam : root.cam === "자유" ? freeCam : followCam
            // 연결이 끊기면 장면을 회색으로 (로봇은 마지막 위치에 멈춰 있다)
            layer.enabled: robot.linkLost
            layer.effect: MultiEffect { saturation: -1; brightness: -0.15 }

            environment: SceneEnvironment {
                backgroundMode: SceneEnvironment.Color
                clearColor: Theme.canvas3d
                antialiasingMode: SceneEnvironment.MSAA
                antialiasingQuality: SceneEnvironment.High
                tonemapMode: SceneEnvironment.TonemapModeFilmic
                lightProbe: Texture { textureData: ProceduralSkyTextureData {} }
                probeExposure: 1.6
                aoEnabled: true
                aoStrength: 50
                aoDistance: 6
                InfiniteGrid { gridInterval: 100 }
            }

            DirectionalLight {
                eulerRotation: Qt.vector3d(-55, -35, 0)
                brightness: 2.0
                castsShadow: true
                shadowMapQuality: Light.ShadowMapQualityHigh
                shadowFactor: 55
                shadowMapFar: 3000
            }

            // ROS 프레임: 이 안의 모든 좌표는 odom 기준 m
            Node {
                id: world
                eulerRotation.x: -90
                scale: Qt.vector3d(100, 100, 100)

                Model {   // 그림자를 받는 바닥 (격자 바로 아래), 거리 측정 때 클릭 위치를 찾는다
                    id: floor
                    source: "#Rectangle"
                    pickable: true
                    position.z: -0.002
                    scale: Qt.vector3d(1, 1, 1)
                    materials: PrincipledMaterial { baseColor: Theme.matGround; roughness: 0.95; metalness: 0 }
                    receivesShadows: true
                }

                Model {
                    visible: mapLayer.checked && robot.hasMap
                    source: "#Rectangle"
                    position: robot.mapCenter
                    eulerRotation.z: robot.mapYaw
                    scale: Qt.vector3d(robot.mapSize.width / 100, robot.mapSize.height / 100, 1)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaMode: PrincipledMaterial.Blend
                        baseColorMap: Texture {
                            textureData: MapTexture { image: robot.mapImage }
                            magFilter: Texture.Nearest
                        }
                    }
                }

                component Dots: Model {
                    property alias points: inst.points
                    property alias size: inst.size
                    property alias color: inst.color
                    source: "#Cube"
                    castsShadows: false
                    visible: inst.points.length > 0
                    instancing: PointInstancing { id: inst }
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "white" }
                }

                Dots { visible: pathLayer.checked && robot.sensorOn.odom && points.length > 0; points: robot.trail; size: 0.02; color: Theme.trail }
                Dots { visible: pathLayer.checked && points.length > 0; points: robot.plan; size: 0.02; color: Theme.plan }
                Dots { visible: pathLayer.checked && points.length > 0; points: robot.targetLine; size: 0.018; color: Theme.plan }
                Dots { points: root.measureA && root.measureB ? root.dense(root.measureA, root.measureB) : []; size: 0.015; color: Theme.goalDrag }
                Dots { points: [root.measureA, root.measureB].filter(p => p); size: 0.04; color: Theme.goalDrag }

                Node {   // Nav2 이동 목표 화살표 (끄는 동안 노랑, 보낸 뒤 초록)
                    visible: root.goalA !== null
                    position: root.goalA ?? Qt.vector3d(0, 0, 0)
                    eulerRotation.z: root.goalYaw * 180 / Math.PI
                    readonly property PrincipledMaterial mat: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting; baseColor: root.goalDragging ? Theme.goalDrag : Theme.ok
                    }
                    Model { source: "#Sphere"; scale: Qt.vector3d(0.0005, 0.0005, 0.0005); materials: parent.mat; castsShadows: false }
                    Model { source: "#Cylinder"; position.x: 0.15; eulerRotation.z: -90; scale: Qt.vector3d(0.0002, 0.003, 0.0002); materials: parent.mat; castsShadows: false }
                    Model { source: "#Cone"; position.x: 0.30; eulerRotation.z: -90; scale: Qt.vector3d(0.0008, 0.0012, 0.0008); materials: parent.mat; castsShadows: false }
                }

                Node {
                    id: base
                    position: Qt.vector3d(robot.x, robot.y, 0)
                    eulerRotation.z: robot.theta * 180 / Math.PI

                    RobotModel {
                        id: model
                        wheelAngleL: robot.wheelAngleL
                        wheelAngleR: robot.wheelAngleR
                        bodyUrl: robot.modelFile("body")
                        wheelLUrl: robot.modelFile("wheel_left")
                        wheelRUrl: robot.modelFile("wheel_right")
                        lidarUrl: robot.modelFile("lidar_head")
                        parts: robot.modelParts()
                        lidarHz: robot.lidarHz
                    }

                    Dots {   // LiDAR 점 (scan 프레임 = base_link, 라이다 헤드 높이에 그림)
                        visible: scanLayer.checked && robot.sensorOn.scan && points.length > 0
                        position.z: model.lidarPos.z
                        points: robot.scanPoints
                        size: 0.018
                        color: Theme.lidar
                    }

                    Node {   // 좌표축 x 빨강 · y 초록 · z 파랑 (0.3 m)
                        visible: axesLayer.checked && robot.sensorOn.tf
                        position.z: 0.22
                        Model { source: "#Cube"; position.x: 0.15; scale: Qt.vector3d(0.003, 0.0001, 0.0001); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: Theme.axisX } }
                        Model { source: "#Cube"; position.y: 0.15; scale: Qt.vector3d(0.0001, 0.003, 0.0001); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: Theme.axisY } }
                        Model { source: "#Cube"; position.z: 0.15; scale: Qt.vector3d(0.0001, 0.0001, 0.003); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: Theme.axisZ } }
                    }
                }
            }

            // 추적: 로봇 뒤 1.3 m · 위 0.9 m 에서 로봇을 바라본다
            PerspectiveCamera {
                id: followCam
                position: root.scenePos(robot.x - 1.3 * Math.cos(robot.theta), robot.y - 1.3 * Math.sin(robot.theta), 0.9)
                lookAtNode: base
                clipNear: 1
                clipFar: 20000
            }
            // 위에서: 로봇 위 4 m, 화면 오른쪽이 로봇 앞(odom x)
            PerspectiveCamera {
                id: topCam
                position: root.scenePos(robot.x, robot.y, 4)
                eulerRotation.x: -90
                clipNear: 1
                clipFar: 20000
            }
            // 자유: 로봇(+ 끌어서 옮긴 만큼)을 중심으로 회전 · 이동 · 확대
            Node {
                id: orbit
                position: root.scenePos(robot.x, robot.y, 0).plus(root.panOffset)
                eulerRotation: Qt.vector3d(root.camPitch, root.camYaw, 0)
                PerspectiveCamera { id: freeCam; position: Qt.vector3d(0, 0, root.camDist); clipNear: 1; clipFar: 20000 }
            }
        }

        // Fusion 360 방식 조작: 휠 확대 · 가운데 버튼 끌기 이동 · Shift+가운데 회전.
        // 가운데 버튼이 없을 때: 왼쪽 끌기(또는 Shift+왼쪽) 회전 · Ctrl+왼쪽 이동 · Alt+왼쪽 확대
        MouseArea {
            anchors.fill: view
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            property var drag: null
            onPressed: (m) => {
                root.clickedView()
                if (m.button === Qt.LeftButton && root.tool === "목표") {
                    const p = root.floorAt(m.x, m.y)
                    if (p) root.goalA = p, root.goalYaw = robot.theta, root.goalDragging = true
                    drag = null
                    return
                }
                const shift = m.modifiers & Qt.ShiftModifier
                drag = { x: m.x, y: m.y, moved: false, left: m.button === Qt.LeftButton,
                         mode: m.button === Qt.MiddleButton ? (shift ? "orbit" : "pan")
                             : (m.modifiers & Qt.ControlModifier) ? "pan" : (m.modifiers & Qt.AltModifier) ? "zoom" : "orbit" }
            }
            onPositionChanged: (m) => {
                if (root.goalDragging) {   // RViz 처럼 누른 곳에서 끄는 쪽이 바라볼 방향
                    const p = root.floorAt(m.x, m.y)
                    if (p && p.minus(root.goalA).length() > 0.03)
                        root.goalYaw = Math.atan2(p.y - root.goalA.y, p.x - root.goalA.x)
                    return
                }
                if (!drag) return
                const dx = m.x - drag.x, dy = m.y - drag.y
                if (!drag.moved && Math.hypot(dx, dy) < 4) return
                drag.moved = true
                drag.x = m.x; drag.y = m.y
                root.enterFree()
                viewAnim.stop()
                if (drag.mode === "orbit") {
                    root.camYaw -= dx * 0.3
                    root.camPitch = Math.max(-90, Math.min(90, root.camPitch - dy * 0.3))
                } else if (drag.mode === "pan") {
                    const k = root.camDist * 2 * Math.tan(freeCam.fieldOfView * Math.PI / 360) / view.height
                    root.panOffset = root.panOffset.plus(orbit.right.times(-dx * k)).plus(orbit.up.times(dy * k))
                } else {
                    root.camDist = Math.max(20, Math.min(5000, root.camDist * Math.pow(1.005, dy)))
                }
            }
            onReleased: (m) => {
                if (root.goalDragging) {
                    root.goalDragging = false
                    robot.sendGoal(root.goalA.x, root.goalA.y, root.goalYaw)
                    root.tool = ""   // RViz 처럼 한 번 보내면 도구가 꺼진다
                    return
                }
                if (drag && !drag.moved && drag.left && root.measuring) {
                    const p = root.floorAt(m.x, m.y)
                    if (p && (!root.measureA || root.measureB))
                        root.measureA = p, root.measureB = null
                    else if (p)
                        root.measureB = p
                }
                drag = null
            }
            onWheel: (w) => {
                root.enterFree()
                root.camDist = Math.max(20, Math.min(5000, root.camDist * Math.pow(0.999, w.angleDelta.y)))
            }
        }

        // ---- 표시 레이어 ----
        Column {
            x: 14; y: 14
            spacing: 6
            component Layer: Rectangle {
                property alias checked: c.checked
                property alias text: c.text
                radius: Theme.rMd
                color: Theme.hudFill
                width: c.implicitWidth + 16
                height: 32
                CheckBox { id: c; anchors.centerIn: parent; checked: true; focusPolicy: Qt.NoFocus; font.pixelSize: Theme.fsBody
                    contentItem: Text { text: c.text; font: c.font; color: Theme.hudText1; leftPadding: c.indicator.width + 6; verticalAlignment: Text.AlignVCenter } }
            }
            Layer { id: scanLayer; text: "LiDAR 점" }
            Layer { id: pathLayer; text: "이동 경로" }
            Layer { id: axesLayer; text: "좌표축" }
            Layer { id: mapLayer; text: "지도"; checked: false; onCheckedChanged: if (root.robot) root.robot.mapWanted = checked }
        }

        // ---- 자세 · 축척 ----
        Row {
            x: 14
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            spacing: 10
            Rectangle {
                radius: Theme.rLg; color: Theme.hudFill
                width: pose.implicitWidth + 28; height: pose.implicitHeight + 20
                Column {
                    id: pose
                    anchors.centerIn: parent
                    spacing: 2
                    Text { color: Theme.hudText1; font.family: root.mono; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                        text: "x <b>" + robot.x.toFixed(3) + "</b> m · y <b>" + robot.y.toFixed(3) + "</b> m" }
                    Text { color: Theme.hudText1; font.family: root.mono; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                        text: "θ <b>" + (robot.theta * 180 / Math.PI).toFixed(1) + "°</b> · v <b>" + robot.v.toFixed(3) + "</b> m/s · ω <b>" + robot.w.toFixed(2) + "</b> rad/s" }
                }
            }
            Rectangle {   // 위에서 볼 때만 축척이 맞다
                visible: root.cam === "위에서"
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.rLg; color: Theme.hudFill
                width: bar.width + 60; height: 36
                readonly property real pxPerM: view.height / (2 * 4 * Math.tan(topCam.fieldOfView * Math.PI / 360))
                Rectangle { id: bar; x: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.pxPerM * 0.5; height: 4; color: Theme.hudText1 }
                Text { anchors.left: bar.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "0.5 m"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall }
            }
        }

        // ---- 거리 측정 · 이동 목표 안내 ----
        Rectangle {
            visible: root.tool !== "" || root.goalA !== null
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            radius: Theme.rLg; color: Theme.hudFill; border.color: Theme.warnSubtle
            width: mt.implicitWidth + 28; height: mt.implicitHeight + 20
            Text {
                id: mt
                anchors.centerIn: parent
                color: Theme.goalDrag; font.family: root.mono; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                text: root.tool === "목표" && !root.goalDragging ? "목표 위치를 누른 채 바라볼 방향으로 끌고 놓으세요 (Nav2)"
                    : !root.measuring ? (root.goalA ? "이동 목표 (" + root.goalA.x.toFixed(2) + ", " + root.goalA.y.toFixed(2) + ") m · "
                      + (root.goalYaw * 180 / Math.PI).toFixed(0) + "°" + (root.goalDragging ? "" : " · 보냄") : "")
                    : !root.measureA ? "바닥을 클릭해 시작점을 찍으세요"
                    : !root.measureB ? "끝점을 클릭하세요"
                    : "거리 <b>" + root.measureLen.toFixed(3) + "</b> m · Δx " + (root.measureB.x - root.measureA.x).toFixed(3)
                      + " · Δy " + (root.measureB.y - root.measureA.y).toFixed(3) + "<br>다시 클릭하면 새로 잽니다"
            }
        }

        // ---- 뷰 큐브: 지금 각도를 보여 주고, 면을 누르면 그 방향으로 · 오른쪽 클릭은 뷰 메뉴 ----
        Item {
            id: cube
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            width: 110; height: 110
            View3D {
                id: cubeView
                anchors.fill: parent
                environment: SceneEnvironment { backgroundMode: SceneEnvironment.Transparent; antialiasingMode: SceneEnvironment.MSAA }
                Node {
                    eulerRotation: Qt.vector3d(root.viewPitch, root.viewYaw, 0)
                    PerspectiveCamera { position: Qt.vector3d(0, 0, 330); fieldOfView: 30 }
                }
                DirectionalLight { eulerRotation: Qt.vector3d(-30, 30, 0); ambientColor: Theme.sceneAmbient }
                component Face: Model {
                    id: face
                    property string label
                    source: "#Rectangle"
                    pickable: true
                    materials: PrincipledMaterial {
                        roughness: 0.8
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 128; height: 128
                                color: Theme.viewCubeFace; border.color: Theme.viewCubeEdge; border.width: 4
                                Text { anchors.centerIn: parent; text: face.label; color: Theme.hudText1; font.pixelSize: Theme.fsMetric; font.bold: true }
                            }
                        }
                    }
                }
                Face { label: "위"; position: Qt.vector3d(0, 50, 0); eulerRotation: Qt.vector3d(-90, 90, 0) }
                Face { label: "아래"; position: Qt.vector3d(0, -50, 0); eulerRotation: Qt.vector3d(90, 90, 0) }
                Face { label: "앞"; position: Qt.vector3d(50, 0, 0); eulerRotation: Qt.vector3d(0, 90, 0) }
                Face { label: "뒤"; position: Qt.vector3d(-50, 0, 0); eulerRotation: Qt.vector3d(0, -90, 0) }
                Face { label: "오른쪽"; position: Qt.vector3d(0, 0, 50) }
                Face { label: "왼쪽"; position: Qt.vector3d(0, 0, -50); eulerRotation: Qt.vector3d(0, 180, 0) }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (m) => {
                    if (m.button === Qt.RightButton)
                        return viewMenu.popup()
                    const r = cubeView.pick(m.x, m.y)
                    if (r.objectHit) root.setView(r.objectHit.label)
                }
            }
            Menu {
                id: viewMenu
                Repeater {
                    model: ["홈", "위", "아래", "앞", "뒤", "왼쪽", "오른쪽"]
                    MenuItem { required property string modelData; text: modelData === "홈" ? "홈 (기본 각도)" : modelData + " 뷰"; onTriggered: root.setView(modelData) }
                }
            }
        }

        // ---- 범례 ----
        Rectangle {
            anchors.right: parent.right
            anchors.top: cube.bottom
            anchors.margins: 14
            radius: Theme.rLg; color: Theme.hudFill
            width: legend.implicitWidth + 24; height: legend.implicitHeight + 16
            Column {
                id: legend
                anchors.centerIn: parent
                spacing: 4
                Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.trail; anchors.verticalCenter: parent.verticalCenter } Text { text: "이동 경로"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.plan; anchors.verticalCenter: parent.verticalCenter } Text { text: "목표 / 계획 경로"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                Row { spacing: 6; Rectangle { width: 8; height: 8; radius: Theme.rXs; color: Theme.lidar; anchors.verticalCenter: parent.verticalCenter } Text { text: "LiDAR 점"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.ok; anchors.verticalCenter: parent.verticalCenter } Text { text: "이동 목표 (Nav2)"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                Text { text: "휠 확대 · 가운데 끌기 이동 · Shift+가운데 회전\n왼쪽 끌기 회전 · Ctrl 이동 · Alt 확대"; color: Theme.hudText2; font.pixelSize: Theme.fsCaption }
                Text { visible: model.bodyUrl.toString() === ""; text: "기본 모델 (model/ 폴더에 Fusion 360 내보내기 파일을 넣으면 교체)"; color: Theme.hudText2; font.pixelSize: Theme.fsCaption }
            }
        }

        // ---- 연결 끊김 안내 ----
        Rectangle {
            visible: robot.linkLost
            anchors.centerIn: parent
            width: 320; height: 110; radius: Theme.rLg
            color: Theme.dangerSubtle; border.color: Theme.danger
            Column {
                anchors.centerIn: parent
                spacing: 6
                Text { text: "로봇과 연결이 끊겼습니다"; color: Theme.dangerText; font.pixelSize: Theme.fsTitle; font.bold: true }
                Text { text: "파이 워치독이 0.5 s 안에 로봇을 세웁니다.\n3 s 마다 다시 연결합니다."; color: Theme.dangerText; font.pixelSize: Theme.fsSmall }
            }
        }
    }
}
