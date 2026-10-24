import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 속도 한계(조종 바 윗줄): 파이 cmd_watchdog 의 최대 전진 · 후진 · 회전. 줄마다 진행 막대 모양 슬라이더(0 ~ 펌웨어 한계)
// + 값 · 워치독이 돌려준 실제 값 + 자물쇠. 자물쇠를 누르면 그 값이 고정되어 슬라이더가 꺼진다(C++ 도 잠긴 줄은 무시).
// 실제 값이 다르거나(거부 · 연결 끊김) 모르면 경고색.
ColumnLayout {
    id: panel
    required property Robot robot
    readonly property var limits: robot.limits
    // 시나리오가 1280 · 1920 폭에서 겹침이 없는지 본다: 모든 줄이 패널 안에 들고 슬라이더가 최소 폭 이상
    readonly property bool fits: {
        let ok = rep.count === 3 && lines.width > 0
        for (let i = 0; i < rep.count; ++i) {
            const it = rep.itemAt(i)
            ok = ok && it !== null && it.fits
        }
        return ok
    }
    readonly property bool fwdSliderEnabled: rep.count > 0 && rep.itemAt(0) !== null && rep.itemAt(0).sliderEnabled
    spacing: Theme.sp1

    RowLayout {
        id: lines
        Layout.fillWidth: true
        spacing: Theme.sp4

        Text {
            text: "속도 한계"
            color: Theme.text1
            font.pixelSize: Theme.fsLabel
            font.weight: Theme.wStrong
        }

        Repeater {
            id: rep
            model: 3   // 줄 수는 고정 — rows 가 바뀔 때마다 대리자를 다시 만들면 끄는 중인 슬라이더가 끊긴다
            delegate: RowLayout {
                id: line
                required property int index
                readonly property var row: panel.limits.rows[index]
                readonly property bool fits: slider.width >= 80 && x + width <= lines.width + 0.5
                readonly property bool sliderEnabled: slider.enabled
                Layout.fillWidth: true
                spacing: Theme.sp2

                Text {
                    text: line.row.label
                    color: Theme.text2
                    font.pixelSize: Theme.fsBody
                }

                Slider {
                    id: slider
                    objectName: "limitSlider_" + line.row.name
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.preferredWidth: 160
                    from: 0; to: line.row.firmware; stepSize: 0.01
                    value: line.row.value
                    enabled: !line.row.locked
                    focusPolicy: Qt.NoFocus
                    onMoved: panel.limits.setLimit(line.row.name, value)
                    background: Rectangle {
                        x: slider.leftPadding
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: slider.availableWidth
                        height: 8
                        radius: Theme.rXs
                        color: Theme.surface3
                        opacity: slider.enabled ? 1 : Theme.opDisabled + 0.3
                        Rectangle {   // 채운 부분 = 지금 한계
                            width: slider.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            color: line.row.warn ? Theme.warn : Theme.accent
                        }
                        Rectangle {   // 펌웨어 한계 눈금(오른쪽 끝)
                            x: parent.width - width
                            y: -3
                            width: 2
                            height: parent.height + 6
                            color: Theme.text3
                        }
                    }
                    handle: Rectangle {
                        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: 6
                        height: 18
                        radius: Theme.rXs
                        visible: slider.enabled
                        color: slider.pressed ? Theme.text1 : Theme.text2
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "펌웨어 한계 " + line.row.firmware.toFixed(2) + " " + line.row.unit
                                  + (line.row.locked ? " · 잠김(자물쇠를 눌러 풀기)" : "")
                }

                Column {
                    Layout.preferredWidth: 78
                    Text {
                        objectName: "limitValue_" + line.row.name
                        text: line.row.value.toFixed(2) + " " + line.row.unit
                        color: line.row.warn ? Theme.warnText : Theme.text1
                        font.pixelSize: Theme.fsBody
                        font.weight: Theme.wStrong
                        font.features: Theme.numFeatures
                    }
                    Text {
                        text: panel.limits.support === -1 ? "파이 미지원"
                            : line.row.actualKnown ? "파이 " + line.row.actual.toFixed(2) : "파이 —"
                        color: line.row.warn ? Theme.warnText : Theme.text3
                        font.pixelSize: Theme.fsCaption
                        font.features: Theme.numFeatures
                    }
                }

                IconBtn {
                    objectName: "limitLock_" + line.row.name
                    checked: line.row.locked
                    icon.source: line.row.locked ? "qrc:/icons/lock.svg" : "qrc:/icons/unlock.svg"
                    icon.color: line.row.locked ? Theme.accentText : Theme.text2
                    tip: line.row.locked ? "고정됨 — 눌러서 풀기" : "이 값으로 고정"
                    onClicked: panel.limits.setLocked(line.row.name, !line.row.locked)
                }
            }
        }
    }

    Text {
        objectName: "limitsUnsupported"
        visible: panel.limits.support === -1
        Layout.fillWidth: true
        text: "파이 워치독이 속도 한계를 지원하지 않음 — 배포 필요 (지금은 대시보드 키보드 · 조이스틱 배율만 제한)"
        color: Theme.warnText
        font.pixelSize: Theme.fsSmall
        elide: Text.ElideRight
    }
}
