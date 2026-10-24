cmake_minimum_required(VERSION 3.21)
project(robot_dashboard VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Quick3D WebSockets Graphs Network)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(robot_dashboard WIN32 src/main.cpp)

qt_add_qml_module(robot_dashboard
    URI RobotDashboard
    SOURCES
        src/rosbridge.h src/rosbridge.cpp
        src/robot.h src/robot.cpp
        src/pointinstancing.h src/pointinstancing.cpp
        src/maptexture.h src/maptexture.cpp
    QML_FILES
        qml/Main.qml
        qml/Panel.qml
        qml/Seg.qml
        qml/Btn.qml
        qml/DashboardPage.qml
        qml/SceneView.qml
        qml/RobotModel.qml
        qml/InspectorPage.qml
        qml/ConnectPage.qml
)

target_include_directories(robot_dashboard PRIVATE src)
target_link_libraries(robot_dashboard PRIVATE
    Qt6::Quick Qt6::Quick3D Qt6::WebSockets Qt6::Graphs Qt6::Network)

# 기능 묶음마다 새 소스 · QML 은 cmake/<묶음>.cmake 에 등록한다(target_sources · qt_target_qml_sources 로
# robot_dashboard 에 더한다). 위의 목록은 고치지 않는다 — 여러 작업이 동시에 고쳐도 겹치지 않게.
file(GLOB DASHBOARD_EXT_CMAKE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(ext IN LISTS DASHBOARD_EXT_CMAKE)
    include(${ext})
endforeach()

# 단위 시험(tests/tst_*.cpp) — dashboard\test.cmd 가 빌드 후 ctest 로 돌린다.
enable_testing()
add_subdirectory(tests)
