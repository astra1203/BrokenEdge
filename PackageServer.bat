@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "PROJECT_FILE=%PROJECT_ROOT%BrokenEdge.uproject"
set "UE_ROOT=D:\UnrealEngine-5.5.4-release"
set "RUN_UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
set "OUTPUT_DIR=%PROJECT_ROOT%Packaged\Server"

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

echo Packaging BrokenEdge dedicated server...
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
	-server ^
	-noclient ^
	-serverplatform=Win64 ^
	-serverconfig=Development ^
	-map=/Game/Maps/MainMap ^
	-prereqs ^
	-ubtargs="-NoHotReloadFromIDE" ^
	-utf8output

if errorlevel 1 (
	echo [ERROR] Dedicated server packaging failed.
	exit /b 1
)

copy /Y "%PROJECT_ROOT%Deployment\StartServer.bat" "%OUTPUT_DIR%\WindowsServer\StartServer.bat" >NUL
if errorlevel 1 (
	echo [ERROR] Failed to copy StartServer.bat into the server package.
	exit /b 1
)

echo [SUCCESS] Dedicated server package: "%OUTPUT_DIR%\WindowsServer"
exit /b 0
