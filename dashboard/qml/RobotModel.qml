import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils

// 로봇 모델 (base_link 프레임, m 단위, x 앞 · y 왼쪽 · z 위).
// model/ 폴더에 Fusion 360 에서 내보낸 body · wheel_left · wheel_right (.glb/.gltf/.fbx/.obj) 가 있으면
// 그 파일을 쓰고, 없으면 기본 도형(PBR 재질)으로 그린다. 바퀴 파일은 원점이 차축 중심이어야 한다.
Node {
    id: root
    property real wheelAngleL: 0   // rad
    property real wheelAngleR: 0
    property url bodyUrl
    property url wheelLUrl
    property url wheelRUrl
    // 내보낸 파일의 좌표계 보정: glTF 는 Y 가 위 · m 단위이므로 x 축으로 90° 돌려 z 를 위로 맞춘다.
    // Fusion 에서 cm/mm 로 내보냈으면 modelScale 을 0.01 / 0.001 로 바꾼다.
    property vector3d modelRotation: Qt.vector3d(90, 0, 0)
    property real modelScale: 1.0

    readonly property real wheelRadius: 0.05    // firmware/pwm-pico/include/config.h
    readonly property real wheelSeparation: 0.34
    readonly property real lidarHeight: 0.20

    PrincipledMaterial { id: aluminum; baseColor: "#C9CFD6"; metalness: 0.55; roughness: 0.35 }
    PrincipledMaterial { id: darkPlate; baseColor: "#2A2F36"; metalness: 0.2; roughness: 0.55 }
    PrincipledMaterial { id: rubber; baseColor: "#1B1F25"; metalness: 0.0; roughness: 0.92 }
    PrincipledMaterial { id: hubMetal; baseColor: "#8A939E"; metalness: 0.9; roughness: 0.25 }
    PrincipledMaterial { id: lidarBody; baseColor: "#3C434C"; metalness: 0.4; roughness: 0.4 }
    PrincipledMaterial { id: lidarTop; baseColor: "#5A6370"; metalness: 0.6; roughness: 0.3 }

    // ---- 몸체 ----
    RuntimeLoader {
        visible: root.bodyUrl.toString() !== ""
        source: root.bodyUrl
        eulerRotation: root.modelRotation
        scale: Qt.vector3d(root.modelScale, root.modelScale, root.modelScale)
    }
    Node {
        visible: root.bodyUrl.toString() === ""
        Model { source: "#Cube"; position: Qt.vector3d(0, 0, 0.09); scale: Qt.vector3d(0.0036, 0.0028, 0.0010); materials: aluminum }
        Model { source: "#Cube"; position: Qt.vector3d(0, 0, 0.145); scale: Qt.vector3d(0.0032, 0.0026, 0.0001); materials: darkPlate }
        Model { source: "#Sphere"; position: Qt.vector3d(-0.14, 0, 0.02); scale: Qt.vector3d(0.0004, 0.0004, 0.0004); materials: hubMetal }
        Model { source: "#Cylinder"; position: Qt.vector3d(0, 0, 0.165); eulerRotation.x: 90; scale: Qt.vector3d(0.0008, 0.0004, 0.0008); materials: lidarBody }
        Model { source: "#Cylinder"; position: Qt.vector3d(0, 0, 0.195); eulerRotation.x: 90; scale: Qt.vector3d(0.0007, 0.0002, 0.0007); materials: lidarTop }
    }

    // ---- 바퀴 (차축 = y 축, 실측 바퀴 속도를 적분한 각도로 회전) ----
    component Wheel: Node {
        property real angle
        property url url
        property real side: 1
        position: Qt.vector3d(0, side * root.wheelSeparation / 2, root.wheelRadius)
        eulerRotation.y: angle * 180 / Math.PI
        RuntimeLoader {
            visible: parent.url.toString() !== ""
            source: parent.url
            eulerRotation: root.modelRotation
            scale: Qt.vector3d(root.modelScale, root.modelScale, root.modelScale)
        }
        Node {
            visible: parent.url.toString() === ""
            Model { source: "#Cylinder"; scale: Qt.vector3d(0.001, 0.0004, 0.001); materials: rubber }
            Model { source: "#Cylinder"; position.y: parent.parent.side * 0.0205; scale: Qt.vector3d(0.0006, 0.00005, 0.0006); materials: hubMetal }
            // 회전이 보이도록 바퀴 옆면의 표시
            Model { source: "#Cube"; position: Qt.vector3d(0, parent.parent.side * 0.021, 0.025); scale: Qt.vector3d(0.0001, 0.00005, 0.0004); materials: darkPlate }
        }
    }
    Wheel { side: 1; angle: root.wheelAngleL; url: root.wheelLUrl }
    Wheel { side: -1; angle: root.wheelAngleR; url: root.wheelRUrl }
}
