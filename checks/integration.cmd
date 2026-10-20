@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
setlocal
cd /d "%~dp0.."
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
set PYDEPS=--with "websockets>=12,<14" --with websocket-client --with pytest --with pillow
call dashboard\test.cmd || exit /b 1
python -I dashboard\tools\smoke.py --seconds 12 || exit /b 1
uv run --no-project %PYDEPS% python -m pytest tooling\mockbridge\tests -q || exit /b 1
rem Merge checks skip the 10-minute soak and run 4 scenarios at a time. FULL=1 runs everything (soak included).
if "%FULL%"=="1" (
  uv run --no-project %PYDEPS% python tooling\mockbridge\run_scenarios.py --offline --jobs 4 %* || exit /b 1
  exit /b 0
)
for /f %%i in ('python -I checks\scenario_ids.py') do set ONLY=%%i
uv run --no-project %PYDEPS% python tooling\mockbridge\run_scenarios.py --offline --jobs 4 --only %ONLY% %* || exit /b 1
