# P5 플롯 엔진 (dashboard/src/plot/)

## 목표
QML 30Hz append/remove 그래프를 대체할 C++ 엔진: messagepath · timebuffer · transforms · PlotSeries · PlotFeeder (+ fuzzyScore · flattenNumericPaths).
QML 은 고치지 않는다. 등록은 dashboard/cmake/plot.cmake, 시험은 dashboard/tests/tst_plot_*.cpp (W0 규칙). 검사: checks\p5_plot.cmd (= dashboard\test.cmd).

## 진행
- [x] messagepath.{h,cpp} (+ fuzzyScore, flattenNumericPaths)
- [x] timebuffer.{h,cpp}
- [x] transforms.{h,cpp}
- [x] plotseries.{h,cpp}
- [x] plotfeeder.{h,cpp}
- [x] cmake/plot.cmake
- [x] tests/tst_plot_{messagepath,timebuffer,transforms,feeder}.cpp
- [x] build + checks\p5_plot.cmd 통과(5/5), build/RobotDashboard/robot_dashboard.qmltypes 에 PlotSeries·PlotFeeder 확인, smoke.py(시험 모드 8 s) 오류 0

## 남은 일
- 없음. (P6 가 QML 에서 PlotFeeder{ series: [...]; targets: [LineSeries...] } 로 쓴다.)

## 결정·발견
- DECISIONS.md 참고.
- 현재 RosBridge::want 는 throttle 을 마지막 호출 값으로 덮어쓴다(G7). P4 가 하한 표를 넣는다는 약속에 따라 PlotSeries 는 -1 을 그대로 넘긴다.
