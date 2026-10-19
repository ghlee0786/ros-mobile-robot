pragma Singleton
import QtCore
import QtQuick

// 디자인 토큰 하나뿐인 곳. 속성 이름은 다른 묶음과의 약속이므로 바꾸지 않는다.
// 색은 dark 에 따라 다크/라이트 값을 고른다(3D 배경 canvas3d 는 라이트에서도 어둡다). Qt 의 알파 색은 #AARRGGBB.
//
// 명암비(WCAG 2.x, 글자/배경):
//   다크  text1  base 15.4 · surface1 14.5 · surface2 13.2 · surface3 11.6
//         text2  base 7.9 · surface1 7.4 · surface2 6.7 · surface3 5.9
//         text3  base 5.1 · surface1 4.8 · surface2 4.4 · surface3 3.9 (surface3 위에서는 text2 이상을 쓴다)
//         accentText/surface1 8.2 · okText 9.1 · warnText 10.4 · dangerText 7.4
//         accentText/accentSubtle 6.5 · okText/okSubtle 7.3 · warnText/warnSubtle 7.5 · dangerText/dangerSubtle 6.7
//         textOnAccent/accent 4.7 · textOnAccent/danger 4.5
//   라이트 text1  base 16.6 · surface1 17.8 · surface2 15.8 · surface3 14.6
//         text2  base 6.5 · surface1 7.0 · surface2 6.2 · surface3 5.7
//         text3  base 5.0 · surface1 5.4 · surface2 4.8 · surface3 4.4
//         accentText/surface1 6.7 · okText 5.0 · warnText 5.4 · dangerText 5.3
//         accentText/accentSubtle 5.6 · okText/okSubtle 4.4 · warnText/warnSubtle 4.9 · dangerText/dangerSubtle 4.5
//         textOnAccent/accent 5.2 · textOnAccent/danger 5.3
QtObject {
    id: theme

    // ---- 설정 (QtCore Settings, category "theme") ----
    property Settings store: Settings {
        id: store
        category: "theme"
        property bool reduceMotion: false
        property bool largeText: false
        property bool followSystem: false   // 기본은 다크(3D 화면과 맞춤). 시스템 테마를 따르려면 true
        property bool darkChoice: true   // followSystem 이 꺼졌을 때 쓰는 값
    }
    property alias reduceMotion: store.reduceMotion
    property alias largeText: store.largeText
    property alias followSystem: store.followSystem

    readonly property bool dark: followSystem ? Qt.styleHints.colorScheme !== Qt.ColorScheme.Light : store.darkChoice
    function toggleDark() { store.darkChoice = !dark; followSystem = false }

    // ---- 색 ----
    readonly property color canvas: dark ? "#0B0D10" : "#EEF0F3"
    readonly property color canvas3d: "#0B0D10"
    readonly property color base: dark ? "#111318" : "#F6F7F9"
    readonly property color surface1: dark ? "#171A20" : "#FFFFFF"
    readonly property color surface2: dark ? "#1E2229" : "#F0F2F5"
    readonly property color surface3: dark ? "#272C34" : "#E6E9EE"
    readonly property color borderSubtle: dark ? "#0FFFFFFF" : "#0F000000"
    readonly property color borderDefault: dark ? "#1AFFFFFF" : "#1A000000"
    readonly property color borderStrong: dark ? "#33FFFFFF" : "#33000000"
    readonly property color text1: dark ? "#E8EAED" : "#16181D"
    readonly property color text2: dark ? "#A3A9B2" : "#525A66"
    readonly property color text3: dark ? "#7F8792" : "#636B77"
    readonly property color textOnAccent: "#FFFFFF"
    readonly property color accent: dark ? "#2F6FE4" : "#2563EB"
    readonly property color accentHover: dark ? "#3D7FF5" : "#1D4ED8"
    readonly property color accentText: dark ? "#7DB4FF" : "#1D4ED8"
    readonly property color accentSubtle: dark ? "#1A2C4A" : "#E3ECFD"
    readonly property color ok: dark ? "#2EA043" : "#1A7F4B"
    readonly property color okText: dark ? "#6CCF8E" : "#1A7F4B"
    readonly property color okSubtle: dark ? "#17302A" : "#E3F4EA"
    readonly property color warn: dark ? "#D29922" : "#B26B00"
    readonly property color warnText: dark ? "#F2C14E" : "#9A5B00"
    readonly property color warnSubtle: dark ? "#3A3320" : "#FDF3DC"
    readonly property color danger: dark ? "#D93A40" : "#C8303A"
    readonly property color dangerHover: dark ? "#E5484D" : "#B42830"
    readonly property color dangerText: dark ? "#FF8589" : "#C8303A"
    readonly property color dangerSubtle: dark ? "#3A1A1C" : "#FCE7E8"
    readonly property color dangerBorder: "#5C2427"
    readonly property color hudFill: "#E0171A20"
    // hudFill · canvas3d 는 라이트에서도 어두우므로 그 위 글자는 다크 값으로 고정한다
    readonly property color hudText1: "#E8EAED"
    readonly property color hudText2: "#A3A9B2"
    readonly property color shadow: "#80000000"   // 떠 있는 요소의 MultiEffect shadowColor

    // ---- 데이터 색 (UI 색과 분리) ----
    readonly property var series: ["#4C9AFF", "#F2994A", "#56CCB2", "#BB86FC", "#F2C94C"]
    readonly property color axisX: "#E5534B"
    readonly property color axisY: "#57AB5A"
    readonly property color axisZ: "#539BF5"
    readonly property color lidar: "#E0457B"   // 이동 경로(trail 청록)와 구분되게 기존 분홍을 유지한다
    readonly property color trail: "#56CCB2"
    readonly property color plan: "#57AB5A"
    readonly property color goalDrag: "#F2C94C"
    readonly property color initialPose: "#BB86FC"
    // 3D 재질 · 조명 · 뷰 큐브 (UI 테마와 무관하게 고정)
    readonly property color matAluminum: "#C9CFD6"
    readonly property color matDarkPlate: "#2A2F36"
    readonly property color matRubber: "#1B1F25"
    readonly property color matHub: "#8A939E"
    readonly property color matLidarBody: "#3C434C"
    readonly property color matLidarTop: "#5A6370"
    readonly property color matGround: "#121820"
    readonly property color sceneAmbient: "#888888"
    readonly property color viewCubeFace: "#2A3340"
    readonly property color viewCubeEdge: "#7FD3F0"

    // ---- 글꼴 (Main.qml 의 FontLoader 가 읽기에 성공하면 이름을 바꾼다) ----
    property string fontFamily: "Malgun Gothic"
    property string monoFamily: "Consolas"
    readonly property real scale: largeText ? 1.15 : 1.0
    readonly property int fsCaption: Math.round(11 * scale)
    readonly property int fsSmall: Math.round(12 * scale)
    readonly property int fsBody: Math.round(13 * scale)
    readonly property int fsLabel: Math.round(14 * scale)
    readonly property int fsTitle: Math.round(16 * scale)
    readonly property int fsHeading: Math.round(20 * scale)
    readonly property int fsMetric: Math.round(28 * scale)
    readonly property int wRegular: Font.Normal
    readonly property int wMedium: Font.Medium
    readonly property int wStrong: Font.DemiBold
    readonly property var numFeatures: ({ "tnum": 1 })

    // ---- 간격 · 컨트롤 높이 ----
    readonly property int sp1: 4
    readonly property int sp2: 8
    readonly property int sp3: 12
    readonly property int sp4: 16
    readonly property int sp5: 20
    readonly property int sp6: 24
    readonly property int sp8: 32
    readonly property int spNudge: 6
    readonly property int hXs: 24
    readonly property int hSm: 32
    readonly property int hMd: 36
    readonly property int hLg: 40
    readonly property int hEstop: 44

    // ---- 반경 ----
    readonly property int rXs: 4
    readonly property int rSm: 6
    readonly property int rMd: 8
    readonly property int rLg: 10
    readonly property int rXl: 16
    readonly property int rPill: 999

    // ---- 모션 (reduceMotion 이면 모두 0) ----
    readonly property int durFast: reduceMotion ? 0 : 100
    readonly property int durBase: reduceMotion ? 0 : 150
    readonly property int durMed: reduceMotion ? 0 : 200
    readonly property int durSlow: reduceMotion ? 0 : 250
    readonly property int durCamera: reduceMotion ? 0 : 300
    readonly property var easeOutCurve: [0, 0, 0.2, 1, 1, 1]

    // ---- 상태 레이어 ----
    readonly property real stHover: 0.08
    readonly property real stPressed: 0.10
    readonly property real stFocus: 0.10
    readonly property real stDragged: 0.16
    readonly property real opDisabled: 0.4
}
