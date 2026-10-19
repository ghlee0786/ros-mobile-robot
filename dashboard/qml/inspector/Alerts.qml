pragma Singleton
import QtQuick

// 경보 목록: id 로 넣고 뺀다(severity error · warn · info, message, tip, 처음 뜬 시각).
// 점검 화면(InspectorPage)이 채우고, 상태 바 배지(StatusExtraInspector)와 점검 화면 상단 목록이 함께 본다.
QtObject {
    readonly property ListModel items: ListModel {}
    readonly property int count: items.count

    function indexOf(id) {
        for (let i = 0; i < items.count; ++i)
            if (items.get(i).alertId === id)
                return i
        return -1
    }
    function set(id, severity, message, tip) {
        const i = indexOf(id)
        if (i >= 0) {
            items.setProperty(i, "severity", severity)
            items.setProperty(i, "message", message)
            items.setProperty(i, "tip", tip)
        } else {
            items.append({ alertId: id, severity: severity, message: message, tip: tip,
                           time: Qt.formatTime(new Date(), "HH:mm:ss") })
        }
    }
    function remove(id) {
        const i = indexOf(id)
        if (i >= 0)
            items.remove(i)
    }
    function update(id, on, severity, message, tip) {
        on ? set(id, severity, message, tip) : remove(id)
    }
}
