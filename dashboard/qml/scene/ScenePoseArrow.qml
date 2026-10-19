import QtQuick
import QtQuick3D
import RobotDashboard

// 바닥 위 자세 화살표(원점 구 + 0.3 m 화살). 이동 목표 · 초기 위치 · AMCL 고스트에 쓴다. 좌표는 부모 기준 m · rad.
Node {
    id: root
    property color color: Theme.goalDrag
    property real yaw: 0   // rad
    eulerRotation.z: yaw * 180 / Math.PI
    readonly property PrincipledMaterial mat: PrincipledMaterial {
        lighting: PrincipledMaterial.NoLighting; baseColor: root.color
        alphaMode: root.opacity < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Default
    }
    Model { source: "#Sphere"; scale: Qt.vector3d(0.0005, 0.0005, 0.0005); materials: root.mat; castsShadows: false }
    Model { source: "#Cylinder"; position.x: 0.15; eulerRotation.z: -90; scale: Qt.vector3d(0.0002, 0.003, 0.0002); materials: root.mat; castsShadows: false }
    Model { source: "#Cone"; position.x: 0.30; eulerRotation.z: -90; scale: Qt.vector3d(0.0008, 0.0012, 0.0008); materials: root.mat; castsShadows: false }
}
