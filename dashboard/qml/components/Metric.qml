import QtQuick
import RobotDashboard

// 수치: 라벨 + 값(tnum) + 단위. stale 이면 흐리게 하고 'n s 전' 을 붙인다.
Column {
    id: root
    property string label
    property string value
    property string unit
    property bool stale: false
    property int ageSec: 0
    property int valueSize: Theme.fsTitle

    spacing: 2
    Text { text: root.label; color: Theme.text2; font.pixelSize: Theme.fsSmall }
    Row {
        spacing: Theme.sp1
        opacity: root.stale ? Theme.opDisabled : 1
        Text {
            id: v
            text: root.value
            color: Theme.text1
            font.pixelSize: root.valueSize
            font.weight: Theme.wMedium
            font.features: Theme.numFeatures
        }
        Text {
            anchors.baseline: v.baseline
            visible: root.unit !== ""
            text: root.unit
            color: Theme.text3
            font.pixelSize: Theme.fsSmall
        }
    }
    Text {
        visible: root.stale
        text: root.ageSec + " s 전"
        color: Theme.text3
        font.pixelSize: Theme.fsCaption
        font.features: Theme.numFeatures
    }
}
