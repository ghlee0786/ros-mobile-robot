# P3 Theme · 공통 컴포넌트 · 글꼴 · 아이콘 (루트 CMakeLists.txt 가 include 한다 → CMAKE_CURRENT_SOURCE_DIR = dashboard/)
find_package(Qt6 REQUIRED COMPONENTS Svg)

# 싱글턴 표시는 qt_target_qml_sources 보다 먼저 둬야 qmldir 에 singleton 으로 들어간다.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_target_qml_sources(robot_dashboard
    QML_FILES
        qml/Theme.qml
        qml/components/StatusChip.qml
        qml/components/Kbd.qml
        qml/components/EmptyState.qml
        qml/components/LoadingBar.qml
        qml/components/Skeleton.qml
        qml/components/IconBtn.qml
        qml/components/Metric.qml
        qml/components/Banner.qml
        qml/components/CommandPalette.qml
        qml/components/ActionBar.qml
        qml/components/StatusBar.qml
        qml/components/StatusExtraDash.qml
        qml/components/StatusExtraInspector.qml
)

# Pretendard Variable 은 수 MB 라 BIG_RESOURCES 로 넣는다. 경로: qrc:/fonts/..., qrc:/icons/...
qt_add_resources(robot_dashboard "dashboard_fonts"
    PREFIX "/"
    BIG_RESOURCES
    FILES
        fonts/PretendardVariable.ttf
        fonts/JetBrainsMono-Regular.ttf
        fonts/Pretendard-OFL.txt
        fonts/JetBrainsMono-OFL.txt
)

# lucide 아이콘(ISC): 복사할 때 stroke-width 1.5, stroke 흰색으로 바꿨다. 색은 icon.color / IconImage.color 로 입힌다.
qt_add_resources(robot_dashboard "dashboard_icons"
    PREFIX "/"
    FILES
        icons/plug.svg
        icons/wifi-off.svg
        icons/gauge.svg
        icons/play.svg
        icons/pause.svg
        icons/square.svg
        icons/octagon-alert.svg
        icons/map.svg
        icons/crosshair.svg
        icons/ruler.svg
        icons/navigation.svg
        icons/search.svg
        icons/command.svg
        icons/eye.svg
        icons/eye-off.svg
        icons/lock.svg
        icons/unlock.svg
        icons/download.svg
        icons/chevron-down.svg
        icons/lucide-LICENSE.txt
)

target_link_libraries(robot_dashboard PRIVATE Qt6::Svg)
