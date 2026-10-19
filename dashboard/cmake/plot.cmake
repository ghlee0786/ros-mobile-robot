# P5 플롯 엔진(src/plot/): PlotSeries · PlotFeeder QML 타입. 시험은 tests/tst_plot_*.cpp.
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/messagepath.h ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/messagepath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/timebuffer.h ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/timebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/transforms.h ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/transforms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/plotseries.h ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/plotseries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/plotfeeder.h ${CMAKE_CURRENT_SOURCE_DIR}/src/plot/plotfeeder.cpp
)
target_include_directories(robot_dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/plot)
