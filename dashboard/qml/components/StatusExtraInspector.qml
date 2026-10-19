import QtQuick
import RobotDashboard

// 점검 도구 묶음(P7)이 헤더에 칩을 더하는 자리. StatusBar 가 만든다.
// 전체 수신 Mbps 와 경보 개수(점검 화면이 채우는 Alerts 싱글턴).
Row {
    id: extra
    required property Robot robot
    spacing: Theme.sp2

    StatusChip {
        visible: extra.robot.bridge.connected
        kind: "neutral"
        mono: true
        text: (extra.robot.bridge.rxBytesPerSec * 8 / 1e6).toFixed(2) + " Mbps"
    }
    StatusChip {
        visible: Alerts.count > 0
        kind: "warn"
        text: "경보 " + Alerts.count
    }
}
