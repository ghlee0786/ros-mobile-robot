@echo off
rem 로봇 대시보드 빌드 (Qt 6.8.3 MinGW). 결과: dashboard\build\robot_dashboard.exe
setlocal
if "%QT_ROOT%"=="" set QT_ROOT=E:\Qt
set PATH=%QT_ROOT%\6.8.3\mingw_64\bin;%QT_ROOT%\Tools\mingw1310_64\bin;%QT_ROOT%\Tools\CMake_64\bin;%QT_ROOT%\Tools\Ninja;%PATH%
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QT_ROOT%\6.8.3\mingw_64 || exit /b 1
cmake --build build || exit /b 1
