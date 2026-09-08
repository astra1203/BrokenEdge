@echo off
setlocal
cd /d "%~dp0"

set "SERVER_PORT=%~1"
if not defined SERVER_PORT set "SERVER_PORT=7777"
set "SERVER_MATCH=%~2"
if not defined SERVER_MATCH set "SERVER_MATCH=FreeForAll"
set "SERVER_MIN_PLAYERS=%~3"
if not defined SERVER_MIN_PLAYERS set "SERVER_MIN_PLAYERS=2"
set "SERVER_BIND=%~4"
set "SERVER_BIND_ARG="
if defined SERVER_BIND set "SERVER_BIND_ARG=-MULTIHOME=%SERVER_BIND%"

echo Starting BrokenEdge dedicated server on UDP port %SERVER_PORT%...
"%~dp0BrokenEdgeServer.exe" /Game/Maps/Lobby -log -port=%SERVER_PORT% -MatchType=%SERVER_MATCH% -MinPlayersToStart=%SERVER_MIN_PLAYERS% %SERVER_BIND_ARG% -unattended

set "SERVER_EXIT_CODE=%ERRORLEVEL%"
echo BrokenEdge dedicated server exited with code %SERVER_EXIT_CODE%.
pause
exit /b %SERVER_EXIT_CODE%
