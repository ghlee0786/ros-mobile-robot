import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RobotDashboard

// 데이터 점검: 토픽 · 로그 · 진단 · 노드 · 서비스 · 파라미터 · 기록 · 시험 탭과 경보 목록.
// 안 보이는 탭은 구독 · 조회를 켜지 않는다. 시험 훅 state 덤프용으로 objectName "inspector" 와 읽기 전용 요약 속성을 둔다.
Item {
    id: page
    objectName: "inspector"
    required property Robot robot
    property string tab: "토픽"   // 시험 actions 에서 {"set": "inspector.tab", "value": "로그"} 로 바꾼다
    readonly property var tabs: ["토픽", "로그", "진단", "노드", "서비스", "파라미터", "기록·시험"]

    // 요약(읽기 전용)
    readonly property int logCount: logModel.count
    readonly property int logShownCount: logFilter.count
    readonly property int diagCount: diagModel.count
    readonly property int diagStaleCount: diagModel.staleCount
    readonly property int diagErrorCount: diagModel.errorCount
    readonly property bool diagSynthetic: diagModel.synthetic
    readonly property bool diagStreamLost: diagModel.streamLost
    readonly property int alertCount: Alerts.count
    readonly property string inspectTopic: msgTree.topic
    readonly property int nodeCount: rosApi.nodes.length
    readonly property int serviceCount: rosApi.services.length
    readonly property string rosapiError: rosApi.error
    readonly property string serviceCallError: rosApi.callError
    readonly property int paramCount: paramClient.count
    readonly property bool paramLastSuccessful: paramClient.lastSuccessful
    readonly property int callsPending: rosApi.callsPending

    function shows(name) { return page.visible && page.tab === name }

    LogModel { id: logModel; objectName: "logModel"; bridge: page.robot.bridge; active: page.shows("로그") }
    LogFilterModel { id: logFilter; objectName: "logFilter"; sourceModel: logModel }
    DiagnosticsModel { id: diagModel; objectName: "diagModel"; bridge: page.robot.bridge; active: page.shows("진단") }
    MessageTreeModel { id: msgTree; objectName: "msgTree"; bridge: page.robot.bridge }
    MessageTreeModel { id: responseTree; objectName: "responseTree" }
    RosApiClient { id: rosApi; objectName: "rosApi"; bridge: page.robot.bridge }
    ParamClient {
        id: paramClient
        objectName: "paramClient"
        bridge: page.robot.bridge
        active: page.shows("파라미터")
        writable: !page.robot.bridge.noPublish
    }

    // ---- 경보: 조건은 1 s 마다 다시 본다. bridgeError 는 30 s 동안 남긴다. ----
    property double bridgeErrorAt: 0
    Connections {
        target: page.robot.bridge
        function onBridgeError(text) {
            page.bridgeErrorAt = Date.now()
            Alerts.set("bridgeError", "warn", "rosbridge: " + text, "서비스 이름 · 요청 형식을 확인한다. 30 s 뒤 사라진다.")
        }
    }
    function refreshAlerts() {
        const r = page.robot
        const connected = r.bridge.connected
        Alerts.update("linkLost", r.linkLost, "error", "로봇 연결 끊김",
                      "파이 전원 · Wi-Fi · rosbridge(9090)를 확인한다.")
        Alerts.update("missingRequired", connected && r.missingRequired.length > 0, "error",
                      "필수 토픽 없음: " + r.missingRequired.join(", "),
                      "노드 탭에서 그 토픽을 내는 노드가 떠 있는지 본다.")
        Alerts.update("watchdog", connected && !r.watchdogOk, "warn", "워치독 확인 안 됨",
                      "펌웨어 워치독(/link 하트비트)이 오는지 진단 탭에서 본다.")
        Alerts.update("diagError", diagModel.errorCount > 0 && !diagModel.synthetic, "error",
                      "진단 ERROR " + diagModel.errorCount + "개", "진단 탭의 Error 그룹을 연다.")
        Alerts.update("rosapiSlow", rosApi.rosapiSlow, "warn", "rosapi 응답 지연: " + rosApi.error,
                      "파이에서 rosapi 노드가 떠 있는지 본다(rosbridge launch).")
        if (page.bridgeErrorAt > 0 && Date.now() - page.bridgeErrorAt > 30000) {
            Alerts.remove("bridgeError")
            page.bridgeErrorAt = 0
        }
    }
    Timer { interval: 1000; running: true; repeat: true; triggeredOnStart: true; onTriggered: page.refreshAlerts() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp4
        anchors.leftMargin: Theme.sp5
        anchors.rightMargin: Theme.sp5
        spacing: Theme.sp3

        // 경보 목록(tip 포함)
        Panel {
            Layout.fillWidth: true
            visible: Alerts.count > 0
            title: "경보"
            subtitle: Alerts.count + "개"
            Repeater {
                model: Alerts.items
                RowLayout {
                    required property string severity
                    required property string message
                    required property string tip
                    required property string time
                    Layout.fillWidth: true
                    spacing: Theme.sp2
                    StatusChip {
                        kind: severity === "error" ? "danger" : severity === "warn" ? "warn" : "neutral"
                        text: severity === "error" ? "오류" : severity === "warn" ? "주의" : "알림"
                    }
                    Text { text: message; color: Theme.text1; font.pixelSize: Theme.fsBody; elide: Text.ElideRight; Layout.maximumWidth: 520 }
                    Text { text: tip; color: Theme.text2; font.pixelSize: Theme.fsSmall; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: time; color: Theme.text3; font.family: Theme.monoFamily; font.pixelSize: Theme.fsCaption }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp3
            Seg {
                options: page.tabs
                current: page.tab
                onPicked: v => page.tab = v
            }
            StatusChip { visible: diagModel.active && diagModel.streamLost; kind: "danger"; text: "진단 끊김" }
            Item { Layout.fillWidth: true }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.tabs.indexOf(page.tab)
            TopicsTab { robot: page.robot; tree: msgTree; active: page.shows("토픽") }
            LogTab { log: logModel; filter: logFilter }
            DiagTab { robot: page.robot; diag: diagModel }
            NodesTab { robot: page.robot; api: rosApi; active: page.shows("노드") }
            ServicesTab { api: rosApi; response: responseTree; active: page.shows("서비스") }
            ParamsTab { params: paramClient; api: rosApi; active: page.shows("파라미터") }
            RecordTab { robot: page.robot }
        }
    }
}
