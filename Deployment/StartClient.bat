@echo off
setlocal
cd /d "%~dp0"

set "SERVER_ADDRESS=%~1"
if not defined SERVER_ADDRESS set /p "SERVER_ADDRESS=Server address [127.0.0.1:7777]: "
if not defined SERVER_ADDRESS set "SERVER_ADDRESS=127.0.0.1:7777"

echo Connecting to %SERVER_ADDRESS%...
start "" "%~dp0BrokenEdge.exe" "%SERVER_ADDRESS%" -windowed -log
exit /b 0
