# 속도 한계: 파이 cmd_watchdog 의 max_forward · max_reverse · max_angular 를 읽고 쓰는 모델 · 조종 바의 '속도 한계' 패널
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/speedlimits.h ${CMAKE_CURRENT_SOURCE_DIR}/src/speedlimits.cpp)
qt_target_qml_sources(robot_dashboard
    QML_FILES
        qml/dash/SpeedLimitsPanel.qml
)
