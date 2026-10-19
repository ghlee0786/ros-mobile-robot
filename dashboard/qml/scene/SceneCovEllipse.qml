import QtQuick
import QtQuick3D
import RobotDashboard

// AmclPose(공분산이 붙은 자세)를 반투명 고스트 화살표 · 2σ 타원 · yaw ±2σ 부채꼴로 그린다(odom 프레임 m).
Node {
    id: root
    required property AmclPose source
    property color color: Theme.series[0]
    property bool ghost: true
    property real fanRadius: 0.45   // 부채꼴 반지름 m
    visible: source.active && source.valid
    position: Qt.vector3d(source.x, source.y, 0.012)

    // 부채꼴 테두리: yaw ± spread 호와 두 반지름(부모 기준 점)
    function fan() {
        const src = root.source, s = Math.min(Math.PI, src.yawSpread), out = []
        const n = Math.max(2, Math.ceil(2 * s * fanRadius / 0.02))
        for (let i = 0; i <= n; ++i) {
            const a = src.yaw - s + 2 * s * i / n
            out.push(Qt.vector3d(fanRadius * Math.cos(a), fanRadius * Math.sin(a), 0))
        }
        for (const a of [src.yaw - s, src.yaw + s])
            for (let r = 0.02; r < fanRadius; r += 0.02)
                out.push(Qt.vector3d(r * Math.cos(a), r * Math.sin(a), 0))
        return out
    }

    Node {   // 2σ 타원: 원기둥을 바닥에 눕혀 납작하게 누르고 긴 축 · 짧은 축 길이로 늘린 뒤 긴 축 방향으로 돌린다
        eulerRotation.z: root.source.angle * 180 / Math.PI
        Model {
            source: "#Cylinder"
            eulerRotation.x: 90
            scale: Qt.vector3d(2 * root.source.axisMajor / 100, 0.00002, 2 * root.source.axisMinor / 100)
            castsShadows: false
            opacity: 0.35
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.color; alphaMode: PrincipledMaterial.Blend }
        }
    }
    SceneDots { visible: root.source.yawSpread > 0; points: root.visible ? root.fan() : []; size: 0.012; color: root.color }
    ScenePoseArrow { visible: root.ghost; yaw: root.source.yaw; color: root.color; opacity: 0.5 }
}
