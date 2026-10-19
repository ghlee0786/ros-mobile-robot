@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
setlocal
cd /d "%~dp0.."
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
set PYDEPS=--with "websockets>=12,<14" --with websocket-client --with pytest --with pillow
call dashboard\test.cmd || exit /b 1
python -I dashboard\tools\smoke.py --seconds 12 || exit /b 1
uv run --no-project %PYDEPS% python -m pytest tooling\mockbridge\tests -q || exit /b 1
uv run --no-project %PYDEPS% python tooling\mockbridge\run_scenarios.py --offline %* || exit /b 1
