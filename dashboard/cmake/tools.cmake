# 점검 도구 C++ 모델(P7a): 로그 · 진단 · 메시지 트리 · 파라미터 · rosapi 클라이언트.
target_sources(robot_dashboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/servicequeue.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/servicequeue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/logmodel.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/logmodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/diagnosticsmodel.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/diagnosticsmodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/messagetreemodel.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/messagetreemodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/paramclient.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/paramclient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/rosapiclient.h ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/rosapiclient.cpp
)
# QML 형식 등록 코드가 헤더를 파일 이름만으로 include 한다
target_include_directories(robot_dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/tools)
