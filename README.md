# BrokenEdge

BrokenEdge 是一个使用 **Unreal Engine 5.5.4** 和 C++ 开发的多人第三人称项目，主要用于学习和实践角色移动、武器交互、属性增益、HUD、网络复制以及在线会话等 UE5 系统。

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
| 引擎 | Unreal Engine 5.5.4 |
| 语言 | C++ / Blueprint |
| 平台 | Windows 64 位 |
| 输入 | Enhanced Input |
| 联机 | Online Subsystem Null / MultiplayerSessions |
| UI | UMG / Slate |
| 特效 | Niagara |
| 大文件 | Git LFS |

当前 `.uproject` 关联的是本机源码版 UE 5.5.4。其他电脑首次打开时，如果引擎关联失效，请重新选择本机安装的 UE 5.5.4。

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

1. 安装 Unreal Engine 5.5.4。
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

- `Content/Maps/MainMap.umap`：编辑器、游戏和服务器的默认地图
- `Content/Maps/Lobby.umap`：联机会话大厅地图
- `BrokenEdge.Target.cs`：游戏客户端目标
- `BrokenEdgeEditor.Target.cs`：编辑器目标
- `BrokenEdgeServer.Target.cs`：独立服务器目标

`DefaultGame.ini` 中的独立服务器会话配置当前为最多 8 个公开连接，并启用自动启动会话。联机功能应分别在 Standalone、Listen Server、Client 和 Dedicated Server 场景中验证。

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

- [Gitea 局域网 Git 服务器指南](Docs/Gitea_LAN_Git_Server_Guide.md)
- [Git 使用指南](Docs/Git_Usage_Guide.md)
- [PlayerController、生命值、护盾与 UI](Docs/UE5_PlayerController_Health_Shield_UI.md)
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
