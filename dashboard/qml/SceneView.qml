import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick3D
import QtQuick3D.Helpers
import RobotDashboard

// 3D 뷰: 바닥 격자 · 로봇 모델 · LiDAR 점 · 이동 경로 · 목표/계획 경로 · 좌표축 · 지도
ColumnLayout {
    id: root
    required property Robot robot
    property string cam: "추적"
    readonly property string mono: "Consolas"
    signal clickedView()
    spacing: 10

    // ROS(x 앞, y 왼쪽, z 위, m) → Qt Quick 3D 장면(y 위, 1 m = 100)
    function scenePos(x, y, z) { return Qt.vector3d(x * 100, z * 100, -y * 100) }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "3D 뷰"; color: "#E6E9EE"; font.pixelSize: 16; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Seg { options: ["추적", "위에서", "자유"]; current: root.cam; onPicked: (v) => root.cam = v }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 14
        color: "#0B0E12"
        border.color: "#2B323C"
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
                clearColor: "#0F141B"
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

                Model {   // 그림자를 받는 바닥 (격자 바로 아래)
                    source: "#Rectangle"
                    position.z: -0.002
                    scale: Qt.vector3d(1, 1, 1)
                    materials: PrincipledMaterial { baseColor: "#121820"; roughness: 0.95; metalness: 0 }
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

                Dots { visible: pathLayer.checked && robot.sensorOn.odom && points.length > 0; points: robot.trail; size: 0.02; color: "#4CC3E8" }
                Dots { visible: pathLayer.checked && points.length > 0; points: robot.plan; size: 0.02; color: "#F2A33A" }
                Dots { visible: pathLayer.checked && points.length > 0; points: robot.targetLine; size: 0.018; color: "#F2A33A" }

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
                    }

                    Dots {   // LiDAR 점 (scan 프레임 = base_link, 라이다 높이에 그림)
                        visible: scanLayer.checked && robot.sensorOn.scan && points.length > 0
                        position.z: model.lidarHeight
                        points: robot.scanPoints
                        size: 0.018
                        color: "#E0457B"
                    }

                    Node {   // 좌표축 x 빨강 · y 초록 · z 파랑 (0.3 m)
                        visible: axesLayer.checked && robot.sensorOn.tf
                        position.z: 0.22
                        Model { source: "#Cube"; position.x: 0.15; scale: Qt.vector3d(0.003, 0.0001, 0.0001); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#E5484D" } }
                        Model { source: "#Cube"; position.y: 0.15; scale: Qt.vector3d(0.0001, 0.003, 0.0001); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#5BC48A" } }
                        Model { source: "#Cube"; position.z: 0.15; scale: Qt.vector3d(0.0001, 0.0001, 0.003); materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#4C8DF0" } }
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
            // 자유: 로봇을 중심으로 드래그 회전 · 휠 확대
            Node {
                id: orbit
                position: root.scenePos(robot.x, robot.y, 0)
                eulerRotation.x: -30
                PerspectiveCamera { id: freeCam; position: Qt.vector3d(0, 0, 250); clipNear: 1; clipFar: 20000 }
            }
        }

        OrbitCameraController {
            anchors.fill: view
            origin: orbit
            camera: freeCam
            enabled: root.cam === "자유"
        }
        MouseArea {
            anchors.fill: view
            acceptedButtons: Qt.LeftButton
            propagateComposedEvents: true
            onPressed: (m) => { root.clickedView(); m.accepted = root.cam !== "자유" ? true : false }
        }

        // ---- 표시 레이어 ----
        Column {
            x: 14; y: 14
            spacing: 6
            component Layer: Rectangle {
                property alias checked: c.checked
                property alias text: c.text
                radius: 8
                color: "#E6161A21"
                width: c.implicitWidth + 16
                height: 32
                CheckBox { id: c; anchors.centerIn: parent; checked: true; focusPolicy: Qt.NoFocus; font.pixelSize: 13
                    contentItem: Text { text: c.text; font: c.font; color: "#E6E9EE"; leftPadding: c.indicator.width + 6; verticalAlignment: Text.AlignVCenter } }
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
                radius: 10; color: "#E6161A21"
                width: pose.implicitWidth + 28; height: pose.implicitHeight + 20
                Column {
                    id: pose
                    anchors.centerIn: parent
                    spacing: 2
                    Text { color: "#E6E9EE"; font.family: root.mono; font.pixelSize: 13; textFormat: Text.StyledText
                        text: "x <b>" + robot.x.toFixed(3) + "</b> m · y <b>" + robot.y.toFixed(3) + "</b> m" }
                    Text { color: "#E6E9EE"; font.family: root.mono; font.pixelSize: 13; textFormat: Text.StyledText
                        text: "θ <b>" + (robot.theta * 180 / Math.PI).toFixed(1) + "°</b> · v <b>" + robot.v.toFixed(3) + "</b> m/s · ω <b>" + robot.w.toFixed(2) + "</b> rad/s" }
                }
            }
            Rectangle {   // 위에서 볼 때만 축척이 맞다
                visible: root.cam === "위에서"
                anchors.verticalCenter: parent.verticalCenter
                radius: 10; color: "#E6161A21"
                width: bar.width + 60; height: 36
                readonly property real pxPerM: view.height / (2 * 4 * Math.tan(topCam.fieldOfView * Math.PI / 360))
                Rectangle { id: bar; x: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.pxPerM * 0.5; height: 4; color: "#E6E9EE" }
                Text { anchors.left: bar.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "0.5 m"; color: "#C5CBD3"; font.pixelSize: 12 }
            }
        }

        // ---- 범례 ----
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            radius: 10; color: "#E6161A21"
            width: legend.implicitWidth + 24; height: legend.implicitHeight + 16
            Column {
                id: legend
                anchors.centerIn: parent
                spacing: 4
                Row { spacing: 6; Rectangle { width: 18; height: 3; color: "#4CC3E8"; anchors.verticalCenter: parent.verticalCenter } Text { text: "이동 경로"; color: "#C5CBD3"; font.pixelSize: 12 } }
                Row { spacing: 6; Rectangle { width: 18; height: 3; color: "#F2A33A"; anchors.verticalCenter: parent.verticalCenter } Text { text: "목표 / 계획 경로"; color: "#C5CBD3"; font.pixelSize: 12 } }
                Row { spacing: 6; Rectangle { width: 8; height: 8; radius: 4; color: "#E0457B"; anchors.verticalCenter: parent.verticalCenter } Text { text: "LiDAR 점"; color: "#C5CBD3"; font.pixelSize: 12 } }
                Text { visible: model.bodyUrl.toString() === ""; text: "기본 모델 (model/ 폴더에 Fusion 360 내보내기 파일을 넣으면 교체)"; color: "#9AA3AF"; font.pixelSize: 11 }
            }
        }

        // ---- 연결 끊김 안내 ----
        Rectangle {
            visible: robot.linkLost
            anchors.centerIn: parent
            width: 320; height: 110; radius: 12
            color: "#2A1416"; border.color: "#D93A40"
            Column {
                anchors.centerIn: parent
                spacing: 6
                Text { text: "로봇과 연결이 끊겼습니다"; color: "#F4B4B6"; font.pixelSize: 16; font.bold: true }
                Text { text: "파이 워치독이 0.5 s 안에 로봇을 세웁니다.\n3 s 마다 다시 연결합니다."; color: "#E7A2A4"; font.pixelSize: 12 }
            }
        }
    }
}
