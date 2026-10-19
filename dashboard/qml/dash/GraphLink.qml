import QtQuick

// 페이지 전역 링크 줌: linked 이면 한 그래프의 X 확대 · 이동 · 트래커 · 멈춤을 다른 그래프에 같이 적용한다(Y 는 각자).
QtObject {
    property bool linked: false
    property real xMin: 0
    property real xMax: 0
    signal zoomed(var src, real x0, real x1)
    signal tracked(var src, real t)
    signal pauseSet(var src, bool paused)
    onZoomed: (src, x0, x1) => { xMin = x0; xMax = x1 }
}
