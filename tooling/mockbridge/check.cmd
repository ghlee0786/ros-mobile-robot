@echo off
rem 모의 rosbridge 계약 시험(pytest). 종료 코드 0 = 통과. cmd.exe 로 실행한다.
setlocal
cd /d "%~dp0"
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
uv run --no-project --with "websockets>=12,<14" --with websocket-client --with pytest --with pillow python -m pytest tests -q || exit /b 1
