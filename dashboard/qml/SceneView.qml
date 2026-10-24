import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick3D
import QtQuick3D.Helpers
import RobotDashboard

// 3D 뷰: 바닥 격자 · 로봇 모델 · LiDAR 점(잔상) · 이동 경로 · 목표/계획 경로 · 좌표축 · 지도 · 코스트맵 · footprint
// · TF 축 · AMCL 공분산 · 거리 측정 · 이동 목표 · 초기 위치
ColumnLayout {
    id: root
    required property Robot robot
    property string cam: "추적"   // 추적 · 3인칭 · 위에서 · 자유
    property string tool: ""   // "" · "측정" · "목표" · "초기 위치"
    readonly property bool measuring: tool === "측정"
    property var measureA: null   // 측정 점 (odom 프레임, m)
    property var measureB: null
    readonly property real measureLen: measureA && measureB ? measureB.minus(measureA).length() : 0
    property var goalA: null      // Nav2 이동 목표 (odom 프레임, m)
    property real goalYaw: 0      // rad
    property bool goalDragging: false
    property string goalError: ""   // 목표를 보내지 않은 이유(잠김 · 정지 모드 · 끊김), 몇 초 보이고 사라진다
    Timer { id: goalErrorTimer; interval: 6000; onTriggered: root.goalError = "" }
    property var initA: null      // AMCL 초기 위치 (끄는 동안, odom 프레임 m)
    property real initYaw: 0      // rad
    property bool initDragging: false
    property var cursorFloor: null   // 커서 아래 바닥 점 (odom 프레임 m)
    // 자유 시점: 로봇 + panOffset 을 중심으로 camYaw · camPitch(deg) 각도, camDist(1 m = 100) 거리에서 본다
    property real camYaw: 0
    property real camPitch: -30
    property real camDist: 250
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    // 3인칭 추적: 로봇 yaw 를 따라 돌고, 사용자가 돌린 만큼(tpYaw · tpPitch · tpDist)은 그대로 둔다
    property real tpYaw: 0
    property real tpPitch: -25
    property real tpDist: 180
    // 지금 보이는 카메라의 각도 (뷰 큐브 표시 · 자유 시점으로 넘어갈 때 시작 각도)
    readonly property real viewYaw: cam === "추적" ? Math.atan2(-Math.cos(robot.theta), Math.sin(robot.theta)) * 180 / Math.PI
                                  : cam === "3인칭" ? robot.theta * 180 / Math.PI - 90 + tpYaw
                                  : cam === "위에서" ? 0 : camYaw
    readonly property real viewPitch: cam === "추적" ? -Math.atan2(0.9, 1.3) * 180 / Math.PI : cam === "3인칭" ? tpPitch
                                    : cam === "위에서" ? -90 : camPitch
    readonly property string mono: Theme.monoFamily
    readonly property string modelLoad: model.bodyLoad   // 로봇 모델(glb) 읽기 결과 — state 의 scene.modelLoad

    // ---- 표시 레이어 (레이어 패널 · 시험 동작이 바꾸고 Settings 에 저장) ----
    property bool layerPanelOpen: false     // 기본 접힘(3D 를 덜 가리게), 마지막 상태는 Settings 가 기억
    property bool legendOpen: false
    property bool showScan: true
    property real scanDecay: 0              // 잔상 s (0 · 1 · 3 · 10), 0 이면 robot.scanPoints 를 그대로 그린다
    property string scanColorMode: "flat"   // flat · range · intensity
    property real scanPointSize: 0.018      // m
    property bool showTrail: true           // 이동 경로 · 계획 경로 · 직진 목표선
    property bool trailGradient: false
    property bool showAxes: true
    property bool showMap: true             // RViz2 처럼 기본 켬(/map 구독). robot.mapWanted 와 같이 움직인다
    property real mapOpacity: 0.7           // RViz2 Map 디스플레이 기본 Alpha
    readonly property bool mapFilterNearest: mapPlane.nearest   // 칸이 번지지 않게 최근접(시험 판정용)
    readonly property bool noMapHintShown: showMap && !robot.hasMap   // '지도 없음' 안내

    // /odom · /wheel_states 는 표시용 25 Hz(throttle 40 ms)로 온다. 3D 로봇 · 따라가는 카메라 · 바퀴는 새 값까지
    // 40 ms 동안 선형 보간해 프레임마다 움직인다. 각도는 감기지 않게(±180° · 2π 를 넘을 때 거꾸로 돌지 않게) 이어 붙인다.
    function unwrapTo(cur, target, period) { return cur + ((target - cur) % period + 1.5 * period) % period - period / 2 }
    property vector3d drawPos: Qt.vector3d(robot.x, robot.y, 0)
    Behavior on drawPos { Vector3dAnimation { duration: 40 } }
    property real drawYaw: 0         // 도
    property real drawWheelL: 0      // rad
    property real drawWheelR: 0
    Behavior on drawYaw { NumberAnimation { duration: 40 } }
    Behavior on drawWheelL { NumberAnimation { duration: 40 } }
    Behavior on drawWheelR { NumberAnimation { duration: 40 } }
    readonly property real rawYaw: robot.theta * 180 / Math.PI
    onRawYawChanged: drawYaw = unwrapTo(drawYaw, rawYaw, 360)
    readonly property real rawWheelL: robot.wheelAngleL
    readonly property real rawWheelR: robot.wheelAngleR
    onRawWheelLChanged: drawWheelL = unwrapTo(drawWheelL, rawWheelL, 2 * Math.PI)
    onRawWheelRChanged: drawWheelR = unwrapTo(drawWheelR, rawWheelR, 2 * Math.PI)
    // 시험 판정용: 직전 1 s 동안 그린 로봇 위치가 바뀐 횟수(drawHz)와 받은 위치가 바뀐 횟수(poseHz)
    property int drawHz: 0
    property int poseHz: 0
    property int drawN: 0
    property int poseN: 0
    onDrawPosChanged: ++drawN
    readonly property real rawX: robot.x
    onRawXChanged: ++poseN
    Timer { interval: 1000; repeat: true; running: true; onTriggered: { root.drawHz = root.drawN; root.poseHz = root.poseN; root.drawN = 0; root.poseN = 0 } }
    property bool mapDrawBehind: false
    property bool showGlobalCostmap: false
    property bool showLocalCostmap: false
    property bool showFootprint: false
    property bool showTf: false
    property bool tfLabels: true
    property real tfScale: 1
    property bool showAmcl: false
    property bool showOdomCov: false
    property string bookmarks: "[]"         // 저장된 뷰 JSON 배열 [{name, mode, pivot:[x,y,z], dist, yaw, pitch}]
    // ---- 판정용 요약 (state 덤프의 scene 항목) ----
    readonly property bool costmapReceived: globalCostmap.received || localCostmap.received
    readonly property bool footprintReceived: footprintLayer.received
    readonly property int tfFrameCount: tfTree.frameCount
    readonly property int scanAccumPoints: scanAcc.pointCount
    readonly property bool amclReceived: amclPose.received
    // 지도 판 확인: 점유 셀이 있다고 알려진 점(odom m, 시험 동작이 바꾼다)을 렌더 경로(지도 판 노드 변환)로 uv 로 바꿔
    // 그 텍스처 픽셀이 점유(검정)인지, 판 위 그 점과 같은 xy 의 라이다 높이 점이 화면에서 몇 px 떨어지는지 본다.
    property real mapProbeX: 2.975          // 기본 = 모의 서버 기본 방 오른쪽 벽 셀(x 2.95..3.0)
    property real mapProbeY: 1.0
    property real mapProbeU: -1
    property real mapProbeV: -1
    property bool mapProbeOccupied: false
    property real mapProbeErrPx: -1
    // 위에서 시점: 1 m 가 몇 px 인지(직교 카메라라 높이와 상관없이 같다). 4 m 위 시야 60° 와 같은 넓이.
    readonly property real topPxPerM: Math.max(1, view.height) / (8 * Math.tan(Math.PI / 6))
    signal clickedView()
    spacing: 10

    // ROS(x 앞, y 왼쪽, z 위, m) → Qt Quick 3D 장면(y 위, 1 m = 100)
    function scenePos(x, y, z) { return Qt.vector3d(x * 100, z * 100, -y * 100) }
    // 장면 좌표 → ROS m
    function rosPos(v) { return Qt.vector3d(v.x / 100, -v.z / 100, v.y / 100) }
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
    // 추적 · 3인칭 · 위에서 시점에서 조작을 시작하면 지금 보이는 각도 그대로 자유 시점으로 넘어간다
    function enterFree() {
        if (cam === "자유")
            return
        camDist = cam === "추적" ? 100 * Math.hypot(1.3, 0.9) : cam === "3인칭" ? tpDist : 400
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
    // 더블클릭한 바닥 점을 회전 중심으로 (Focus)
    function focusAt(p) {
        enterFree()
        viewAnim.stop()
        panOffset = scenePos(p.x - robot.x, p.y - robot.y, 0)
    }
    // 저장된 뷰(카메라 북마크): 모드 · 피벗(odom m) · 거리 · yaw · pitch
    function bookmarkList() {
        try { return JSON.parse(bookmarks) } catch (e) { return [] }
    }
    function saveBookmark(name) {
        const list = bookmarkList(), tp = cam === "3인칭", off = rosPos(panOffset)
        list.push({ name: name || ("뷰 " + (list.length + 1)), mode: cam, pivot: [robot.x + off.x, robot.y + off.y, off.z],
                    dist: tp ? tpDist : camDist, yaw: tp ? tpYaw : camYaw, pitch: tp ? tpPitch : camPitch })
        bookmarks = JSON.stringify(list)
    }
    function applyBookmark(i) {
        const b = bookmarkList()[i]
        if (!b)
            return
        viewAnim.stop()
        cam = b.mode
        if (b.mode === "자유") {
            camDist = b.dist; camYaw = b.yaw; camPitch = b.pitch
            panOffset = scenePos(b.pivot[0] - robot.x, b.pivot[1] - robot.y, b.pivot[2])
        } else if (b.mode === "3인칭") {
            tpDist = b.dist; tpYaw = b.yaw; tpPitch = b.pitch
        }
    }
    function updateMapProbe() {
        const s = world.mapPositionToScene(Qt.vector3d(mapProbeX, mapProbeY, 0))
        const l = mapPlane.mapPositionFromScene(s)   // 판 메시 좌표 -50..50 (+y = 이미지 위)
        mapProbeU = l.x / 100 + 0.5
        mapProbeV = 0.5 - l.y / 100
        mapProbeOccupied = mapProbeU >= 0 && mapProbeU < 1 && mapProbeV >= 0 && mapProbeV < 1
                           && mapPlane.tex.pixelAt(mapProbeU, mapProbeV).hslLightness < 0.25
        const a = view.mapFrom3DScene(mapPlane.mapPositionToScene(Qt.vector3d(l.x, l.y, 0)))
        const b = view.mapFrom3DScene(world.mapPositionToScene(Qt.vector3d(mapProbeX, mapProbeY, model.lidarPos.z)))
        mapProbeErrPx = Math.hypot(a.x - b.x, a.y - b.y)
    }
    Timer { interval: 500; repeat: true; triggeredOnStart: true; running: root.showMap && robot.hasMap; onTriggered: root.updateMapProbe() }
    function removeBookmark(i) {
        const list = bookmarkList()
        list.splice(i, 1)
        bookmarks = JSON.stringify(list)
    }
    ParallelAnimation {
        id: viewAnim
        NumberAnimation { id: yawAnim; target: root; property: "camYaw"; duration: Theme.durCamera; easing.type: Easing.OutCubic }
        NumberAnimation { id: pitchAnim; target: root; property: "camPitch"; duration: Theme.durCamera; easing.type: Easing.OutCubic }
    }

    // 카메라 · 표시 레이어는 다음 실행에도 그대로
    Settings {
        category: "view"
        property alias cam: root.cam
        property alias camYaw: root.camYaw
        property alias camPitch: root.camPitch
        property alias camDist: root.camDist
        property alias tpYaw: root.tpYaw
        property alias tpPitch: root.tpPitch
        property alias tpDist: root.tpDist
        property alias bookmarks: root.bookmarks
        property alias scanLayer: root.showScan
        property alias pathLayer: root.showTrail
        property alias axesLayer: root.showAxes
        property alias mapLayerOn: root.showMap   // 예전 키 mapLayer 는 기본 꺼짐 시절 값이라 읽지 않는다
    }
    Settings {
        category: "layers"
        property alias panelOpen: root.layerPanelOpen
        property alias legendOpen: root.legendOpen
        property alias scanDecay: root.scanDecay
        property alias scanColorMode: root.scanColorMode
        property alias scanPointSize: root.scanPointSize
        property alias trailGradient: root.trailGradient
        property alias mapAlpha: root.mapOpacity   // 예전 키 mapOpacity(기본 1)는 읽지 않는다
        property alias mapDrawBehind: root.mapDrawBehind
        property alias globalCostmap: root.showGlobalCostmap
        property alias localCostmap: root.showLocalCostmap
        property alias footprint: root.showFootprint
        property alias tf: root.showTf
        property alias tfLabels: root.tfLabels
        property alias tfScale: root.tfScale
        property alias amcl: root.showAmcl
        property alias odomCov: root.showOdomCov
    }
    // 지도 레이어 = robot.mapWanted (시험 동작 · 다른 화면이 바꿔도 체크가 따라온다)
    onShowMapChanged: if (robot.mapWanted !== showMap) robot.mapWanted = showMap
    // 이 화면이 저장된 값(기본 켬)을 robot 에 넣기 전에 Main 의 connectTo(resetSession)가 내는 mapChanged 는 따라가지 않는다
    property bool mapSynced: false
    Connections {
        target: root.robot
        enabled: root.mapSynced
        function onMapChanged() { if (root.showMap !== root.robot.mapWanted) root.showMap = root.robot.mapWanted }
    }
    Component.onCompleted: { if (robot.mapWanted !== showMap) robot.mapWanted = showMap; mapSynced = true }

    // ---- 장면 데이터 (켤 때만 구독, RosBridge 공개 API 만 씀) ----
    ScanAccumulator {
        id: scanAcc
        bridge: root.robot.bridge
        // 잔상이 0 s 여도 돌려 /tf_static 장착 변환을 놓치지 않는다(그때 그리기는 robot.scanPoints)
        active: root.showScan && (root.robot.sensorOn.scan ?? false)
        robotX: root.robot.x
        robotY: root.robot.y
        robotTheta: root.robot.theta
        decaySec: root.scanDecay
        colorMode: root.scanColorMode
        color: Theme.lidar
    }
    CostmapLayer { id: globalCostmap; bridge: root.robot.bridge; topic: "/global_costmap/costmap"; throttleMs: 2000; active: root.showGlobalCostmap }
    CostmapLayer { id: localCostmap; bridge: root.robot.bridge; topic: "/local_costmap/costmap"; throttleMs: 1000; active: root.showLocalCostmap }
    FootprintLayer { id: footprintLayer; bridge: root.robot.bridge; active: root.showFootprint }
    TfTree { id: tfTree; bridge: root.robot.bridge; active: root.showTf; fixedFrame: "odom" }
    AmclPose { id: amclPose; bridge: root.robot.bridge; active: root.showAmcl }
    AmclPose { id: odomCov; bridge: root.robot.bridge; topic: "/odom"; active: root.showOdomCov }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp2
        Text { text: "3D 뷰"; color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        // 저장된 뷰(카메라 북마크): 메뉴 하나에 불러오기 · 지금 뷰 저장 · 삭제
        Btn {
            id: bmBtn
            readonly property var names: root.bookmarkList().map(b => b.name)
            text: "저장된 뷰 (" + names.length + ") ▾"
            size: "sm"
            onClicked: bmMenu.popup(bmBtn, 0, bmBtn.height)
            Menu {
                id: bmMenu
                Repeater {
                    model: bmBtn.names
                    MenuItem { required property string modelData; required property int index; text: modelData; onTriggered: root.applyBookmark(index) }
                }
                MenuSeparator { visible: bmBtn.names.length > 0 }
                MenuItem { text: "지금 뷰 저장"; onTriggered: root.saveBookmark("") }
                Menu {
                    title: "삭제"
                    enabled: bmBtn.names.length > 0
                    Repeater {
                        model: bmBtn.names
                        MenuItem { required property string modelData; required property int index; text: modelData; onTriggered: root.removeBookmark(index) }
                    }
                }
            }
        }
        Btn { text: "기본 시점"; size: "sm"; onClicked: root.setView("홈") }
        Btn { text: "지도 초기화"; size: "sm"; onClicked: robot.resetMap() }
        Item { implicitWidth: Theme.sp2 }
        Text { text: "도구"; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        Seg {
            // 화면 이름 → 도구 값(tool)
            readonly property var names: ({"없음": "", "거리 측정": "측정", "Nav2 목표": "목표", "초기 위치(AMCL)": "초기 위치"})
            options: ["없음", "거리 측정", "Nav2 목표", "초기 위치(AMCL)"]
            current: Object.keys(names).find(k => names[k] === root.tool) || "없음"
            onPicked: (v) => { root.tool = names[v]; root.measureA = root.measureB = null; root.goalError = "" }
        }
        Item { implicitWidth: Theme.sp2 }
        Seg { options: ["추적", "3인칭", "위에서", "자유"]; current: root.cam; onPicked: (v) => { viewAnim.stop(); root.cam = v } }
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
            camera: root.cam === "위에서" ? topCam : root.cam === "자유" ? freeCam : root.cam === "3인칭" ? tpCam : followCam
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

                // 지도 · 코스트맵 판: 이미지 0 행(맨 위)이 판의 +y 쪽, 왼쪽 열이 -x 쪽이다.
                // textureData 는 첫 행을 v = 0(판의 -y 쪽)에 놓으므로 flipV 로 뒤집는다(원점 yaw 90° 지도에서 확인).
                component GridPlane: Model {
                    id: gp
                    property alias image: tex.image
                    property alias tex: tex
                    property real alpha: 1
                    property bool behind: false   // Draw Behind: 깊이를 쓰지 않아 다른 것을 가리지 않는다
                    readonly property bool nearest: gpTex.magFilter === Texture.Nearest && gpTex.minFilter === Texture.Nearest
                    source: "#Rectangle"
                    castsShadows: false
                    opacity: alpha
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaMode: PrincipledMaterial.Blend
                        depthDrawMode: gp.behind ? PrincipledMaterial.NeverDepthDraw : PrincipledMaterial.OpaqueOnlyDepthDraw
                        baseColorMap: Texture {
                            id: gpTex
                            textureData: MapTexture { id: tex }
                            flipV: true
                            magFilter: Texture.Nearest   // RViz 처럼 칸을 또렷하게(확대 · 축소 모두)
                            minFilter: Texture.Nearest
                            mipFilter: Texture.None
                        }
                    }
                }
                GridPlane {
                    id: mapPlane
                    visible: root.showMap && robot.hasMap
                    image: robot.mapImage
                    alpha: root.mapOpacity
                    behind: root.mapDrawBehind
                    position: robot.mapCenter
                    eulerRotation.z: robot.mapYaw
                    scale: Qt.vector3d(robot.mapSize.width / 100, robot.mapSize.height / 100, 1)
                }
                GridPlane {   // 지도보다 0.002 m 위(CostmapLayer.kZ), 반투명
                    visible: globalCostmap.active && globalCostmap.received
                    image: globalCostmap.image
                    alpha: 0.7
                    position: globalCostmap.center
                    eulerRotation.z: globalCostmap.yaw
                    scale: Qt.vector3d(globalCostmap.size.width / 100, globalCostmap.size.height / 100, 1)
                }
                GridPlane {   // 지역 코스트맵은 전역보다 1 mm 더 위
                    visible: localCostmap.active && localCostmap.received
                    image: localCostmap.image
                    alpha: 0.7
                    position: localCostmap.center.plus(Qt.vector3d(0, 0, 0.001))
                    eulerRotation.z: localCostmap.yaw
                    scale: Qt.vector3d(localCostmap.size.width / 100, localCostmap.size.height / 100, 1)
                }
                SceneDots { visible: footprintLayer.active && points.length > 0; points: footprintLayer.points; size: 0.012; color: Theme.series[1] }

                SceneDots { visible: root.showTrail && !root.trailGradient && robot.sensorOn.odom && points.length > 0; points: robot.trail; size: 0.02; color: Theme.trail }
                Model {   // 이동 경로 그라디언트: 오래된 쪽일수록 흐리게
                    id: trailFade
                    visible: root.showTrail && root.trailGradient && robot.sensorOn.odom && robot.trail.length > 0
                    source: "#Cube"
                    castsShadows: false
                    instancing: ColorPointInstancing { points: trailFade.visible ? robot.trail : []; size: 0.02; color: Theme.trail }
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "white"; alphaMode: PrincipledMaterial.Blend }
                }
                SceneDots { visible: root.showTrail && points.length > 0; points: robot.plan; size: 0.02; color: Theme.plan }
                SceneDots { visible: root.showTrail && points.length > 0; points: robot.targetLine; size: 0.018; color: Theme.plan }
                SceneDots { points: root.measureA && root.measureB ? root.dense(root.measureA, root.measureB) : []; size: 0.015; color: Theme.goalDrag }
                SceneDots { points: [root.measureA, root.measureB].filter(p => p); size: 0.04; color: Theme.goalDrag }

                // LiDAR 잔상: 받은 시점 자세로 odom 프레임에 고정한 점(라이다 헤드 높이), 점마다 색 · 알파
                Model {
                    visible: root.showScan && root.scanDecay > 0 && scanAcc.pointCount > 0
                    position.z: model.lidarPos.z
                    source: "#Cube"
                    castsShadows: false
                    instancing: ColorPointInstancing { source: root.scanDecay > 0 ? scanAcc : null; size: root.scanPointSize }
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "white"; alphaMode: PrincipledMaterial.Blend }
                }

                SceneTfAxes { visible: tfTree.active; tree: tfTree; axisScale: root.tfScale }
                SceneCovEllipse { source: amclPose; color: Theme.series[0] }
                SceneCovEllipse { source: odomCov; color: Theme.series[2]; ghost: false }

                ScenePoseArrow {   // Nav2 이동 목표 화살표 (끄는 동안 노랑, 보낸 뒤 초록)
                    visible: root.goalA !== null
                    position: root.goalA ?? Qt.vector3d(0, 0, 0)
                    yaw: root.goalYaw
                    color: root.goalDragging ? Theme.goalDrag : Theme.ok
                }
                ScenePoseArrow {   // AMCL 초기 위치 미리보기 (끄는 동안)
                    visible: root.initDragging
                    position: root.initA ?? Qt.vector3d(0, 0, 0)
                    yaw: root.initYaw
                    color: Theme.initialPose
                }

                Node {
                    id: base
                    position: root.drawPos
                    eulerRotation.z: root.drawYaw

                    RobotModel {
                        id: model
                        wheelAngleL: root.drawWheelL
                        wheelAngleR: root.drawWheelR
                        bodyUrl: robot.modelFile("body")
                        wheelLUrl: robot.modelFile("wheel_left")
                        wheelRUrl: robot.modelFile("wheel_right")
                        lidarUrl: robot.modelFile("lidar_head")
                        parts: robot.modelParts()
                        lidarHz: robot.lidarHz
                    }

                    SceneDots {   // LiDAR 점 (scan 프레임 = base_link, 라이다 헤드 높이에 그림), 잔상이 꺼졌을 때
                        visible: root.showScan && root.scanDecay <= 0 && robot.sensorOn.scan && points.length > 0
                        position.z: model.lidarPos.z
                        points: robot.scanPoints
                        size: root.scanPointSize
                        color: Theme.lidar
                    }

                    Node {   // 좌표축 x 빨강 · y 초록 · z 파랑 (0.3 m)
                        visible: root.showAxes && robot.sensorOn.tf
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
                position: root.scenePos(root.drawPos.x - 1.3 * Math.cos(root.drawYaw * Math.PI / 180), root.drawPos.y - 1.3 * Math.sin(root.drawYaw * Math.PI / 180), 0.9)
                lookAtNode: base
                clipNear: 1
                clipFar: 20000
            }
            // 3인칭: 로봇 yaw 를 따라 돌고, 끌어서 돌린 오프셋(tpYaw · tpPitch)과 거리(tpDist)는 유지한다
            Node {
                position: root.scenePos(root.drawPos.x, root.drawPos.y, 0)
                eulerRotation: Qt.vector3d(root.tpPitch, root.drawYaw - 90 + root.tpYaw, 0)
                PerspectiveCamera { id: tpCam; position: Qt.vector3d(0, 0, root.tpDist); clipNear: 1; clipFar: 20000 }
            }
            // 위에서: 로봇 위 4 m, 화면 오른쪽이 로봇 앞(odom x). 직교 카메라라 바닥 지도와 라이다 높이 점의 축척이 같다
            // (원근이면 높이 0.4 m 점이 바닥보다 약 1.1 배 크게 보여 지도가 작아 보였다).
            OrthographicCamera {
                id: topCam
                position: root.scenePos(root.drawPos.x, root.drawPos.y, 4)
                eulerRotation.x: -90
                horizontalMagnification: root.topPxPerM / 100
                verticalMagnification: root.topPxPerM / 100
                clipNear: 1
                clipFar: 20000
            }
            // 자유: 로봇(+ 끌어서 옮긴 만큼)을 중심으로 회전 · 이동 · 확대
            Node {
                id: orbit
                position: root.scenePos(root.drawPos.x, root.drawPos.y, 0).plus(root.panOffset)
                eulerRotation: Qt.vector3d(root.camPitch, root.camYaw, 0)
                PerspectiveCamera { id: freeCam; position: Qt.vector3d(0, 0, root.camDist); clipNear: 1; clipFar: 20000 }
            }
        }

        // ---- TF 프레임 이름 (화면에 겹쳐 그림, 켜져 있을 때만 프레임마다 위치를 다시 계산) ----
        Item {
            id: tfLabelLayer
            anchors.fill: view
            visible: root.showTf && root.tfLabels
            property int tick: 0
            FrameAnimation { running: tfLabelLayer.visible && tfTree.frameCount > 0; onTriggered: tfLabelLayer.tick++ }
            Repeater {
                model: tfLabelLayer.visible ? tfTree.frameCount : 0
                Text {
                    required property int index
                    readonly property var f: tfTree.frames[index] ?? ({})
                    readonly property vector3d sp: {
                        tfLabelLayer.tick
                        const p = f.position ?? Qt.vector3d(0, 0, 0)
                        return view.mapFrom3DScene(root.scenePos(p.x, p.y, p.z))
                    }
                    visible: sp.z > 0
                    x: sp.x + 6
                    y: sp.y - height - 2
                    opacity: f.alpha ?? 1
                    text: f.name ?? ""
                    color: Theme.hudText1
                    style: Text.Outline
                    styleColor: Theme.canvas3d
                    font.family: root.mono
                    font.pixelSize: Theme.fsCaption
                }
            }
        }

        // Fusion 360 방식 조작: 휠 확대 · 가운데 버튼 끌기 이동 · Shift+가운데 회전.
        // 가운데 버튼이 없을 때: 왼쪽 끌기(또는 Shift+왼쪽) 회전 · Ctrl+왼쪽 이동 · Alt+왼쪽 확대. 바닥 더블클릭은 그 점으로 Focus.
        MouseArea {
            anchors.fill: view
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            hoverEnabled: true
            property var drag: null
            onPressed: (m) => {
                root.clickedView()
                viewAnim.stop()   // 새 입력이 오면 카메라 애니메이션을 바로 멈춘다
                if (m.button === Qt.LeftButton && (root.tool === "목표" || root.tool === "초기 위치")) {
                    const p = root.floorAt(m.x, m.y)
                    if (p && root.tool === "목표")
                        root.goalA = p, root.goalYaw = robot.theta, root.goalDragging = true
                    else if (p)
                        root.initA = p, root.initYaw = robot.theta, root.initDragging = true
                    drag = null
                    return
                }
                const shift = m.modifiers & Qt.ShiftModifier
                drag = { x: m.x, y: m.y, moved: false, left: m.button === Qt.LeftButton,
                         mode: m.button === Qt.MiddleButton ? (shift ? "orbit" : "pan")
                             : (m.modifiers & Qt.ControlModifier) ? "pan" : (m.modifiers & Qt.AltModifier) ? "zoom" : "orbit" }
            }
            onPositionChanged: (m) => {
                root.cursorFloor = root.floorAt(m.x, m.y)
                if (root.goalDragging || root.initDragging) {   // RViz 처럼 누른 곳에서 끄는 쪽이 바라볼 방향
                    const p = root.cursorFloor, a = root.goalDragging ? root.goalA : root.initA
                    if (p && p.minus(a).length() > 0.03) {
                        const yaw = Math.atan2(p.y - a.y, p.x - a.x)
                        if (root.goalDragging) root.goalYaw = yaw
                        else root.initYaw = yaw
                    }
                    return
                }
                if (!drag) return
                const dx = m.x - drag.x, dy = m.y - drag.y
                if (!drag.moved && Math.hypot(dx, dy) < 4) return
                drag.moved = true
                drag.x = m.x; drag.y = m.y
                viewAnim.stop()
                if (root.cam === "3인칭" && drag.mode !== "pan") {   // 3인칭은 로봇 기준 오프셋만 바꾼다
                    if (drag.mode === "orbit") {
                        root.tpYaw -= dx * 0.3
                        root.tpPitch = Math.max(-89, Math.min(30, root.tpPitch - dy * 0.3))
                    } else {
                        root.tpDist = Math.max(30, Math.min(5000, root.tpDist * Math.pow(1.005, dy)))
                    }
                    return
                }
                root.enterFree()
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
            onExited: root.cursorFloor = null
            onReleased: (m) => {
                if (root.goalDragging) {
                    root.goalDragging = false
                    root.goalError = robot.sendGoal(root.goalA.x, root.goalA.y, root.goalYaw)
                    if (root.goalError) {
                        root.goalA = null   // 보내지 않았다 — 화살표를 지우고 도구는 켜 둔 채 이유를 보인다
                        goalErrorTimer.restart()
                        return
                    }
                    root.tool = ""   // RViz 처럼 한 번 보내면 도구가 꺼진다
                    return
                }
                if (root.initDragging) {
                    root.initDragging = false
                    robot.setInitialPose(root.initA.x, root.initA.y, root.initYaw)
                    root.tool = ""
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
            onDoubleClicked: (m) => {
                if (root.tool !== "" || m.button !== Qt.LeftButton)
                    return
                const p = root.floorAt(m.x, m.y)
                if (p) root.focusAt(p)
            }
            onWheel: (w) => {
                viewAnim.stop()
                if (root.cam === "3인칭")
                    return root.tpDist = Math.max(30, Math.min(5000, root.tpDist * Math.pow(0.999, w.angleDelta.y)))
                root.enterFree()
                root.camDist = Math.max(20, Math.min(5000, root.camDist * Math.pow(0.999, w.angleDelta.y)))
            }
        }

        // ---- 표시 레이어 목록 ----
        SceneLayerPanel {
            x: 14; y: 14
            maxHeight: parent.height - 28 - poseRow.height - 14
            scene: root
            robot: root.robot
            scanAcc: scanAcc
            globalCostmap: globalCostmap
            localCostmap: localCostmap
            footprint: footprintLayer
            tf: tfTree
            amcl: amclPose
            odomCov: odomCov
        }

        // ---- 자세 · 커서 좌표 · 축척 ----
        Row {
            id: poseRow
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
                    Text { color: Theme.hudText2; font.family: root.mono; font.pixelSize: Theme.fsSmall
                        text: root.cursorFloor ? "커서 x " + root.cursorFloor.x.toFixed(2) + " · y " + root.cursorFloor.y.toFixed(2) + " m (odom)" : "커서 —" }
                }
            }
            Rectangle {   // 위에서 볼 때만 축척이 맞다
                visible: root.cam === "위에서"
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.rLg; color: Theme.hudFill
                width: bar.width + 60; height: 36
                Rectangle { id: bar; x: 14; anchors.verticalCenter: parent.verticalCenter; width: root.topPxPerM * 0.5; height: 4; color: Theme.hudText1 }
                Text { anchors.left: bar.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "0.5 m"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall }
            }
        }

        // ---- 지도 없음 안내(지도를 받으면 사라진다) ----
        Rectangle {
            objectName: "noMapHint"
            visible: root.noMapHintShown
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.margins: 14
            radius: Theme.rLg; color: Theme.hudFill
            width: noMap.implicitWidth + 28; height: noMap.implicitHeight + 16
            Text {
                id: noMap
                anchors.centerIn: parent
                color: Theme.hudText2; font.pixelSize: Theme.fsSmall
                text: "지도 없음 — PC 에서 SLAM/Nav2 를 실행하면 /map 이 표시됩니다(tooling\\pc\\map_start.cmd)"
            }
        }

        // ---- 거리 측정 · 이동 목표 · 초기 위치 안내 ----
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
                color: root.tool === "초기 위치" ? Theme.initialPose : Theme.goalDrag
                font.family: root.mono; font.pixelSize: Theme.fsBody; textFormat: Text.StyledText
                text: root.tool === "초기 위치" ? (root.initDragging
                          ? "초기 위치 (" + root.initA.x.toFixed(2) + ", " + root.initA.y.toFixed(2) + ") m · " + (root.initYaw * 180 / Math.PI).toFixed(0) + "°"
                          : "로봇이 있는 곳을 누른 채 로봇이 보는 방향으로 끌고 놓으세요 (AMCL)")
                      + (robot.mapOdomFresh ? "" : "<br>map→odom 이 없거나 낡았습니다 — odom 프레임으로 보냅니다 (PC Nav2 연결 확인)")
                    : root.goalError ? "목표를 보내지 않았습니다 — " + root.goalError + "<br>조종 바의 <b>길게 눌러 잠금 해제</b>(또는 Ctrl+Enter)로 조종을 켠 뒤 다시 찍으세요"
                    : root.tool === "목표" && !root.goalDragging && !robot.teleopArmed ? "① 조종이 잠겨 있습니다 — 먼저 <b>Ctrl+Enter</b>(또는 조종 바의 잠금 해제)<br>② 그다음 목표 위치를 누른 채 바라볼 방향으로 끌고 놓으세요 (Nav2)"
                    : root.tool === "목표" && !root.goalDragging ? "목표 위치를 누른 채 바라볼 방향으로 끌고 놓으세요 (Nav2) — 놓는 순간 보냅니다"
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
                Node {   // 위에서 시점은 로봇 앞(+x)이 큐브 위쪽이 되게 돌려 보인다(윗면 글자가 바로 읽힘)
                    eulerRotation: Qt.vector3d(root.viewPitch, root.cam === "위에서" ? -90 : root.viewYaw, 0)
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
                // 윗면 · 아랫면 글자의 위쪽은 '앞'(odom +x) 면을 향한다(추적 시점에서 바로 읽힌다)
                Face { label: "위"; position: Qt.vector3d(0, 50, 0); eulerRotation: Qt.vector3d(-90, -90, 0) }
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
                Text {   // 누르면 펼치기 · 접기 (기본 접힘, 마지막 상태 기억)
                    text: (root.legendOpen ? "▾ " : "▸ ") + "범례"
                    color: Theme.hudText1; font.pixelSize: Theme.fsSmall
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.legendOpen = !root.legendOpen }
                }
                Column {
                    visible: root.legendOpen
                    spacing: 4
                    Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.trail; anchors.verticalCenter: parent.verticalCenter } Text { text: "이동 경로"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                    Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.plan; anchors.verticalCenter: parent.verticalCenter } Text { text: "목표 / 계획 경로"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                    Row { spacing: 6; Rectangle { width: 8; height: 8; radius: Theme.rXs; color: Theme.lidar; anchors.verticalCenter: parent.verticalCenter } Text { text: "LiDAR 점"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                    Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.ok; anchors.verticalCenter: parent.verticalCenter } Text { text: "이동 목표 (Nav2)"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                    Row { spacing: 6; Rectangle { width: 18; height: 3; color: Theme.initialPose; anchors.verticalCenter: parent.verticalCenter } Text { text: "초기 위치 (AMCL)"; color: Theme.hudText2; font.pixelSize: Theme.fsSmall } }
                    Text { text: "휠 확대 · 가운데 끌기 이동 · Shift+가운데 회전\n왼쪽 끌기 회전 · Ctrl 이동 · Alt 확대 · 더블클릭 Focus"; color: Theme.hudText2; font.pixelSize: Theme.fsCaption }
                    Text { visible: model.bodyUrl.toString() === ""; text: "기본 모델 (model/ 폴더에 Fusion 360 내보내기 파일을 넣으면 교체)"; color: Theme.hudText2; font.pixelSize: Theme.fsCaption }
                }
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
