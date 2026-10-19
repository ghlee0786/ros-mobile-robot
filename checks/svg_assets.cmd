@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
setlocal
cd /d "%~dp0.."
python -I docs\assets\check_assets.py || exit /b 1
