@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "PROJECT_FILE=%PROJECT_ROOT%BrokenEdge.uproject"
set "UE_ROOT=D:\UnrealEngine-5.5.4-release"
set "RUN_UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
set "OUTPUT_DIR=%PROJECT_ROOT%Packaged\Client"

if not exist "%RUN_UAT%" (
	echo [ERROR] RunUAT.bat was not found: "%RUN_UAT%"
	exit /b 1
)

if not exist "%PROJECT_FILE%" (
	echo [ERROR] Project file was not found: "%PROJECT_FILE%"
	exit /b 1
)

tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>NUL | find /I "UnrealEditor.exe" >NUL
if not errorlevel 1 (
	echo [ERROR] Unreal Editor is running. Close it before packaging.
	exit /b 1
)

echo Packaging BrokenEdge Windows client...
call "%RUN_UAT%" BuildCookRun ^
	-project="%PROJECT_FILE%" ^
	-nop4 ^
	-build ^
	-cook ^
	-stage ^
	-pak ^
	-iostore ^
	-archive ^
	-archivedirectory="%OUTPUT_DIR%" ^
	-platform=Win64 ^
	-clientconfig=Development ^
	-map=/Game/Maps/MainMap ^
	-prereqs ^
	-ubtargs="-NoHotReloadFromIDE" ^
	-utf8output

if errorlevel 1 (
	echo [ERROR] Windows client packaging failed.
	exit /b 1
)

copy /Y "%PROJECT_ROOT%Deployment\StartClient.bat" "%OUTPUT_DIR%\Windows\StartClient.bat" >NUL
if errorlevel 1 (
	echo [ERROR] Failed to copy StartClient.bat into the client package.
	exit /b 1
)

echo [SUCCESS] Windows client package: "%OUTPUT_DIR%\Windows"
exit /b 0
