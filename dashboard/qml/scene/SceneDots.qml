import QtQuick
import QtQuick3D
import RobotDashboard

// 점 목록을 같은 크기 · 색의 작은 큐브로 그린다(LiDAR 점 · 경로 · 선). 좌표는 부모 노드 기준 m.
Model {
    property alias points: inst.points
    property alias size: inst.size
    property alias color: inst.color
    source: "#Cube"
    castsShadows: false
    visible: inst.points.length > 0
    instancing: PointInstancing { id: inst }
    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "white" }
}
