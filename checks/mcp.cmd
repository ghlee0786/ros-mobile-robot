@echo off
rem Harness task check (run by cmd.exe, no bash syntax). Exit code 0 = pass.
rem ROS monitor MCP server + register tool tests on the mock rosbridge (no robot, no real user config).
setlocal
cd /d "%~dp0.."
if "%UV_CACHE_DIR%"=="" set UV_CACHE_DIR=E:\Temp\uv-cache
uv run --no-project --with "mcp>=1.2" --with "websockets>=12,<14" --with pytest python -m pytest tooling\mcp -q -p no:cacheprovider || exit /b 1
