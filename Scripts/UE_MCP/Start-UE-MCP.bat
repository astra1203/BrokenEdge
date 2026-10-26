@echo off
REM Start BrokenEdge with the built-in Unreal MCP server listening on port 8000.
REM The endpoint is http://127.0.0.1:8000/mcp (see Scripts/UE_MCP/ue_mcp.py).
REM Config also lives in Config/DefaultEditorPerProjectUserSettings.ini:
REM   [/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
REM   bAutoStartServer = True / ServerPortNumber = 8000 / ServerUrlPath = /mcp
REM The command-line switches below override the ini, so the server starts even
REM if a per-user Saved/Config value turned auto-start off.
setlocal
set ENGINE=D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJECT=%~dp0..\..\BrokenEdge.uproject
if not exist "%ENGINE%" (
    echo UnrealEditor.exe not found at %ENGINE%
    exit /b 1
)
start "" "%ENGINE%" "%PROJECT%" -ModelContextProtocolStartServer -ModelContextProtocolPort=8000
echo Editor launched. Wait for the project to load, then verify with:
echo   python Scripts\UE_MCP\ue_mcp.py ping
endlocal
