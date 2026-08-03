@echo off
setlocal
cd /d "%~dp0"

set "SERVER_PORT=%~1"
if not defined SERVER_PORT set "SERVER_PORT=7777"

echo Starting BrokenEdge dedicated server on UDP port %SERVER_PORT%...
"%~dp0BrokenEdgeServer.exe" -log -port=%SERVER_PORT% -unattended

set "SERVER_EXIT_CODE=%ERRORLEVEL%"
echo BrokenEdge dedicated server exited with code %SERVER_EXIT_CODE%.
pause
exit /b %SERVER_EXIT_CODE%
