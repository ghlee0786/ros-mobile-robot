import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import RobotDashboard

// 새 버전 알림 배너: '새 버전 vX.Y.Z' + [변경 내역] [다운로드하고 설치] [이 버전 건너뛰기] [나중에].
// 새 버전이 없을 때는 updater.message(수동 확인 결과 · 오류 · 주행 중 안내)를 [닫기] 와 함께 보인다.
Rectangle {
    id: root
    objectName: "updateBanner"
    required property Updater updater

    visible: updater.available || updater.downloading || updater.message !== ""
    implicitHeight: Theme.hLg
    implicitWidth: row.implicitWidth + Theme.sp4
    radius: Theme.rMd
    color: Theme.accentSubtle
    border.color: Theme.accent

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.sp2
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/download.svg"
            sourceSize: Qt.size(16, 16)
            color: Theme.accentText
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.updater.downloading ? root.updater.latestTag + " 내려받는 중 · "
                                               + Math.round(root.updater.progress * 100) + " %"
                : root.updater.message !== "" ? root.updater.message
                : "새 버전 " + root.updater.latestTag + " 이(가) 나왔습니다"
            color: Theme.accentText
            font.pixelSize: Theme.fsBody
            font.weight: Theme.wMedium
            font.features: Theme.numFeatures
        }
        Btn {
            visible: root.updater.available && !root.updater.downloading
            anchors.verticalCenter: parent.verticalCenter
            size: "sm"; kind: "ghost"; text: "변경 내역"
            onClicked: notes.open()
        }
        Btn {
            objectName: "updateInstall"
            visible: root.updater.available && !root.updater.downloading
            anchors.verticalCenter: parent.verticalCenter
            size: "sm"; kind: "accent"; text: "다운로드하고 설치"
            onClicked: root.updater.downloadAndInstall()
        }
        Btn {
            visible: root.updater.available && !root.updater.downloading
            anchors.verticalCenter: parent.verticalCenter
            size: "sm"; kind: "ghost"; text: "이 버전 건너뛰기"
            onClicked: root.updater.skip()
        }
        Btn {
            visible: !root.updater.downloading
            anchors.verticalCenter: parent.verticalCenter
            size: "sm"; kind: "ghost"; text: root.updater.available ? "나중에" : "닫기"
            onClicked: { root.updater.dismissMessage(); if (root.updater.available) root.updater.later() }
        }
    }

    // 변경 내역(릴리스 body, Markdown)
    Popup {
        id: notes
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round(parent.height * 0.12)
        width: 640
        height: Math.min(parent.height * 0.7, 560)
        padding: Theme.sp5
        modal: true
        background: Rectangle { radius: Theme.rXl; color: Theme.surface2; border.color: Theme.borderDefault }
        Overlay.modal: Rectangle { color: Theme.shadow; opacity: 0.5 }
        contentItem: Column {
            spacing: Theme.sp3
            Text {
                text: "변경 내역 · " + root.updater.latestTag
                color: Theme.text1; font.pixelSize: Theme.fsTitle; font.weight: Theme.wStrong
            }
            ScrollView {
                width: parent.width
                height: notes.availableHeight - Theme.hMd - 2 * Theme.sp3 - Theme.fsTitle * 1.5
                clip: true
                Text {
                    width: notes.availableWidth - Theme.sp3
                    text: root.updater.notes
                    textFormat: Text.MarkdownText
                    wrapMode: Text.Wrap
                    color: Theme.text1
                    font.pixelSize: Theme.fsBody
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
            }
            Btn { anchors.right: parent.right; text: "닫기"; onClicked: notes.close() }
        }
    }
}
