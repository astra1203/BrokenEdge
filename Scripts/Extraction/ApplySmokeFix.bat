@echo off
chcp 65001 >nul
setlocal

rem 一键：编译 C++ + 重建烟雾材质。
rem 编译前必须确认编辑器已关闭 —— 该模块没有 LiveCodingManifest，热补丁无效，
rem 而且编辑器会锁住 Content/Extraction/Abilities 下的资产（Error Code 32）。

set "UE=D:\Program Files\Epic Games\UE_5.8"
set "PROJ=D:\Users\SCSJ\BrokenEdge2.0\BrokenEdge.uproject"
set "ROOT=D:\Users\SCSJ\BrokenEdge2.0"

echo.
echo === 1/3 检查编辑器进程 ===
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
    echo [中止] 虚幻编辑器还开着。
    echo         它会同时挡住编译和资产写入，请先关掉编辑器再双击本文件。
    echo.
    pause
    exit /b 1
)

echo === 2/3 编译 BrokenEdgeEditor ===
call "%UE%\Engine\Build\BatchFiles\Build.bat" BrokenEdgeEditor Win64 Development -Project="%PROJ%" -WaitMutex -NoUBA
if errorlevel 1 (
    echo.
    echo [失败] 编译不通过，错误见上方输出。
    pause
    exit /b 1
)

echo.
echo === 3/3 重建烟雾材质 ===
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%ROOT%\Scripts\Extraction\build_smoke_material.py" -unattended -nopause -nosplash -NullRHI -abslog="%ROOT%\Saved\Logs\smoke_material_apply.log"
findstr /C:"SMOKE_ASSETS_OK" "%ROOT%\Saved\Logs\smoke_material_apply.log" >nul
if errorlevel 1 (
    echo [警告] 材质脚本没有输出 SMOKE_ASSETS_OK，请看 Saved\Logs\smoke_material_apply.log
) else (
    echo [成功] 烟雾材质已按新图重建。
)

echo.
echo 全部完成。现在打开项目进 PIE 测试：
echo   - 进游戏后先站 30 秒以上，再扔烟（旧 bug 只在服务器跑过 18 秒后触发）
echo   - 落地约 1.5 秒后起烟，应该立刻是一团看得见的灰烟
echo   - Saved\Logs\ 里搜 [Smoke]，每 0.5 秒一行，能看到位置 / alpha / 尺寸
echo.
pause
