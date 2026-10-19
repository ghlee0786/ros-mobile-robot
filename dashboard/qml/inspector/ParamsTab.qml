import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 파라미터 탭: ParamClient 가 '/<node>/list_parameters' 등을 한 번에 하나씩 부른다.
// 범위가 있으면 Slider(놓을 때만 전송), bool 은 Switch, 나머지는 입력칸. 바꾸기 전에 확인 문구를 띄운다.
// --no-publish(ROBOT_DASHBOARD_NO_PUBLISH=1)면 쓰기를 막는다.
RowLayout {
    id: tab
    required property ParamClient params
    required property RosApiClient api
    property bool active: false
    spacing: Theme.sp4

    onActiveChanged: if (active && api.nodes.length === 0) api.refreshNodes()

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: "파라미터"
        subtitle: tab.params.node !== "" ? tab.params.node + " · " + tab.params.count + "개" : ""
        headerExtra: [
            ComboBox {
                width: 260
                model: tab.api.nodes
                displayText: tab.params.node !== "" ? tab.params.node : "노드 고르기"
                currentIndex: -1
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fsBody
                onActivated: { tab.params.node = currentText; tab.params.refresh() }
            },
            Btn { size: "sm"; text: "다시 읽기"; enabled: tab.params.node !== ""; onClicked: tab.params.refresh() }
        ]

        LoadingBar { Layout.fillWidth: true; running: tab.params.busy }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: tab.params.writable
                  ? "값을 바꾸면 파이에서 도는 노드(속도 한계 등)의 동작이 바로 바뀐다. 적용 전에 한 번 더 묻는다."
                  : "쓰기 막힘: --no-publish 로 실행 중이라 값을 읽기만 한다."
            color: Theme.warnText
            font.pixelSize: Theme.fsSmall
        }
        Text {
            Layout.fillWidth: true
            visible: tab.params.error !== ""
            wrapMode: Text.WordWrap
            text: "실패: " + tab.params.error
            color: Theme.dangerText
            font.pixelSize: Theme.fsSmall
        }
        StatusChip {
            visible: tab.params.hasResult && tab.params.error === ""
            kind: tab.params.lastSuccessful ? "ok" : "danger"
            text: tab.params.lastSuccessful ? "적용됨" : "거부됨: " + tab.params.lastReason
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { text: "이름"; Layout.preferredWidth: 280; leftPadding: Theme.sp2; color: Theme.text2; font.pixelSize: Theme.fsSmall }
            Text { text: "형식"; Layout.preferredWidth: 90; color: Theme.text2; font.pixelSize: Theme.fsSmall }
            Text { text: "값"; Layout.fillWidth: true; color: Theme.text2; font.pixelSize: Theme.fsSmall }
            Text { text: "범위"; Layout.preferredWidth: 140; color: Theme.text2; font.pixelSize: Theme.fsSmall }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tab.params.count > 0
            clip: true
            model: tab.params
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: prow
                required property string name
                required property string typeName
                required property var value
                required property string valueText
                required property bool hasRange
                required property real min
                required property real max
                required property real step
                required property bool readOnly
                required property string description
                required property string editor
                readonly property bool editable: !readOnly && tab.params.writable
                width: ListView.view.width
                height: 40
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }

                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Text {
                        Layout.preferredWidth: 280
                        leftPadding: Theme.sp2
                        text: prow.name; elide: Text.ElideRight
                        color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsBody
                        ToolTip.visible: nameHover.hovered && prow.description !== ""
                        ToolTip.text: prow.description
                        HoverHandler { id: nameHover }
                    }
                    Text { Layout.preferredWidth: 90; text: prow.typeName; color: Theme.text2; font.pixelSize: Theme.fsSmall }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider {
                            id: slider
                            visible: prow.editor === "slider"
                            Layout.fillWidth: true
                            enabled: prow.editable
                            from: prow.min; to: prow.max
                            stepSize: prow.step > 0 ? prow.step : (prow.typeName === "integer" ? 1 : 0)
                            value: Number(prow.value)
                            onPressedChanged: if (!pressed && value !== Number(prow.value))
                                confirm.ask(prow.name, value, () => slider.value = Qt.binding(() => Number(prow.value)))
                        }
                        Text {
                            visible: prow.editor === "slider"
                            text: prow.editor === "slider" && slider.pressed ? slider.value.toFixed(3) : prow.valueText
                            color: Theme.text1; font.family: Theme.monoFamily; font.pixelSize: Theme.fsBody
                            font.features: Theme.numFeatures
                        }
                        Switch {
                            id: sw
                            visible: prow.editor === "switch"
                            enabled: prow.editable
                            checked: prow.value === true
                            onToggled: confirm.ask(prow.name, checked, () => sw.checked = Qt.binding(() => prow.value === true))
                        }
                        InField {
                            id: field
                            visible: prow.editor === "text"
                            Layout.fillWidth: true
                            mono: true
                            readOnly: !prow.editable
                            text: prow.valueText
                            onAccepted: if (text !== prow.valueText)
                                confirm.ask(prow.name, text, () => field.text = Qt.binding(() => prow.valueText))
                        }
                        Item { Layout.fillWidth: prow.editor === "switch" }   // Switch 만 있을 때 열 폭을 지킨다
                    }
                    Text {
                        Layout.preferredWidth: 140
                        leftPadding: Theme.sp2
                        text: prow.hasRange ? prow.min + " … " + prow.max : prow.readOnly ? "읽기 전용" : "—"
                        color: Theme.text3; font.family: Theme.monoFamily; font.pixelSize: Theme.fsSmall
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tab.params.count === 0
            EmptyState {
                anchors.centerIn: parent
                visible: !tab.params.busy
                title: tab.params.node === "" ? "고른 노드 없음" : "파라미터 없음"
                description: tab.params.node === "" ? "위에서 노드를 고르면 파라미터를 읽어 옵니다" : ""
            }
        }
    }

    // 적용 확인: 파이 노드 값을 바꾼다는 것을 한 번 더 알린다
    Popup {
        id: confirm
        property string name
        property var value
        property var revert
        function ask(n, v, r) { name = n; value = v; revert = r; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: Theme.sp5
        closePolicy: Popup.CloseOnEscape
        onClosed: if (revert) { revert(); revert = null }
        background: Rectangle { radius: Theme.rXl; color: Theme.surface3; border.color: Theme.borderDefault }
        contentItem: ColumnLayout {
            spacing: Theme.sp3
            Text { text: "파이 노드 값 바꾸기"; color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Theme.wStrong }
            Text {
                text: tab.params.node + " 의 " + confirm.name + " 을(를) " + String(confirm.value) + " 로 바꿉니다.\n"
                      + "속도 한계처럼 로봇 동작에 바로 영향을 주는 값일 수 있습니다."
                color: Theme.text2; font.pixelSize: Theme.fsBody
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Btn { text: "취소"; onClicked: confirm.close() }
                Btn {
                    text: "적용"; kind: "danger"
                    onClicked: {
                        tab.params.set(confirm.name, confirm.value)
                        confirm.close()   // 값은 응답 · /parameter_events 로 다시 맞춘다
                    }
                }
            }
        }
    }
}
