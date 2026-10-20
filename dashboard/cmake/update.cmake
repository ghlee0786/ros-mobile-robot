# 버전 · 업데이트 확인(GitHub Releases) · 알림 배너. 버전은 CMakeLists.txt 의 project(VERSION) 하나에서 온다.
target_compile_definitions(robot_dashboard PRIVATE ROBOT_DASHBOARD_VERSION="${PROJECT_VERSION}")
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/update/updater.h ${CMAKE_CURRENT_SOURCE_DIR}/src/update/updater.cpp)
# QML 형식 등록 코드가 헤더를 파일 이름만으로 include 한다
target_include_directories(robot_dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/update)
qt_target_qml_sources(robot_dashboard
    QML_FILES
        qml/components/UpdateBanner.qml
)
