@echo off
rem 설치 파일 만들기: Release 빌드 → dist\stage 에 배포(windeployqt) → Inno Setup 으로 dist\ROSMonitor-Setup-<ver>.exe
rem 버전은 dashboard\CMakeLists.txt 의 project(... VERSION x.y.z ...) 에서 읽는다.
setlocal
cd /d "%~dp0.."
if "%QT_ROOT%"=="" set QT_ROOT=E:\Qt

for /f "tokens=3" %%v in ('findstr /b /c:"project(robot_dashboard VERSION" dashboard\CMakeLists.txt') do set VER=%%v
if "%VER%"=="" (echo dashboard\CMakeLists.txt 에서 VERSION 을 찾지 못했습니다 & exit /b 1)
echo === ROS 모니터 %VER% 설치 파일 만들기

rem (1) Release 빌드
call dashboard\build.cmd || exit /b 1

rem (2) 배포 폴더: exe + Qt 런타임 · QML 모듈 · 플러그인
set PATH=%QT_ROOT%\6.8.3\mingw_64\bin;%QT_ROOT%\Tools\mingw1310_64\bin;%PATH%
if exist dist\stage rmdir /s /q dist\stage
mkdir dist\stage\model || exit /b 1
copy /y dashboard\build\robot_dashboard.exe dist\stage\ >nul || exit /b 1
windeployqt --release --qmldir dashboard\qml --no-translations --compiler-runtime dist\stage\robot_dashboard.exe >dist\windeployqt.log || (type dist\windeployqt.log & exit /b 1)
rem windeployqt 가 QML import 만으로는 넣지 않는 플러그인 확인(TLS · SVG · Quick3D)
for %%f in (tls\qschannelbackend.dll imageformats\qsvg.dll platforms\qwindows.dll Qt6Quick3D.dll Qt6Graphs.dll Qt6WebSockets.dll Qt6Svg.dll qml\QtQuick3D) do (
    if not exist dist\stage\%%f (echo 배포에 %%f 가 없습니다 & exit /b 1)
)

rem (3) 로봇 모델(glb 만, 원본 OBJ 는 넣지 않음) · 라이선스 · 안내
copy /y dashboard\model\*.glb dist\stage\model\ >nul || exit /b 1
copy /y dashboard\model\parts.json dist\stage\model\ >nul || exit /b 1
copy /y LICENSE dist\stage\LICENSE.txt >nul || exit /b 1
copy /y dashboard\README.md dist\stage\README.md >nul || exit /b 1

rem (4) Inno Setup: 사용자 폴더 → PATH → Program Files → E:\Tools\InnoSetup 순으로 ISCC 를 찾는다
set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" for %%i in (ISCC.exe) do if not "%%~$PATH:i"=="" set ISCC=%%~$PATH:i
if not exist "%ISCC%" set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" set ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" set ISCC=E:\Tools\InnoSetup\ISCC.exe
if not exist "%ISCC%" (echo Inno Setup 6 의 ISCC.exe 를 찾지 못했습니다 & exit /b 1)
"%ISCC%" /Q /DAppVersion=%VER% installer\robot_dashboard.iss || exit /b 1
echo === dist\ROSMonitor-Setup-%VER%.exe
