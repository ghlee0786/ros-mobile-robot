@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
setlocal
cd /d "%~dp0.."
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
uv run --no-project --with pytest --with numpy --with trimesh --with fast-simplification python -m pytest tooling\fusion\tests -q || exit /b 1
