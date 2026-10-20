@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
rem Unit tests, then build the installer, install it silently into a temp folder, smoke-run it, upgrade over it, uninstall.
setlocal
cd /d "%~dp0.."
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
call dashboard\test.cmd || exit /b 1
call installer\build_installer.cmd || exit /b 1
python -I installer\test_installer.py || exit /b 1
