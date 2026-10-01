# BrokenEdge

BrokenEdge 是一个使用 **Unreal Engine 5.8.3** 和 C++ 开发的多人第三人称项目，主要用于学习和实践角色移动、武器交互、属性增益、HUD、网络复制以及在线会话等 UE5 系统。

> 项目仍在开发中。部分玩法、联机回调和表现逻辑可能尚未完成，当前实现请以源码、蓝图和实际运行结果为准。

## 主要内容

- 基于 Enhanced Input 的移动、视角、跳跃、蹲伏、装备和瞄准输入
- 角色移动动画、瞄准偏移和原地转向数据
- 主武器/副武器的拾取、装备、背包挂接、丢弃和切换流程
- 动态准星、武器弹药 HUD 接口和命中扫描武器基础结构
- 生命值、护盾及其网络复制与 HUD 更新
- 生命、护盾、移动速度和跳跃能力拾取增益
- `Server`、`Multicast`、`ReplicatedUsing` 等多人同步机制
- 基于 Online Subsystem 的创建、搜索和加入会话接口
- 游戏客户端、编辑器和独立服务器构建目标

## 技术环境

| 项目 | 当前配置 |
| --- | --- |
| 引擎 | Unreal Engine 5.8.3 |
| 语言 | C++ / Blueprint |
| 平台 | Windows 64 位 |
| 输入 | Enhanced Input |
| 联机 | Online Subsystem Null / MultiplayerSessions |
| UI | UMG / Slate |
| 特效 | Niagara |
| 大文件 | Git LFS |

当前 `.uproject` 关联的是本机源码版 UE 5.8.3。其他电脑首次打开时，如果引擎关联失效，请重新选择本机安装的 UE 5.8.3。

## 获取项目

