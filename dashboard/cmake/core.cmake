# P4 코어: 메시지 해석 순수 함수(msgparse) · 표 모델(topicmodel), 큰 메시지 처리용 Qt Concurrent
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/msgparse.h ${CMAKE_CURRENT_SOURCE_DIR}/src/msgparse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/topicmodel.h ${CMAKE_CURRENT_SOURCE_DIR}/src/topicmodel.cpp)
target_link_libraries(robot_dashboard PRIVATE Qt6::Concurrent)
