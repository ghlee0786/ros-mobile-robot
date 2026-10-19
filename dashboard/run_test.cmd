@echo off
rem 로봇 대시보드를 시험 모드 인자로 실행하고 끝날 때까지 기다려 종료 코드를 돌려준다. 인자는 robot_dashboard.exe --help 참고.
setlocal
if "%QT_ROOT%"=="" set QT_ROOT=E:\Qt
set PATH=%QT_ROOT%\6.8.3\mingw_64\bin;%QT_ROOT%\Tools\mingw1310_64\bin;%PATH%
"%~dp0build\robot_dashboard.exe" %*
exit /b %ERRORLEVEL%
