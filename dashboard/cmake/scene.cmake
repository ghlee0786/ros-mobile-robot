# 3D 장면 모델(P8): LiDAR 잔상 · TF 트리 · AMCL 공분산 · 코스트맵 · footprint.
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scenemath.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scenemath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scenesource.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scenesource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scanaccumulator.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scanaccumulator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/colorpointinstancing.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/colorpointinstancing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/tftree.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/tftree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/amclpose.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/amclpose.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/costmaplayer.h ${CMAKE_CURRENT_SOURCE_DIR}/src/scene/costmaplayer.cpp
)
# qmltyperegistrar 가 만든 등록 파일이 헤더를 파일 이름만으로 include 한다.
target_include_directories(robot_dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/scene)
