# 앱 아이콘 '아이소 로봇' — exe 아이콘(Windows 자원)과 창 · 작업 표시줄 아이콘(qrc:/icons/app.png, main.cpp).
# 원본은 icons/app.svg, app.ico · app.png 는 그것을 256 px 로 그려 만든 것(16~256 px 묶음).
if(WIN32)
    target_sources(robot_dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icons/app.rc)
endif()
qt_add_resources(robot_dashboard "dashboard_app_icon" PREFIX "/" FILES icons/app.png)
