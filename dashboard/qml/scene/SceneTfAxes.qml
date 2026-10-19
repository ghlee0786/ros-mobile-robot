import QtQuick
import QtQuick3D
import RobotDashboard

// TfTree 의 프레임마다 작은 3축(x 빨강 · y 초록 · z 파랑)과 부모까지의 선을 그린다(고정 프레임 = 부모 노드).
// 정적이 아닌 프레임은 tree 가 준 gray(회색 보간) · alpha 로 페이드한다.
Node {
    id: root
    required property TfTree tree
    property real axisScale: 1
    readonly property real len: 0.1 * axisScale

    function faded(c, g) { return Qt.rgba(c.r + (0.5 - c.r) * g, c.g + (0.5 - c.g) * g, c.b + (0.5 - c.b) * g, 1) }
    // 부모가 보이는 프레임마다 부모 → 자식 2 cm 간격 점
    function links() {
        const out = []
        for (const f of tree.frames) {
            if (!f.linked)
                continue
            const a = f.parentPosition, b = f.position, d = b.minus(a), n = Math.floor(d.length() / 0.02)
            for (let i = 1; i < n; ++i)
                out.push(a.plus(d.times(i / n)))
        }
        return out
    }

    component Axis: Model {
        id: ax
        property color c
        property bool blend
        source: "#Cube"
        castsShadows: false
        materials: PrincipledMaterial {
            lighting: PrincipledMaterial.NoLighting; baseColor: ax.c
            alphaMode: ax.blend ? PrincipledMaterial.Blend : PrincipledMaterial.Default
        }
    }

    // 목록이 200 ms 마다 새로 오므로 개수로 만들고 값은 index 로 읽는다(지연 생성 반복 방지)
    Repeater3D {
        model: root.tree.frameCount
        delegate: Node {
            id: fr
            required property int index
            readonly property var f: root.tree.frames[index] ?? ({})
            readonly property real gray: f.gray ?? 0
            position: f.position ?? Qt.vector3d(0, 0, 0)
            rotation: f.rotation ?? Qt.quaternion(1, 0, 0, 0)
            opacity: f.alpha ?? 1
            Axis { c: root.faded(Theme.axisX, fr.gray); blend: fr.opacity < 1; position.x: root.len / 2; scale: Qt.vector3d(root.len / 100, 0.00006, 0.00006) }
            Axis { c: root.faded(Theme.axisY, fr.gray); blend: fr.opacity < 1; position.y: root.len / 2; scale: Qt.vector3d(0.00006, root.len / 100, 0.00006) }
            Axis { c: root.faded(Theme.axisZ, fr.gray); blend: fr.opacity < 1; position.z: root.len / 2; scale: Qt.vector3d(0.00006, 0.00006, root.len / 100) }
        }
    }
    SceneDots { points: root.visible ? root.links() : []; size: 0.006; color: Theme.hudText2 }
}
