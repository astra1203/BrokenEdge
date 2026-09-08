@echo off
setlocal
cd /d "%~dp0"

set "SERVER_ADDRESS=%~1"
set "CLIENT_MATCH=%~2"
if not defined CLIENT_MATCH set "CLIENT_MATCH=FreeForAll"

if defined SERVER_ADDRESS (
	echo Connecting to %SERVER_ADDRESS%...
	start "" "%~dp0BrokenEdge.exe" "%SERVER_ADDRESS%" -windowed -log
) else (
	echo Starting server browser...
	start "" "%~dp0BrokenEdge.exe" /Game/Maps/Lobby -MatchType=%CLIENT_MATCH% -windowed -log
)
exit /b 0
