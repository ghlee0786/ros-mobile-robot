# 단위 시험. tst_<이름>.cpp 하나가 시험 실행 파일 하나다(QtTest, offscreen).
# - 모든 시험은 dashboard_core(src/*.cpp 에서 앱 진입점 · QtQuick3D 렌더 보조를 뺀 것)를 link 한다.
# - tst_<묶음>_*.cpp 는 src/<묶음>/*.cpp 도 함께 빌드한다(예: tst_plot_buffer.cpp → src/plot/*.cpp).
find_package(Qt6 REQUIRED COMPONENTS Test Quick Quick3D WebSockets Network Concurrent Graphs)

file(GLOB DASHBOARD_CORE_SRC CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.h)
list(FILTER DASHBOARD_CORE_SRC EXCLUDE REGEX "/(main|maptexture|pointinstancing|appconfig|testhooks)\.(cpp|h)$")
add_library(dashboard_core STATIC ${DASHBOARD_CORE_SRC})
target_include_directories(dashboard_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dashboard_core PUBLIC Qt6::Quick Qt6::WebSockets Qt6::Network Qt6::Concurrent Qt6::Graphs)

file(GLOB DASHBOARD_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp)
foreach(src IN LISTS DASHBOARD_TESTS)
    get_filename_component(name ${src} NAME_WE)
    set(group_src)
    if(name MATCHES "^tst_([a-z0-9]+)_")
        file(GLOB group_src CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/${CMAKE_MATCH_1}/*.cpp
                                              ${PROJECT_SOURCE_DIR}/src/${CMAKE_MATCH_1}/*.h)
    endif()
    add_executable(${name} ${src} ${group_src})
    target_link_libraries(${name} PRIVATE dashboard_core Qt6::Test Qt6::Quick Qt6::Quick3D)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endforeach()
