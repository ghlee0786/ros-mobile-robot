cmake_minimum_required(VERSION 3.21)
project(robot_dashboard LANGUAGES CXX)

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
