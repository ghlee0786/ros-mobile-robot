import QtQuick
import RobotDashboard

// 헤더 칩 묶음: 연결 · 지연, 토픽 주기, 워치독. 다른 묶음은 StatusExtraDash / StatusExtraInspector 에 칩을 더한다.
Flow {
    id: bar
    required property Robot robot
    default property alias extra: tail.data
    spacing: Theme.sp2

    StatusChip {
        kind: bar.robot.bridge.connected ? "ok" : "danger"
        text: "rosbridge " + bar.robot.bridge.host + ":" + bar.robot.bridge.port + " · "
              + (bar.robot.bridge.connected ? "연결됨 · " + bar.robot.bridge.latencyMs + " ms" : "연결 끊김 · 재연결 중")
    }
    Repeater {
        model: bar.robot.rates
        StatusChip {
            required property string modelData
            kind: modelData.endsWith("—") ? "stale" : "neutral"
            mono: true
            text: modelData
        }
    }
    StatusChip {
        kind: bar.robot.watchdogOk ? "ok" : "warn"
        text: bar.robot.watchdogOk ? "워치독 정상" : "워치독 확인 안 됨"
    }
    StatusExtraDash { robot: bar.robot }
    StatusExtraInspector { robot: bar.robot }
    Row { id: tail; spacing: Theme.sp2 }
}
