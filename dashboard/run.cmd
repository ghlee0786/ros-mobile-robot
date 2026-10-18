@echo off
rem 로봇 대시보드 실행. model\ 과 records\ 는 이 폴더 기준으로 찾고 저장한다.
setlocal
if "%QT_ROOT%"=="" set QT_ROOT=E:\Qt
set PATH=%QT_ROOT%\6.8.3\mingw_64\bin;%QT_ROOT%\Tools\mingw1310_64\bin;%PATH%
cd /d "%~dp0"
if not exist build\robot_dashboard.exe call build.cmd || exit /b 1
start "" build\robot_dashboard.exe
