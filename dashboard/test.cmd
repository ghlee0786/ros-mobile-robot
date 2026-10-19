@echo off
rem 빌드 후 단위 시험(tests\tst_*.cpp)을 돌린다. 종료 코드 = ctest 결과.
setlocal
if "%QT_ROOT%"=="" set QT_ROOT=E:\Qt
call "%~dp0build.cmd" || exit /b 1
set PATH=%QT_ROOT%\6.8.3\mingw_64\bin;%QT_ROOT%\Tools\mingw1310_64\bin;%QT_ROOT%\Tools\CMake_64\bin;%PATH%
cd /d "%~dp0"
ctest --test-dir build --output-on-failure --timeout 120 || exit /b 1