仓库位于团队局域网 Gitea。克隆前必须安装 [Git LFS](https://git-lfs.com/)。

```powershell
git lfs install
git clone http://192.168.1.111:3000/2604_0608_UECPP/BrokenEdge.git
Set-Location BrokenEdge
git lfs pull
```

如果 Gitea 服务器地址发生变化，请使用仓库网页中显示的最新克隆地址。`127.0.0.1` 只表示当前电脑，其他电脑通常应使用服务器的局域网 IP。

确认 Unreal 资源已经由 LFS 正确下载：

```powershell
git lfs status
git lfs ls-files | Select-Object -First 20
```

如果 `.uasset` 或 `.umap` 文件内容只有几行 LFS 指针文本，不要启动编辑器，应先排查凭据和 LFS 对象是否下载完整。

## 打开与编译

1. 安装 Unreal Engine 5.8.3。
2. 安装 Visual Studio 2022（包含“使用 C++ 的游戏开发”）或支持 Unreal 的 JetBrains Rider。
3. 右键 `BrokenEdge.uproject`，选择“生成 Visual Studio 项目文件”。
4. 使用 IDE 编译 `BrokenEdgeEditor` 的 `Development Editor | Win64` 配置，或直接打开 `BrokenEdge.uproject` 让 Unreal Editor 编译。
5. 打开默认地图 `/Game/Maps/MainMap` 进行测试。

使用源码版引擎时，也可以执行：

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" `
  BrokenEdgeEditor Win64 Development `
  -Project="<PROJECT_ROOT>\BrokenEdge.uproject" `
  -WaitMutex -NoHotReloadFromIDE
```

将 `<UE_ROOT>` 和 `<PROJECT_ROOT>` 替换为本机的绝对路径。

## 地图与运行目标

- `Content/Maps/MainMap.umap`：编辑器启动地图。
- `Content/Maps/Lobby.umap`：客户端和专用服务器的默认入口；客户端显示加入菜单，专服等待玩家。
- `Content/Maps/FreeForAll.umap`、`Content/Maps/Teams.umap`：自由模式和团队模式比赛地图。
- `BrokenEdge.Target.cs`：Windows 游戏程序，用于运行客户端。
- `BrokenEdgeEditor.Target.cs`：编辑器目标。
- `BrokenEdgeServer.Target.cs`：无本地玩家、无渲染的专用服务器目标。

### 专用服务器会话

当前使用 `OnlineSubsystemNull`。专服在大厅 `BeginPlay` 后自动创建并广播专用会话，失败时每 5 秒重试。客户端的 Host 按钮隐藏，只搜索同项目、同模式、有空位的专服；加入后解析服务器地址并执行 `ClientTravel`。

专服从 `Config/DefaultGame.ini` 读取：

```ini
[MultiplayerSessions.DedicatedServer]
NumPublicConnections=8
MinPlayersToStart=2
MatchType=FreeForAll
bAutoStartSession=True
```

`NumPublicConnections` 是容量；`MinPlayersToStart` 是开局所需的真实客户端数量，专服本身不算玩家。满足人数后启动在线会话，并无缝跳转到所选比赛地图。将 `bAutoStartSession` 设为 False 会停留在大厅。当前支持 `FreeForAll` 和 `Teams`。

启动参数 `-NumPublicConnections=`、`-MinPlayersToStart=`、`-MatchType=` 优先于配置。客户端菜单模式可以由 `BP_LobbyGameMode.MenuMatchType` 或启动参数 `-MatchType=` 指定；旧的 `Menu Num Public Connections` 不再决定服务器容量。

### 打包与启动

保存并关闭 UE 编辑器后，依次运行仓库根目录的 `PackageServer.bat` 和 `PackageClient.bat`。两个包都包含 Lobby、FreeForAll、Teams，避免大厅跳图后缺少已烘焙地图。

也可以在未被编辑器打开的完整工程副本中顺序打包，以保留当前编辑器。脚本只检查目标工程是否打开；环境变量 `BROKENEDGE_PACKAGE_ROOT` 可指定产物根目录，两个脚本分别输出到其 `Server` 和 `Client` 子目录。副本仅包含磁盘上已保存的内容。

- 服务器：`Packaged/Server/WindowsServer/StartServer.bat [端口] [模式] [开局人数] [绑定IPv4]`
- 客户端：`Packaged/Client/Windows/StartClient.bat [服务器地址] [模式]`

直接双击 StartServer 默认启动 7777 端口、FreeForAll、2 人开局；双击 StartClient 进入菜单，点击“加入服务器”。分别启动两个客户端即可进行默认人数的测试。客户端传入服务器 IP:端口时走直接连接；要验证会话搜索，应使用菜单加入。

示例（在各自包目录运行）：

```bat
StartServer.bat 7777 Teams 2
StartClient.bat "" Teams
```

多网卡或 VPN/TUN 环境中，应在服务器脚本的第四个参数传入实际局域网 IPv4，或直接使用 `-MULTIHOME=<IPv4>`。检查客户端日志中的 `Menu ClientTravel` 地址。Null 的自动发现适用于局域网；公网运行需要可达的服务器地址和端口，当前未接入公网服务器目录或 Steam 匹配。

### 自动验收

编译 Editor 目标后，可用编辑器可执行程序启动一个专服和两个独立客户端：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Test-LanSessions.ps1 -EngineRoot "<UE_ROOT>"
```

服务器和客户端打包完成后，直接测试打包产物：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Test-LanSessions.ps1 -Packaged
```

可追加 `-BindAddress <本机IPv4>` 指定测试网卡。脚本使用无渲染模式，通过实际 UMG 按钮委托检查客户端禁止建房、空搜索重试、模式过滤、专服自动注册、两名客户端搜索加入，以及两个模式的无缝跳图。日志保存到 `Saved/SessionTests/Dedicated-<时间戳>/`。本机进程验收不等于跨机器网络或画面验证。

### 已验证的打包结果（2026-09-08）

本次使用 `Saved/PackagingWorkspace/20260908-093209` 中的已保存工程副本构建，产物归档到原工程的 `Packaged` 目录，保留原工程编辑器运行。服务器和 Windows 客户端的 Build/Cook/Stage/Pak/IoStore/Archive 均成功，UAT 退出码均为 0；两个包的 EXE、`.pak`、`.ucas`、`.utoc` 齐全，启动脚本 SHA-256 与 `Deployment` 中的版本一致。

实际执行 `Test-LanSessions.ps1 -Packaged -BindAddress 192.168.1.111`，9 项独立进程测试全部通过：空搜索重试 1 项；FreeForAll、Teams 各包含模式过滤、两个客户端加入和服务器状态检查 4 项。两个客户端均经菜单 Find/Join 连接到专服，达到 2 人后分别无缝进入对应比赛地图；切图后仍为专服，在线会话和容量配置有效。

- [服务器打包日志](Saved/PackagingLogs/DedicatedServer-20260908.log)
- [客户端成功打包日志](Saved/PackagingLogs/WindowsClient-20260908-resume.log)（首次编译在链接前中断，续跑复用了编译结果）
- [打包产物联机测试日志](Saved/SessionTests/Dedicated-20260908-122341/)

日志为本机产物，不纳入 Git。烘焙仍报告两项已有警告：`BrokenEdgePlayerController.MatchState` 未注册复制，以及动画蓝图 `hand_r` 修改骨骼节点没有启用变换。这次验收覆盖会话连接与地图切换，不代表已验证完整战斗流程、画面或跨机器网络。

## 目录结构

```text
BrokenEdge/
├─ Config/                         项目、输入和引擎配置
├─ Content/                        蓝图、地图、动画、UI、武器与特效资源
├─ Docs/                           项目专题文档与排查记录
├─ Plugins/MultiplayerSessions/    多人会话插件
├─ Source/BrokenEdge/
│  ├─ Character/                   角色与动画实例
│  ├─ Component/                   战斗与增益组件
│  ├─ GameMode/                    游戏模式
│  ├─ Pickups/                     生命、护盾、速度和跳跃拾取物
│  ├─ PlayerController/            玩家控制器与 HUD 桥接
│  ├─ UI/                          HUD 和角色状态界面
│  ├─ Weapon/                      武器及命中扫描武器
│  └─ BrokenEdgeTypes/             战斗状态、武器类型等枚举
└─ BrokenEdge.uproject
```

## 协作流程

`master` 是受保护分支。除仓库负责人外，开发成员应从最新 `master` 创建功能分支，提交 Pull Request 后再由负责人审核合并。

```powershell
git switch master
git pull --ff-only origin master
git switch -c <账号>/<功能名称>

# 完成修改后
git add --all
git commit -m "说明本次修改"
git push -u origin <账号>/<功能名称>
```

分支名示例：

```text
blq2/turning-in-place
blq3/weapon-swap
blq4/session-menu
```

随后在 Gitea 中创建 Pull Request：

```text
来源分支：账号/功能名称
目标分支：master
```

提交前请确认：

```powershell
git status --short
git diff --check
git lfs status
```

不要提交 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 或 IDE 临时文件。切换包含 Unreal 资产的分支前，建议先关闭 Unreal Editor，避免 `.uasset` 被占用。

## 项目文档

- [已完成功能、网络同步分析与后续开发路线](Docs/BrokenEdge_Project_Status_Network_Replication_Roadmap.md)
- [比赛流程与会话匹配代码评审](Docs/BrokenEdge_Match_and_Matchmaking_Gap_Analysis.md)
- [网络功能流程动画页面](Docs/BrokenEdge_Network_Flow_Visualizer.html)
- [Gitea 局域网 Git 服务器指南](Docs/Gitea_LAN_Git_Server_Guide.md)
- [Git 使用指南](Docs/Git_Usage_Guide.md)
- [PlayerController、生命值、护盾与 UI](Docs/UE5_PlayerController_Health_Shield_UI.md)
- [武器手部 IK 问题诊断与修复指南](Docs/UE5_Weapon_Hand_IK_Diagnosis_and_Fix.md)
- [多人武器丢弃功能实现指南](Docs/UE5_Weapon_Drop_Network_Implementation.md)
- [UE5 委托说明](Docs/UE5_Delegate.md)
- [函数归属与访问边界审计](Docs/BrokenEdge_Function_Placement_and_Access_Audit.md)
- [项目资产来源记录](Docs/asset_source.md)

## 开发注意事项

- C++ 默认值不一定等于蓝图实例的最终值；排查问题时同时检查蓝图组件、GameMode、地图和 PIE 网络模式。
- 网络状态应明确区分服务器权威逻辑、本地玩家 UI 和模拟代理表现。
- `ReplicatedUsing` 变量仍需在 `GetLifetimeReplicatedProps()` 中注册。
- 修改武器、角色骨骼或蒙太奇通知后，应同时验证服务器与客户端的挂接和动画结果。
- Unreal 二进制资产无法像文本代码一样可靠合并；同一资产尽量由一名成员在一个分支中修改。
- 项目中的专项审计文档记录的是检查时的状态，修复后应结合最新源码重新验证。

## 资源与许可

仓库包含项目代码以及若干 Unreal/Fab 第三方资源。第三方资源仍受其各自许可和账号授权约束，目录名或文件名不能单独证明资源授权。

当前仓库未提供统一的开源许可证，因此未经项目所有者许可，不应公开发布、再分发或用于仓库约定范围之外的用途。资产信息请参考[项目资产来源记录](Docs/asset_source.md)。

## 蜂医角色测试

地图：/Game/Characters/Stinger/Maps/L_StingerTest。角色 BP_StingerCharacter 直接继承 ABrokenEdgeCharacter；第一人称使用修正导入动作和 AK 适配，第三人称暂用原项目重定向动作。操作为 WASD 移动、Shift 冲刺、Space 跳跃、Ctrl 蹲伏、E 装备/切枪、右键瞄准、左键开火、R 换弹、V 切换视角。

[适配说明与验证证据](Docs/Stinger/ViewFit.md)。
