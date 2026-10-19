import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 서비스 탭: /rosapi/services 목록 · /rosapi/service_type · JSON 요청 편집(검증 · 오류 위치) · 호출 · 응답 트리.
RowLayout {
    id: tab
    required property RosApiClient api
    required property MessageTreeModel response
    property bool active: false
    readonly property var check: api.checkJson(request.text)
    spacing: Theme.sp4

    onActiveChanged: if (active && api.services.length === 0) api.refreshServices()
    Connections {
        target: tab.api
        function onResponseChanged() {
            tab.response.clear()
            if (Object.keys(tab.api.response).length > 0)
                tab.response.setMessage(tab.api.response)
        }
    }

    function pick(name, body) {
        service.text = name
        tab.api.fetchServiceType(name)
        if (body !== undefined)
            request.text = body
    }

    Panel {
        Layout.preferredWidth: 520
        Layout.fillHeight: true
        title: "서비스 호출"
        headerExtra: Btn { size: "sm"; text: "목록 새로고침"; onClicked: tab.api.refreshServices() }

        LoadingBar { Layout.fillWidth: true; running: tab.api.callsPending > 0 }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: combo
                Layout.fillWidth: true
                model: tab.api.services
                displayText: currentIndex >= 0 ? currentText : "서비스 고르기 (" + tab.api.services.length + "개)"
                currentIndex: -1
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fsBody
                onActivated: tab.pick(currentText)
            }
        }
        InField { id: service; Layout.fillWidth: true; mono: true; placeholderText: "/서비스/이름"; onAccepted: tab.api.fetchServiceType(text) }
        Row {
            spacing: Theme.sp2
            Text { text: "프리셋"; color: Theme.text2; font.pixelSize: Theme.fsSmall; anchors.verticalCenter: parent.verticalCenter }
            Btn { size: "sm"; text: "지도 저장"; onClicked: tab.pick("/slam_toolbox/save_map", '{\n  "name": {"data": "map"}\n}') }
            Btn { size: "sm"; text: "내비 목표 취소"; onClicked: tab.pick("/navigate_to_pose/_action/cancel_goal", "{}") }
        }
        Text {
            text: "형식: " + (tab.api.serviceType !== "" ? tab.api.serviceType : "—")
            color: Theme.text2
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsSmall
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: request
                text: "{}"
                color: Theme.text1
                selectionColor: Theme.accent
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fsBody
                background: Rectangle {
                    radius: Theme.rSm
                    color: Theme.surface2
                    border.color: tab.check.ok ? Theme.borderDefault : Theme.danger
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !tab.check.ok
            wrapMode: Text.WordWrap
            text: "JSON 오류 · " + tab.check.line + "줄 " + tab.check.column + "칸: " + tab.check.error
            color: Theme.dangerText
            font.pixelSize: Theme.fsSmall
        }
        RowLayout {
            Layout.fillWidth: true
            Btn {
                text: "호출"
                kind: "accent"
                enabled: tab.check.ok && service.text.trim() !== ""
                onClicked: tab.api.callService(service.text.trim(), request.text)
            }
            Btn {
                text: "오류 위치로"
                visible: !tab.check.ok
                onClicked: { request.forceActiveFocus(); request.cursorPosition = tab.check.offset }
            }
        }
    }

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: "응답"
        subtitle: tab.api.responseMs >= 0 ? tab.api.responseMs + " ms" : ""
        Text {
            Layout.fillWidth: true
            visible: tab.api.callError !== ""
            wrapMode: Text.WordWrap
            text: tab.api.callError
            color: Theme.dangerText
            font.pixelSize: Theme.fsBody
        }
        MessageTree {
            Layout.fillWidth: true
            Layout.fillHeight: true
            tree: tab.response
        }
    }
}
