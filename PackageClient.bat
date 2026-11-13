@echo off
setlocal

REM ==========================================================
REM PackageClient.bat -- BrokenEdge Windows 客户端打包脚本
REM
REM 作用: 调用引擎的 RunUAT.bat 执行 BuildCookRun, 一条命令走完
REM       编译 / 烘焙 / 暂存 / 打包 / 归档, 产出可分发的客户端
REM 输出: 默认输出到脚本所在目录下的 Packaged\Client,
REM       最终产物位于 输出目录\Windows
REM       如果设置了环境变量 BROKENEDGE_PACKAGE_ROOT,
REM       则改为输出到 该变量\Client
REM 前置: 必须先关闭 Unreal Editor, 否则脚本会直接中止

REM ==========================================================
REM ---------- 变量定义 ----------
REM PROJECT_ROOT: 脚本自身所在目录, 由批处理参数展开而来, 结尾带反斜杠
set "PROJECT_ROOT=%~dp0"

REM PROJECT_FILE: 要打包的项目文件完整路径
set "PROJECT_FILE=%PROJECT_ROOT%BrokenEdge.uproject"

REM UE_ROOT: selected by BROKENEDGE_UE_ROOT
if not defined BROKENEDGE_UE_ROOT (
    echo [ERROR] Set BROKENEDGE_UE_ROOT to the Unreal Engine installation.
    exit /b 1
)
set "UE_ROOT=%BROKENEDGE_UE_ROOT%"

REM RUN_UAT: 自动化工具入口 RunUAT.bat 的路径
set "RUN_UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"

REM OUTPUT_DIR: 打包归档输出目录, 默认放在项目内的 Packaged\Client
set "OUTPUT_DIR=%PROJECT_ROOT%Packaged\Client"

REM 允许外部用环境变量覆盖输出根目录, 方便 CI 或输出到别的磁盘
if defined BROKENEDGE_PACKAGE_ROOT set "OUTPUT_DIR=%BROKENEDGE_PACKAGE_ROOT%\Client"

REM ---------- 前置检查 ----------
REM 检查引擎里的 RunUAT.bat 是否存在, 引擎路径写错就直接退出
if not exist "%RUN_UAT%" (
	echo [ERROR] RunUAT.bat was not found: "%RUN_UAT%"
	exit /b 1
)

REM 检查 .uproject 项目文件是否真的存在
if not exist "%PROJECT_FILE%" (
	echo [ERROR] Project file was not found: "%PROJECT_FILE%"
	exit /b 1
)

REM 检查 Unreal Editor 是否正打开着本项目
REM 编辑器开着时烘焙会争抢 DerivedDataCache 和中间文件, 容易失败或产出脏数据
REM 所以交给 Assert-EditorClosed.ps1 检测, 它返回非 0 就视为未关闭或状态无法确认
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%Scripts\Assert-EditorClosed.ps1" -ProjectFile "%PROJECT_FILE%"
REM 上一条命令的退出码非 0 时进入失败分支, 不继续打包
if errorlevel 1 (
	echo [ERROR] The target project is open in Unreal Editor or its state could not be checked.
	exit /b 1
)

REM ---------- 执行打包 ----------
echo Packaging BrokenEdge Windows client...
REM 下面调用 RunUAT.bat 的 BuildCookRun 命令, 一次完成全部打包步骤
REM 各参数含义:
REM   -project           指定要打包的 .uproject 文件
REM   -nop4              不连接 Perforce 源码管理, 没有 P4 环境也不会报错
REM   -build             先编译目标平台的程序二进制
REM   -cook              把资源烘焙成目标平台可直接使用的格式
REM   -stage             把需要打包的文件收集到暂存目录
REM   -pak               把烘焙后的资源打进 .pak 包
REM   -iostore           额外生成 IoStore 容器, 提升资源加载性能
REM   -archive           把暂存结果复制到归档目录
REM   -archivedirectory  归档输出目录, 也就是上面的 OUTPUT_DIR
REM   -platform          目标平台, 这里打包 Windows 64 位
REM   -clientconfig      客户端编译配置, 这里是 Development, 正式发版可改成 Shipping
REM   -map               需要烘焙的地图列表, 多个地图之间用加号分隔
REM   -prereqs           一并处理运行库等前置依赖
REM   -ubtargs           传给 UnrealBuildTool 的额外参数, 这里禁用 IDE 热重载
REM   -utf8output        让编译输出使用 UTF-8, 避免中文日志乱码
REM 注意: 下面这条命令用行尾的 ^ 续行, 续行中间不能插入空行或注释
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
	-map=/Game/Maps/Lobby+/Game/Maps/FreeForAll+/Game/Maps/Teams+/Game/Maps/SpaceCenter/L_SpaceCenterExtraction ^
	-prereqs ^
	-ubtargs="-NoHotReloadFromIDE" ^
	-utf8output

REM ---------- 结果检查 ----------
REM errorlevel 非 0 表示上面那条打包命令失败了, 不再继续后面的步骤
if errorlevel 1 (
	echo [ERROR] Windows client packaging failed.
	exit /b 1
)

REM 把启动脚本复制进包内, 方便拿到包之后直接双击运行客户端
REM /Y 表示覆盖同名文件, 结尾的 >NUL 用来屏蔽复制成功的提示
copy /Y "%PROJECT_ROOT%Deployment\StartClient.bat" "%OUTPUT_DIR%\Windows\StartClient.bat" >NUL
if errorlevel 1 (
	echo [ERROR] Failed to copy StartClient.bat into the client package.
	exit /b 1
)

echo [SUCCESS] Windows client package: "%OUTPUT_DIR%\Windows"
REM 明确返回 0 表示成功, 方便命令行或 CI 用退出码判断结果
exit /b 0
