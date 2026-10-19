# P2 시험 훅: 명령줄 시험 모드(AppConfig 싱글턴 · 로그 · actions · 캡처 · state 덤프)
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/appconfig.h ${CMAKE_CURRENT_SOURCE_DIR}/src/appconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/testhooks.h ${CMAKE_CURRENT_SOURCE_DIR}/src/testhooks.cpp)
if(WIN32)
    target_link_libraries(robot_dashboard PRIVATE psapi)   # state 의 proc(GetProcessMemoryInfo)
endif()
