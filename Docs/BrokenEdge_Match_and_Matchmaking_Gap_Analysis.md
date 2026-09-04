# BrokenEdge 比赛流程与会话匹配代码评审

> 当前项目：`C:\UnrealProjects\BrokenEdge`
>
> 参考项目：`D:\SourceFiles\UnrealProjects\UE5BlasterLearning-main`
>
> 当前项目基线：`master`，提交 `9f5d387`（`完善团队模式计分界面`）
>
> 参考项目基线：`master`，提交 `af80861`
>
> 审计日期：2026-09-04
>
> 引擎版本：Unreal Engine 5.5.4
>
> 评审方式：当前项目与参考项目逐文件静态源码对照，并辅以现有日志和资源引用检查

## 1. 文档目的

本文以已完成的 UE5BlasterLearning 项目为功能参照，对 BrokenEdge 的比赛流程和会话匹配代码进行静态评审。评审不仅列出缺失功能，还检查现有代码中的正确性、空指针、异步委托、复制、状态恢复和配置一致性问题。

这里把容易混淆的两个概念分开：

- **比赛流程（Match Flow）**：进入地图后的热身、比赛开始、倒计时、冷却、计分、胜负和重开；
- **会话匹配（Session Matchmaking）**：菜单创建会话、查找房间、筛选 `MatchType`、加入会话、地图旅行和退出清理。

[`BrokenEdgeGameState.cpp`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.cpp) 属于比赛流程层。当前主要阻塞点并不只在 GameState，而是分布在 GameMode、PlayerController、PlayerState、HUD、Lobby 和 MultiplayerSessions 插件之间。

本文用于：

- 判断当前哪些代码已经形成静态调用链；
- 标出确定缺口与正确性问题；
- 给出适合继续实现的文件顺序；
- 为 PIE、Standalone、Listen Server 和 Dedicated Server 验收提供检查表。

## 2. 审计范围与证据边界

### 2.1 本次实际检查内容

当前项目：

- `Source/BrokenEdge/GameState`；
- `Source/BrokenEdge/GameMode`；
- `Source/BrokenEdge/PlayerState`；
- `Source/BrokenEdge/PlayerController`；
- `Source/BrokenEdge/UI`；
- `Plugins/MultiplayerSessions/Source`；
- `Config/DefaultEngine.ini`、`Config/DefaultGame.ini`；
- `MainMap.umap`、`Lobby.umap` 和相关 GameMode/UI 资产中的可读取引用；
- `Saved/Logs/BrokenEdge.log` 中已有的分队和复制警告记录；
- 当前 Git 分支、提交和工作树状态。

参考项目：

- `BlasterGameState`；
- `BlasterGameMode`、`TeamsGameMode`、`LobbyGameMode`、`CaptureTheFlagGameMode`；
- `BlasterPlayerController`、`BlasterPlayerState`；
- HUD、Announcement、ReturnToMainMenu；
- MultiplayerSessions 的创建、查找、加入、销毁和菜单回调；
- 地图入口与 OnlineSubsystem 配置。

### 2.2 状态定义

| 状态 | 含义 |
| --- | --- |
| 已接通 | 当前源码中的主要静态调用链完整 |
| 部分完成 | 主体存在，但至少一个状态同步、表现、失败路径或配置环节未闭合 |
| 已搭架 | 只有字段、声明、空函数、TODO 或被注释的调用 |
| 未实现 | 当前工程没有对应的核心业务入口 |

“已接通”仍不自动等于多人运行通过。除非明确列出日志或本轮运行证据，否则结论只代表源码和资源静态检查。

### 2.3 本轮没有完成的验证

- 没有重新编译 `BrokenEdgeEditor`；
- 没有启动新的 PIE、Standalone 或 Dedicated Server；
- 没有在编辑器中逐项打开 Blueprint Graph、Class Defaults 和 World Settings；
- 对 `.uasset`、`.umap` 的判断仅使用可读取资源引用，不能替代 Blueprint 运行证明；
- 没有把同进程 PIE 的自动连接当作 Find/Join 会话测试。

### 2.4 参考项目的使用原则

参考项目提供了完整功能链和清晰的类职责，但它不是可以逐行复制的“零缺陷规范”。本轮同时发现参考项目仍保留以下边界风险：

- `BlasterGameMode.cpp:183-186` 同样没有在随机索引前检查 `PlayerStarts` 是否为空；
- `TeamsGameMode.cpp:84-85` 同样会在检查前解引用 `Attacker` 和 `Victim`；
- `TeamsGameMode.cpp:136-151` 同样没有明确排除自杀后增加团队分；
- `LobbyGameMode.cpp:28` 使用人数严格相等 `==`，更稳妥的路由通常还需防重复旅行并明确处理 `>=`；
- `MultiplayerSessionsSubsystem.cpp:325-329` 的 `StartSession()` 仍是空实现；
- 参考项目的会话创建依赖本地玩家 NetId，适用于菜单驱动的客户端或 Listen Server，并不是 Dedicated Server 自动注册实现。

因此，本文把参考项目用于确认目标链路，同时对当前项目和参考实现共同存在的风险单独给出防护建议。

## 3. 代码评审结论

### 3.1 总体判断

当前项目已经完成了团队模式的部分主体：

- `PostLogin()` 可以把玩家分配到红队或蓝队；
- `Logout()` 会从队伍数组移除玩家；
- 同队伤害过滤逻辑已经存在；
- 伤害、淘汰、定时重生的主体已经存在；
- 红蓝队分数具备复制属性和 RepNotify；
- 队伍击杀可以进入 `AddRedTeamScores()` 或 `AddBlueTeamScores()`；
- 当前日志中已有红蓝队分配实际执行的记录。

但当前还不能形成“菜单建房或搜房 → Lobby → 正式比赛 → 完整回合 → 返回或重开”的端到端闭环。主要阻断链如下：

```text
进入正式地图
  -> GameMode 驱动 WaitingToStart / InProgress / Cooldown
  -> PlayerController 获得并复制 MatchState
  -> HUD 显示热身、比赛倒计时和最终胜负
  -> 冷却结束后 RestartGame

当前状态：上述链条只有字段和 RPC 外壳，尚未闭合。
```

会话匹配链同样没有闭合：

```text
Host
  -> CreateSession
  -> ServerTravel("/Game/Maps/Lobby?listen")
  -> [缺少 LobbyGameMode 按人数和 MatchType 路由]
  -> 正式比赛地图

Join
  -> FindSessions
  -> [缺少 Menu 的搜索结果回调与 MatchType 筛选]
  -> [缺少有效的 Join 完成回调]
  -> [缺少 GetResolvedConnectString + ClientTravel]
```

因此当前准确状态是：

> **团队分配和队伍分数主体已接通；比赛生命周期仍是框架；会话 Host 部分完成，Join 和 Lobby 路由尚未形成可用闭环。**

### 3.2 严重级别

| 级别 | 判定标准 |
| --- | --- |
| 阻断 | 主流程没有实现，按当前代码无法完成目标闭环 |
| 高 | 可造成错误计分、崩溃、复制失效、会话卡死或核心状态错误 |
| 中 | 特定时序、Late Join、失败路径或模式切换下出现错误 |
| 低 | 当前通常不阻断功能，但会增加误判、维护成本或后续故障概率 |

### 3.3 评审发现总表

本轮共记录 18 项：阻断 3 项、高风险 9 项、中风险 5 项、低风险 1 项。

| ID | 级别 | 发现 | 当前代码证据 | 主要影响 |
| --- | --- | --- | --- | --- |
| BE-MATCH-001 | 阻断 | GameMode 没有热身、比赛、冷却和重开状态机 | [`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp):16-34；头文件没有 `Tick()`/`OnMatchStateSet()` | 配置的三段时间没有执行者，完整回合无法自动推进 |
| BE-SESSION-001 | 阻断 | Find → Join → ClientTravel 主链未闭合 | [`Menu.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/Menu.cpp):44-50、126-132；[`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp):41-45、284-305 | 搜索即使成功，界面也不会选择房间并进入主机 |
| BE-LOBBY-001 | 阻断 | Host 进入 Lobby 后没有已确认的自动开局路由 | `Menu.cpp:101-104` 会旅行到 Lobby；工程没有 `LobbyGameMode`，[`Lobby.umap`](../Content/Maps/Lobby.umap) 可读取引用仍指向 `BP_BrokenEdgeGameMode` | 创建成功后可能停留在 Lobby，无法按人数和模式进入正式地图 |
| BE-REPL-001 | 高 | `MatchState` 声明为 RepNotify，却没有登记复制 | [`BrokenEdgePlayerController.h`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.h):201-209；[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp):22-26 | 远端客户端收不到阶段变化；现有日志已出现明确警告 |
| BE-SCORE-001 | 高 | `Defeats` 被加到攻击者，且受害者统计错误地依赖有效攻击者 | `BrokenEdgeGameMode.cpp:52-55` | 击杀者同时增加 Score 和 Defeats；受害者、环境击杀和自杀统计错误 |
| BE-SESSION-002 | 高 | 移动 `MatchType` 后又读取被移动参数 | `MultiplayerSessionsSubsystem.cpp:111-112、175-176` | 广告出的 `MatchType` 可能为空，后续模式筛选失败 |
| BE-SESSION-003 | 高 | 已有同名会话时，销毁后重建流程被中途注释掉 | `MultiplayerSessionsSubsystem.cpp:121-130` | 重复 Host 时既不销毁旧会话，也不创建新会话，按钮可能永久禁用 |
| BE-SESSION-004 | 高 | Join/Destroy 原生完成委托没有绑定到任何回调 | `MultiplayerSessionsSubsystem.cpp:41-45、293-325`；头文件也没有对应完成函数 | 异步请求即使完成也不能清理 Handle、广播结果或继续旅行/重建 |
| BE-CRASH-001 | 高 | 团队伤害计算在判空前解引用控制器 | [`TeamsGameMode.cpp`](../Source/BrokenEdge/GameMode/TeamsGameMode.cpp):96-100；调用方允许 `InstigatorController` 为空 | 环境伤害、无 Instigator 伤害可能触发空指针崩溃 |
| BE-CRASH-002 | 高 | 重生在出生点数组为空时仍生成随机下标并索引 | `BrokenEdgeGameMode.cpp:76-81` | 地图配置错误或无有效 PlayerStart 时数组越界 |
| BE-TEAM-001 | 高 | 自杀仍会给攻击者所属队伍增加一分 | `TeamsGameMode.cpp:71-93` | 基类不奖励个人分，但子类没有检查攻击者与受害者是否相同 |
| BE-DEDICATED-001 | 高 | 会话创建直接解引用 `ULocalPlayer` | `MultiplayerSessionsSubsystem.cpp:179-180` | 真正 Dedicated Server 没有 LocalPlayer，当前自动注册设计不可用且可能崩溃 |
| BE-UI-001 | 中 | 会话失败路径不能可靠恢复按钮 | `Menu.cpp:85-109、111-137`；`FindSessions()` 在接口无效时静默返回 | Host/Join 操作失败后 UI 可能一直不可点击 |
| BE-LIFECYCLE-001 | 中 | 在线委托、重复请求和 World Context 生命周期处理不完整 | 子系统头文件第 57 行注释了 `Deinitialize()`；实现第 82 行注释了旧委托清理 | PIE World 切换、重复调用或退出时可能残留回调或覆盖 Handle |
| BE-TEAM-002 | 中 | 队伍枚举虽然复制，但颜色、PlayerState 晚到和阵营出生点没有闭环 | [`BrokenEdgePlayerState.h`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.h):41-44；[`BrokenEdgePlayerState.cpp`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.cpp):59-62；`BrokenEdgeCharacter.cpp:821-839` | 各端可能有正确 Team 值，却看不到正确材质或出生位置 |
| BE-MODE-001 | 中 | 菜单默认 FFA，当前主地图却使用 Teams GameMode | [`Menu.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/Menu.h):21-22、58-59；[`DefaultEngine.ini`](../Config/DefaultEngine.ini):4-6；MainMap 资源引用 | 修复 Session 后仍可能出现“房间标签与实际规则不一致” |
| BE-HUD-001 | 中 | 倒计时与队伍分可见性只有控件/字段，没有可靠状态驱动 | `CharacterOverlay.h:51-65`；`BrokenEdgePlayerController.cpp:94-101、161-168、178-200` | Late Join、FFA/Teams 切换和阶段切换时 HUD 状态不确定 |
| BE-MAINT-001 | 低 | 参数上限常量未使用，异步请求提交被记录为“创建成功” | `MultiplayerSessionsSubsystem.cpp:21-23、166-192、252-266` | 可能提交过大的搜索请求，并使日志把“请求已提交”误报成最终成功 |

### 3.4 与参考项目的关键差异

| 链路 | BrokenEdge 当前状态 | 参考项目对应实现 |
| --- | --- | --- |
| 比赛生命周期 | 只有时间字段 | `BlasterGameMode.cpp:24-90` 驱动状态机并通知所有控制器 |
| Late Join | RPC 外壳为空且未调用 | `BlasterPlayerController.cpp:76-94、321-358` 请求并恢复当前状态 |
| MatchState 表现 | RepNotify 为空 | `BlasterPlayerController.cpp:731-820` 处理开赛、冷却和胜负 |
| 个人排名 | 未实现 | `BlasterGameState.cpp:30-47` 维护最高分和并列第一 |
| 队伍颜色/出生 | 未实现 | `BlasterPlayerState.cpp:100-121` 与 `BlasterCharacter.cpp:357-390` 应用队伍状态 |
| Session Join | 无 Menu 回调和旅行 | `Menu.cpp:120-233` 筛选模式、解析地址并 ClientTravel |
| Session 生命周期 | Join/Destroy 回调和 Deinitialize 缺失 | 子系统构造函数第 17-24 行、`Deinitialize()` 第 45-83 行、完成回调第 415-448 行 |
| Lobby 路由 | 无专用 GameMode | `LobbyGameMode.cpp:16-50` 根据人数和 MatchType 选择正式地图 |

### 3.5 实现前需要固定的规则

以下选择会改变具体修复方式，但不影响本轮对现状的判断：

- 首个可玩版本是否只支持 Teams，还是同时保留 FFA/CTF；
- 自伤、自杀、友军击杀分别应该不计分、扣分还是奖励对方；
- Lobby 是人数满额自动开局、达到最小人数开局，还是由房主手动开始；
- 首个 Session 闭环以 Null + Listen Server 为目标，还是必须同时支持 Dedicated Server；
- Cooldown 后直接 `RestartGame()`，还是先返回 Lobby/菜单并销毁会话。

## 4. 当前功能状态总表

| 功能 | 当前状态 | 主要依据或缺口 |
| --- | --- | --- |
| 红蓝队分配 | 已接通 | `TeamsGameMode::PostLogin()` 已实现，已有本地日志记录 |
| 玩家退出队伍 | 已接通 | `TeamsGameMode::Logout()` 会清理红蓝队数组 |
| 友军伤害过滤 | 部分完成 | 有效玩家控制器下同队返回 `0.f`，但空 Instigator 会在判空前被解引用 |
| 队伍分数复制 | 已接通（静态） | 红蓝分数均为 RepNotify，并已登记 `DOREPLIFETIME` |
| 队伍计分规则 | 部分完成 | 正常敌方击杀可加分，但自杀也会给攻击者所属队伍加分 |
| 红蓝 HUD 映射 | 已修正 | 当前 `OnRep_RedTeamScore()` 和 `OnRep_BlueTeamScore()` 映射正确 |
| 伤害与重生主体 | 部分完成 | 调用链存在，但淘汰统计对象错误，且无 Instigator/无 PlayerStart 存在崩溃风险 |
| 个人最高分/并列第一 | 未实现 | 缺少 `TopScoringPlayers`、`TopScore` 和 `UpdateTopScore()` |
| 比赛状态机 | 已搭架 | 没有 GameMode Tick、Cooldown、StartMatch、RestartGame |
| MatchState 复制 | 已搭架 | 属性未登记复制，已有运行警告；客户端回调为空 |
| 热身/比赛倒计时 | 已搭架 | 时间同步存在，但没有 `SetHUDTime()` 驱动倒计时文本 |
| Cooldown 与胜负公告 | 未实现 | 缺少 Announcement、赢家文本和冷却期玩法禁用 |
| 队伍颜色 | 已搭架 | `Team` 会复制，但 `OnRep_Team()` 为空 |
| 阵营出生点 | 未实现 | 没有 `TeamPlayerStart` 和按队伍选择出生点的逻辑 |
| 创建会话 | 部分完成 | 第一次创建主体存在，但 `MatchType` 和失败/重建路径有问题 |
| 查找会话 | 部分完成 | 子系统可搜索，Menu 没有处理搜索结果 |
| 加入会话 | 未闭合 | Join 委托、完成回调、解析地址、ClientTravel 均不完整 |
| Lobby 自动开局 | 未实现（C++） | 没有 `LobbyGameMode` 与人数/模式路由 |
| 返回主菜单/退出会话 | 未实现 | 没有 ReturnToMainMenu、离开 RPC 与完整 DestroySession 流程 |
| Dedicated Server 自动注册 | 未闭合 | 配置存在，但没有确认的启动调用链，创建路径仍依赖 LocalPlayer |
| Steam 公网会话 | 未配置 | 当前明确使用 OnlineSubsystemNull，适合 LAN 验证 |
| Capture The Flag | 零散框架 | 有 Flag 类型/持旗字段，但缺少完整 GameMode、FlagZone、地图和计分闭环 |

## 5. GameState：队伍分数已接通，个人排名仍缺失

### 5.1 当前已经完成的部分

[`BrokenEdgeGameState.cpp`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.cpp) 已完成：

- `RedTeamScore` 和 `BlueTeamScore` 的 `DOREPLIFETIME`；
- 服务端 `AddRedTeamScores()`、`AddBlueTeamScores()`；
- Listen Server 本地 HUD 的即时更新；
- 客户端 `OnRep_RedTeamScore()`、`OnRep_BlueTeamScore()`；
- 红分写红 HUD、蓝分写蓝 HUD。

最近提交已经修正旧版本中 `OnRep_RedTeamScore()` 错用 `BlueTeamScore` 的问题。因此旧路线图中的“红蓝写反”不再是当前缺口。

### 5.2 不需要误补的内容

`RedTeam` 和 `BlueTeam` 数组当前没有复制给客户端。参考项目同样主要把这两个数组用于服务端队伍管理；客户端真正需要的是：

- 自己 PlayerState 中复制的 `Team`；
- GameState 中复制的红蓝队分数；
- HUD 根据比赛模式显示相应信息。

因此不应仅因为数组未标记 `Replicated` 就直接添加网络复制。先确认是否真的存在客户端读取需求。

### 5.3 相比参考项目仍缺少的排名能力

参考项目的 `BlasterGameState` 还维护：

```cpp
UPROPERTY(Replicated)
TArray<ABlasterPlayerState*> TopScoringPlayers;

float TopScore = 0.f;

void UpdateTopScore(ABlasterPlayerState* ScoringPlayer);
```

该逻辑负责：

- 第一个得分玩家成为领先者；
- 新的更高分替换领先列表；
- 同分玩家进入并列第一；
- Cooldown 时生成个人赛赢家文本；
- 驱动获得/失去领先状态的表现。

如果 BrokenEdge 目前只做团队死斗，这一项可以排在比赛状态机之后；如果保留菜单默认的 `FreeForAll`，它就是必需功能。

## 6. 比赛流程层的确定缺口

### 6.1 GameMode 没有驱动比赛状态

[`BrokenEdgeGameMode.h`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.h) 虽然已有：

- `WarmupTime`；
- `MatchTime`；
- `CooldownTime`；
- `LevelStartingTime`；
- `bTeamsMatch`。

但 [`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp) 仍缺少参考项目中的核心执行逻辑：

1. 构造函数设置 `bDelayedStart = true`；
2. `BeginPlay()` 保存 `LevelStartingTime`；
3. `Tick()` 计算当前阶段剩余时间；
4. 热身结束调用 `StartMatch()`；
5. 比赛结束进入自定义 `MatchState::Cooldown`；
6. 冷却结束调用 `RestartGame()`；
7. `OnMatchStateSet()` 遍历 PlayerController 并通知每位玩家。

未设置 `bDelayedStart` 不代表基础 `AGameMode` 永远不会进入 `InProgress`；基类可能按自身条件直接开始比赛。真正的问题是 BrokenEdge 声明的 `WarmupTime`、`MatchTime` 和 `CooldownTime` 没有参与服务器状态推进，因而没有可控热身、冷却和自动重开。即使 PlayerController 的复制代码补齐，服务器仍没有完整的自定义状态变化源。

### 6.2 PlayerController 的 MatchState 链没有闭合

[`BrokenEdgePlayerController.h`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.h) 已声明：

- `ServerCheckMatchState()`；
- `ClientJoinMidgame()`；
- `MatchState` RepNotify；
- 热身、比赛、冷却和关卡开始时间；
- `bShowTeamScores`。

但 [`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp) 当前存在以下断点：

1. `BeginPlay()` 没有调用 `ServerCheckMatchState()`；
2. `GetLifetimeReplicatedProps()` 只登记了 `bShowTeamScores`，没有登记 `MatchState`；
3. `ServerCheckMatchState()` 取得数据后没有调用 `ClientJoinMidgame(...)`；
4. `ClientJoinMidgame_Implementation()` 为空；
5. `OnRep_MatchState()` 为空；
6. 没有 `OnMatchStateSet()`；
7. 没有 `HandleMatchHasStarted()`；
8. 没有 `HandleCooldown()`；
9. `Tick()` 没有调用比赛倒计时更新函数。

已有运行日志明确警告：

```text
Property BrokenEdgePlayerController::MatchState ...
was not registered in GetLifetimeReplicatedProps.
This property will not be replicated.
```

因此 `MatchState` 未复制是当前运行时已出现的确定问题，不只是代码风格建议。

### 6.3 HUD 只有倒计时控件，没有倒计时业务

[`CharacterOverlay.h`](../Source/BrokenEdge/UI/CharacterOverlay.h) 已声明 `MatchCountdownText`，但 PlayerController 没有参考项目中的：

- `SetHUDTime()`；
- `SetHUDMatchCountdown()`；
- `SetHUDAnnouncementCountdown()`。

[`BrokenEdgeHUD.h`](../Source/BrokenEdge/UI/BrokenEdgeHUD.h) 也没有：

- `AnnouncementClass`；
- `Announcement` 实例；
- `AddAnnouncement()`；
- 淘汰公告队列。

结果是当前无法完整显示：

- 热身还有多少秒；
- 比赛还有多少秒；
- 新比赛多久后开始；
- 个人或队伍最终胜者；
- 平局信息。

### 6.4 `bShowTeamScores` 没有真正控制界面

当前 `bShowTeamScores` 只是普通复制属性，没有 RepNotify。`PollInit()` 中非团队模式调用 `HideTeamScores()` 的代码还被注释。

这意味着当前队伍分数的可见性更接近“依靠 Widget 默认状态保持显示”，而不是由比赛模式可靠控制。若以后进入 FFA 地图，队伍分数仍可能错误显示。

建议让比赛开始处理函数统一执行：

- Teams：`InitTeamScores()`；
- FFA：`HideTeamScores()`；
- Late Join：根据服务器传来的 `bIsTeamsMatch` 立即恢复正确状态。

## 7. 淘汰、计分与队伍规则缺口

### 7.1 Defeats 当前加给了攻击者

[`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp) 当前淘汰代码等价于：

```cpp
if (AttackerPlayerState && AttackerPlayerState != DefeatedPlayerState && BrokenEdgeGameState)
{
    AttackerPlayerState->AddToDefeats(1);
    AttackerPlayerState->AddToScore(1);
}
```

按照参考项目的数据含义：

- 攻击者增加 `Score`；
- 受害者增加 `Defeats`。

当前已经取得 `DefeatedPlayerState`，却没有调用它的 `AddToDefeats()`。这会造成死亡/被淘汰统计显示在攻击者身上。

此外，`Defeats` 不应放在“攻击者有效且不是自己”的条件块里，否则自杀或环境伤害造成的死亡仍不会计入受害者。建议把两种统计拆开：

```cpp
if (AttackerPlayerState && AttackerPlayerState != DefeatedPlayerState)
{
    AttackerPlayerState->AddToScore(1.f);
    // 更新最高分
}

if (DefeatedPlayerState)
{
    DefeatedPlayerState->AddToDefeats(1);
}
```

### 7.2 缺少最高分更新和领先者表现

基础 GameMode 在攻击者得分后还应：

1. 保存得分前的领先者列表；
2. 调用 GameState 的 `UpdateTopScore()`；
3. 通知新领先者；
4. 通知失去领先的玩家；
5. 在玩家离开时清理领先者列表。

BrokenEdge 当前角色代码中保留了与领先者有关的注释框架，但没有对应的完整 GameState/GameMode 实现。

### 7.3 淘汰播报和离开处理缺失

参考项目会把攻击者与受害者 PlayerState 广播给所有 PlayerController，再由本地 HUD 生成淘汰提示。当前项目缺少：

- `BroadcastElim()`；
- `ClientElimAnnouncement()`；
- ElimAnnouncement Widget；
- `PlayerLeftGame()`；
- Character 离开完成事件；
- 返回主菜单时的会话清理。

### 7.4 队伍颜色没有应用

[`BrokenEdgePlayerState.h`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.h) 的 `SetTeam()` 当前只给枚举赋值，[`BrokenEdgePlayerState.cpp`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.cpp) 的 `OnRep_Team()` 为空。

还需补齐：

- Character 的 `SetTeamColor(ETeam)`；
- 服务端 `SetTeam()` 后立即设置材质；
- 客户端 `OnRep_Team()` 后设置材质；
- PlayerState 晚到时的初始化；
- 重生后重新应用队伍外观。

### 7.5 比赛开始时没有补分队

当前 `PostLogin()` 会分队，已有日志证明这条路径执行过。但参考项目仍在 `HandleMatchHasStarted()` 中遍历 `GameState->PlayerArray`，为仍处于 `ET_NoTeam` 的玩家补分队。

该防线对以下场景有价值：

- PlayerState 初始化顺序变化；
- Seamless Travel；
- 中途加入；
- 蓝图或测试代码产生未分队玩家。

### 7.6 缺少阵营出生点

参考项目使用 `TeamPlayerStart`，按 PlayerState 的队伍选择出生点。BrokenEdge 当前重生只会从普通 `APlayerStart` 中随机选择，因此无法保证红蓝双方从各自区域出生。

还需新增：

- `ATeamPlayerStart`；
- 可编辑的 `Team` 属性；
- 红蓝出生点 Blueprint；
- 按队伍筛选出生点；
- 没有可用阵营出生点时的安全回退。

### 7.7 团队伤害计算存在空指针入口

[`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp):157-165 会把 `OnTakeAnyDamage` 提供的 `InstigatorController` 原样传给 GameMode。环境伤害、无 Controller 的伤害源或部分测试调用允许该参数为空。

但 [`TeamsGameMode.cpp`](../Source/BrokenEdge/GameMode/TeamsGameMode.cpp):96-100 在任何空值判断之前执行：

```cpp
Attacker->GetPlayerState<ABrokenEdgePlayerState>();
Victim->GetPlayerState<ABrokenEdgePlayerState>();
```

因此当前判空保护来得太晚。应先检查 `Attacker`、`Victim`，再取得 PlayerState，并明确约定环境伤害、自伤和无队伍玩家的伤害策略。参考项目也有相同写法，不能直接照搬。

### 7.8 重生对空出生点数组没有保护

[`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp):76-81 在 `PlayerStarts.Num() == 0` 时仍会执行：

```cpp
FMath::RandRange(0, PlayerStarts.Num() - 1);
PlayerStarts[Selection];
```

这会产生无效范围和数组越界。至少应在数组为空时记录明确错误并使用安全回退，例如 `RestartPlayer(DefeatedController)`，或延迟重试；团队模式完成后还应优先从本队出生点集合选择。参考项目的随机重生代码同样缺少空数组保护。

### 7.9 自杀会错误增加团队分

基类 [`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp):52-57 会排除 `AttackerPlayerState == DefeatedPlayerState` 的个人加分，但子类 [`TeamsGameMode.cpp`](../Source/BrokenEdge/GameMode/TeamsGameMode.cpp):77-93 随后只检查攻击者队伍，没有再次验证攻击者与受害者不同。

当前 `CalculateDamage()` 又明确允许自伤，因此玩家自杀后可进入团队加分分支。团队分应只在“有效敌方击杀”时增加；如果项目希望自杀扣分，也应作为单独规则显式实现，而不是复用普通击杀路径。

## 8. 会话匹配层的确定缺口

### 8.1 Menu 没有消费 FindSessions 结果

[`Menu.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/Menu.cpp) 中，以下绑定仍被注释：

```cpp
MultiplayerOnFindSessionsComplete.AddUObject(...);
MultiplayerOnJoinSessionComplete.AddUObject(...);
MultiplayerOnDestorySessionComplete.AddDynamic(...);
MultiplayerOnStartSessionComplete.AddDynamic(...);
```

`JoinButtonClicked()` 目前只调用：

```cpp
MultiplayerSessionsSubsystem->FindSessions(20000);
```

当前 Menu 没有：

- `OnFindSessions()`；
- 遍历 `SessionResults`；
- 读取会话设置中的 `MatchType`；
- 选择第一个匹配结果；
- 调用 `JoinSession()`；
- 处理没有匹配房间的情况；
- 搜索失败后可靠恢复 Join 按钮。

因此即使 OnlineSubsystem 成功返回搜索结果，界面也不会继续加入。

### 8.2 Join 完成回调没有建立

[`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp) 的构造函数只初始化 Create 和 Find 委托，Join 与 Destroy 委托初始化被注释。

当前还缺：

- `OnJoinSessionComplete(FName, EOnJoinSessionCompleteResult::Type)` 声明；
- 构造函数中的 Join 委托绑定；
- 回调触发后的 Handle 清理；
- 向 Menu 广播具体 Join 结果；
- Menu 中的 `OnJoinSession()`；
- `GetResolvedConnectString(NAME_GameSession, Address)`；
- `PlayerController->ClientTravel(Address, TRAVEL_Absolute)`。

这意味着 `JoinSession()` 函数即使被直接调用，也没有完整的异步完成链把玩家送入服务器地图。

### 8.3 已有会话时无法销毁后重建

`CreateSession()` 检测到 `NAME_GameSession` 已存在时会设置：

- `bCreateSessionOnDestory`；
- `LastNumPublicConnections`；
- `LastMatchType`。

但是实际 `DestorySession()` 调用被注释，随后函数直接 `return`。结果是：

- 不会销毁旧会话；
- 不会创建新会话；
- 不会触发 Create 完成广播；
- Host 按钮可能一直保持禁用。

正确流程应为：

```text
发现旧会话
  -> 缓存新参数
  -> 异步 DestroySession
  -> OnDestorySessionComplete(success)
  -> 清理 Destroy 委托
  -> CreateSession(缓存参数)
  -> 等待新的 Create 完成回调
```

### 8.4 `MatchType` 有被写成空值的风险

当前代码先执行：

```cpp
DesiredMatchType = MoveTemp(MatchType);
```

之后又用已经移动过的局部参数设置会话：

```cpp
LastSessionSettings->Set(FName("MatchType"), MatchType, ...);
```

移动后的 `FString` 仍可析构和重新赋值，但不能继续假定它保留原内容。这里应使用保留下来的 `DesiredMatchType`，或者一开始直接复制参数。

如果广告出去的 `MatchType` 为空，Menu 即使补上搜索回调，也无法用 `FreeForAll`、`Teams` 或 `CaptureTheFlag` 正确筛选会话。

### 8.5 错误路径和生命周期处理不完整

当前还应补齐：

- World、SessionInterface、LocalPlayer、NetId 的统一校验；
- 接口无效时向 UI 广播失败，而不是静默返回；
- Create/Find/Join/Destroy 的重复请求保护；
- 每条立即失败路径的 Handle 清理和 `Reset()`；
- `Deinitialize()` 中清除尚未完成的在线委托；
- World/PIE Context 变化后重新取得 SessionInterface；
- Host 创建失败时恢复 Host 按钮；
- Join 失败、无结果、无匹配模式时恢复 Join 按钮。

具体来说：

- `FindSessions()` 在 `SessionInterface` 无效时直接返回，没有广播失败，已被禁用的 Join 按钮不会恢复；
- `CreateSession()` 会调用 `RefreshSessionInterface()`，但 Find/Join/Destroy 只检查旧指针，没有统一刷新 World 对应接口；
- `RefreshSessionInterface()` 检测到接口变化时，本应先清理旧接口委托的代码仍被注释；
- Join/Destroy 委托对象没有在构造函数中绑定，且没有 `OnJoinSessionComplete()`/`OnDestorySessionComplete()` 实现；
- `Deinitialize()` 被注释，GameInstance 销毁时无法集中清理未完成操作；
- 没有使用 Delegate Handle 阻止同类型异步请求重入；
- `MenuSetup()` 使用 `AddDynamic()`，重复调用前没有先移除 Create 委托，存在重复通知风险。

参考项目当前版本已经为 Create/Find/Join/Destroy 增加失败广播、Handle 重置、重复请求保护和 `Deinitialize()`，适合借鉴结构；但 Dedicated Server 身份策略仍需按 BrokenEdge 自己的目标单独设计。

### 8.6 会话参数校验和日志语义不一致

子系统定义了 `MaxSupportedPublicConnections = 64` 和 `MaxSupportedSearchResults = 100`，但当前没有使用：

- `CreateSession()` 只把 `DesiredNumPublicConnections` 最低限制为 1，真正写入设置时仍使用原始 `NumPublicConnections`；
- `Menu::JoinButtonClicked()` 直接请求 `FindSessions(20000)`；
- `CreateSession()` 的同步返回 `true` 只表示异步请求已成功提交，当前日志却写“创建会话成功”。

建议使用同一份经过 Clamp 的参数写入成员和 `FOnlineSessionSettings`，限制搜索数量，并把日志区分为“请求已提交”和“完成回调成功”。

## 9. Lobby、地图和 MatchType 路由缺口

### 9.1 当前地图入口不一致

[`DefaultEngine.ini`](../Config/DefaultEngine.ini) 当前配置：

```ini
GameDefaultMap=/Game/Maps/MainMap.MainMap
EditorStartupMap=/Game/Maps/MainMap.MainMap
ServerDefaultMap=/Game/Maps/MainMap.MainMap
```

资源引用静态检查显示：

- `MainMap.umap` 引用了 `BP_TeamsGameMode`；
- `MainMap.umap` 引用了 `WBP_Menu` 和 `/Game/Maps/Lobby`；
- `Lobby.umap` 引用了 `BP_BrokenEdgeGameMode`；
- Menu 的 C++ 默认 `MatchType` 是 `FreeForAll`。

这形成了模式语义不一致：菜单默认广告 FFA，当前正式主地图却使用 Teams GameMode。

### 9.2 当前没有 LobbyGameMode

参考项目的 `LobbyGameMode::PostLogin()` 会：

1. 读取 `GameState->PlayerArray.Num()`；
2. 读取会话子系统的 `DesiredNumPublicConnections`；
3. 人数达到目标后开启 `bUseSeamlessTravel`；
4. 根据 `DesiredMatchType` 跳转到 FFA、Teams 或 CTF 地图。

BrokenEdge 当前没有对应的 `LobbyGameMode.h/.cpp`，也没有在可检索 C++ 中发现按人数和 MatchType 从 Lobby 进入正式地图的路由。

因此 Host 创建会话并进入 `/Game/Maps/Lobby?listen` 后，没有已确认的 C++ 自动开局路径。

### 9.3 应先明确项目支持的模式

当前最现实的选择有两种：

#### 方案 A：先只完成 Teams

- Menu 默认 `MatchType` 改为 `Teams`；
- Lobby 达到人数后统一进入 `MainMap`；
- `MainMap` 继续使用 `BP_TeamsGameMode`；
- 暂不暴露 FFA/CTF 选项。

该方案改动范围最小，适合先把一个完整对局闭环跑通。

#### 方案 B：对齐参考项目的多模式路由

- 新增独立 FFA、Teams、CTF 地图或明确的 GameMode Override；
- Menu 提供模式参数；
- Session 广告正确的 `MatchType`；
- LobbyGameMode 根据模式选择地图；
- 各地图配置对应 GameMode。

在 FFA 排名和 CTF 规则尚未完成前，不建议先对外暴露这两个模式。

## 10. OnlineSubsystem 与 Dedicated Server 边界

### 10.1 OnlineSubsystemNull 不是缺陷

当前 [`DefaultEngine.ini`](../Config/DefaultEngine.ini) 使用：

```ini
[OnlineSubsystem]
DefaultPlatformService=Null
```

这适合：

- 本机多进程测试；
- 同一局域网中的独立机器；
- 在接入 Steam 前验证基础 Session 流程。

它不提供参考项目 Steam 配置对应的公网发现能力。因此：

- 如果目标只是 LAN，Null 可以保留；
- 如果目标是 Steam 公网会话，才需要启用 OnlineSubsystemSteam、NetDriver、AppId 和独立账号/进程测试；
- 不应把同进程 PIE 自动连接当作 Null 的搜索/加入成功证据。

### 10.2 Dedicated Server 路径目前没有闭合

项目已有：

- `BrokenEdgeServer.Target.cs`；
- `DefaultGame.ini` 中的连接数和 `bAutoStartSession=True` 配置；
- `IsDedicatedServerInstance()` 检查。

但当前没有找到读取 `[MultiplayerSessions.DedicatedServer]` 并在服务器启动时自动创建会话的业务调用链。

此外当前 `CreateSession()` 仍取得：

```cpp
const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
```

并直接解引用其 NetId。真正的 Dedicated Server 没有本地玩家，这条路径不能作为专服注册实现。

建议先完成 Listen Server + Null/LAN 的创建、查找、加入闭环，再单独设计 Dedicated Server 的无 UI、无 LocalPlayer 注册流程。

## 11. 推荐实现顺序

### P0：先修正错误计分、崩溃入口和比赛状态机

涉及文件：

- [`BrokenEdgeGameMode.h`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.h)
- [`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp)
- [`BrokenEdgePlayerController.h`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.h)
- [`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp)

任务：

1. 修复 BE-SCORE-001：攻击者只增加 Score，受害者独立增加 Defeats；
2. 修复 BE-CRASH-001：团队伤害在使用 Controller 前判空；
3. 修复 BE-CRASH-002：无 PlayerStart 时安全回退；
4. 修复 BE-TEAM-001：团队分只奖励有效敌方击杀；
5. 增加 `MatchState::Cooldown`；
6. 设置 `bDelayedStart`；
7. 实现 GameMode Tick 三阶段切换；
8. 实现 `OnMatchStateSet()`；
9. 注册 PlayerController 的 `MatchState` 复制；
10. `BeginPlay()` 请求当前比赛状态；
11. 完成 `ClientJoinMidgame()` 和 `OnRep_MatchState()`。

验收标准：

- 日志不再出现 `MatchState was not registered`；
- Server 和两个 Client 的 MatchState 一致；
- 中途加入客户端立即进入当前阶段，而不是重新从完整热身开始；
- 热身结束自动开始，比赛结束进入冷却，冷却结束重开。
- 环境伤害不会造成空指针崩溃；
- 无 PlayerStart 时不会数组越界，并能记录可定位的错误；
- 普通击杀、自杀、环境死亡分别得到符合设计的个人分、Defeats 和团队分。

### P1：完成倒计时、胜负和队伍表现

涉及文件：

- [`BrokenEdgeHUD.h`](../Source/BrokenEdge/UI/BrokenEdgeHUD.h)
- [`BrokenEdgeHUD.cpp`](../Source/BrokenEdge/UI/BrokenEdgeHUD.cpp)
- [`CharacterOverlay.h`](../Source/BrokenEdge/UI/CharacterOverlay.h)
- [`BrokenEdgePlayerState.h`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.h)
- [`BrokenEdgePlayerState.cpp`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.cpp)
- [`TeamsGameMode.h`](../Source/BrokenEdge/GameMode/TeamsGameMode.h)
- [`TeamsGameMode.cpp`](../Source/BrokenEdge/GameMode/TeamsGameMode.cpp)

任务：

1. 增加 Announcement Widget 和 C++ 包装类；
2. 实现比赛/公告倒计时；
3. Cooldown 显示红队、蓝队或平局结果；
4. Cooldown 禁止移动/射击；
5. 实现 `OnRep_Team()` 和角色队伍颜色；
6. 比赛开始时补分队；
7. 增加 TeamPlayerStart 和阵营出生点；
8. 恢复 PlayerState 晚到及重生后的 HUD/队伍初始化；
9. 让 `bShowTeamScores` 真正控制 HUD。

验收标准：

- 三端倒计时误差稳定在可接受范围；
- 所有端看到一致的红蓝比分；
- 队伍颜色和出生区域一致；
- 同队伤害为 0，自伤规则符合设计；
- Late Join 能获得当前队伍、比分、MatchState 和剩余时间。

### P2：完成 Session Join 和 Lobby 路由

涉及文件：

- [`MultiplayerSessionsSubsystem.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/MultiplayerSessionsSubsystem.h)
- [`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp)
- [`Menu.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/Menu.h)
- [`Menu.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/Menu.cpp)
- 新增 `LobbyGameMode.h/.cpp`
- `Lobby.umap` 的 GameMode Override

任务：

1. 绑定 Find/Join/Destroy 完成委托；
2. 补齐所有原生完成回调；
3. Menu 按 `MatchType` 自动选择第一个匹配房间；
4. Join 成功后解析地址并 `ClientTravel()`；
5. 完成旧会话 Destroy 后重建；
6. 修正移动后的 `MatchType`；
7. 补齐失败广播、按钮恢复和重复请求保护；
8. 增加 `Deinitialize()` 委托清理；
9. 统一 World Context 对应的 SessionInterface 获取方式；
10. Clamp 会话连接数和搜索结果数，并修正异步日志语义；
11. Lobby 达到人数后按模式进入正式地图。

验收标准：

- 使用两个独立 Standalone 进程，不使用同进程 PIE 自动连接；
- Host 创建会话并进入 Lobby；
- 第二个进程可以搜索到正确 `MatchType`；
- Join 成功后 ClientTravel 到主机；
- 人数达到目标后所有玩家一起进入正式地图；
- 无结果、模式不匹配、Join 失败时按钮均恢复；
- 主机重复建房时先销毁旧会话，再收到新建房结果。

### P3：补全模式和会话生命周期

任务：

- FFA 的 `TopScoringPlayers`、并列第一和领先表现；
- 淘汰播报；
- ReturnToMainMenu；
- 玩家主动离开与 `DestroySession()`；
- CTF GameMode、FlagZone、旗帜重置和计分；
- Steam 公网会话；
- Dedicated Server 自动注册与服务器启动参数。

这些功能应建立在 P0～P2 已通过独立进程验收的基础上。

## 12. 建议的最小可玩目标

为了避免同时扩展 FFA、Teams、CTF、Steam 和 Dedicated Server，建议先完成一个最小闭环：

```text
OnlineSubsystemNull
  + Listen Server
  + Teams MatchType
  + MainMap / BP_TeamsGameMode
  + 2 个独立进程
  + WaitingToStart -> InProgress -> Cooldown -> Restart
  + 分队、友伤过滤、击杀计分、重生、胜负显示
```

这个闭环通过后，再扩展公网平台和其他模式。这样能明确区分：

- 会话是否建立；
- 客户端是否真正通过 Find/Join 进入；
- Lobby 是否正确旅行；
- 对局状态是否复制；
- 计分和 HUD 是否一致。

## 13. 运行验收清单

### 13.1 比赛状态

- [ ] Server 显示 `WaitingToStart`；
- [ ] 所有 Client 获得相同 MatchState；
- [ ] 热身倒计时结束后只由服务器启动比赛；
- [ ] 比赛结束后进入 Cooldown；
- [ ] Cooldown 时所有端禁止继续开火；
- [ ] 冷却结束后服务器重开游戏；
- [ ] Late Join 不会从完整 Warmup 重新计时。

### 13.2 队伍与计分

- [ ] 玩家人数保持红蓝队平衡；
- [ ] 玩家退出后从正确队伍数组移除；
- [ ] 同队攻击不扣血；
- [ ] 自伤行为符合项目设计；
- [ ] 无 Instigator 的环境伤害不会崩溃；
- [ ] 攻击者 Score 增加一次；
- [ ] 受害者 Defeats 增加一次；
- [ ] 红蓝队分只增加一次；
- [ ] 自杀和环境死亡不会被当成普通敌方击杀奖励团队分；
- [ ] 地图缺少 PlayerStart 时不会访问空数组；
- [ ] Listen Server、受击 Client、第三方 Client 看到相同比分；
- [ ] 队伍颜色和出生点正确；
- [ ] Cooldown 显示正确胜者或平局。

### 13.3 Session 与旅行

- [ ] Host 创建请求成功提交；
- [ ] Create 完成回调报告最终结果；
- [ ] SessionSettings 中 `MatchType` 非空且正确；
- [ ] 独立 Client 搜索到会话；
- [ ] Menu 过滤并选择正确 MatchType；
- [ ] Join 完成回调返回 Success；
- [ ] 成功解析连接字符串；
- [ ] ClientTravel 后获得 Pawn 和游戏输入；
- [ ] Lobby 达到人数后使用正确 GameMode 进入正式地图；
- [ ] 旅行后 PlayerState、Team、Score、Defeats 和 MatchState 正确；
- [ ] 主机和客户端退出后会话状态得到清理。

### 13.4 测试拓扑

至少分别验证：

1. PIE Listen Server + Clients：用于快速检查游戏复制；
2. 两个独立 Standalone 进程：用于验证 Null/LAN Find/Join；
3. 打包 Listen Server + Client：用于验证 Cook 后地图、Widget 和会话；
4. Dedicated Server + Clients：仅在专服注册路径完成后验证；
5. 网络延迟/丢包：最后验证倒计时、击杀、重生和 Late Join。

不要用“PIE 第二个窗口自动跟随主机 ServerTravel”证明 FindSessions 和 JoinSession 已成功。

## 14. 当前已有运行证据

本机现有 `Saved/Logs/BrokenEdge.log` 中记录到：

- `ATeamsGameMode::PostLogin` 将玩家分配到蓝队；
- `ATeamsGameMode::PostLogin` 将玩家分配到红队；
- `BrokenEdgePlayerController::MatchState` 未登记到 `GetLifetimeReplicatedProps()` 的警告（当前日志第 1244 行）。

这些日志可以证明：

- 当前分队入口实际执行过；
- MatchState 当前确实不会按声明进行复制。

这些日志不能证明：

- 红蓝队分已在所有客户端正确显示；
- Find/Join 会话成功；
- Lobby 自动进入正式比赛；
- Cooldown、胜负和重开可用；
- Dedicated Server 已注册到可发现会话。

## 15. 与旧路线图的关系

综合路线仍可参考 [BrokenEdge 已完成功能、网络同步分析与后续开发路线](BrokenEdge_Project_Status_Network_Replication_Roadmap.md)，但该文档的源码基线是 `4ddc16f`，早于当前 `9f5d387`。

旧文档中以下结论已经过时：

- 红蓝队分 RepNotify 写反；
- `TeamsGameMode::PostLogin()` 尚未实现；
- 伤害、淘汰和重生调用链完全缺失；
- PlayerState 没有 `GetTeam()/SetTeam()`。

旧文档中以下结论仍与当前源码一致：

- GameMode 比赛状态机未完成；
- PlayerController 的 MatchState 未登记复制；
- `ClientJoinMidgame()` 和 `OnRep_MatchState()` 为空；
- HUD 没有倒计时业务；
- Menu Find/Join/ClientTravel 不完整；
- Destroy 后重建和 Dedicated Server 会话路径未闭合。

涉及匹配功能时，应以本文和最新源码为准。

## 16. 关键文件索引

### 当前项目

- GameState 与队伍分数：[`BrokenEdgeGameState.h`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.h)、[`BrokenEdgeGameState.cpp`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.cpp)
- 基础比赛规则：[`BrokenEdgeGameMode.h`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.h)、[`BrokenEdgeGameMode.cpp`](../Source/BrokenEdge/GameMode/BrokenEdgeGameMode.cpp)
- 队伍规则：[`TeamsGameMode.h`](../Source/BrokenEdge/GameMode/TeamsGameMode.h)、[`TeamsGameMode.cpp`](../Source/BrokenEdge/GameMode/TeamsGameMode.cpp)
- 玩家比赛状态与 HUD 桥接：[`BrokenEdgePlayerController.h`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.h)、[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp)
- 玩家分数与队伍：[`BrokenEdgePlayerState.h`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.h)、[`BrokenEdgePlayerState.cpp`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.cpp)
- 伤害、淘汰和重生入口：[`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp)
- HUD：[`BrokenEdgeHUD.h`](../Source/BrokenEdge/UI/BrokenEdgeHUD.h)、[`BrokenEdgeHUD.cpp`](../Source/BrokenEdge/UI/BrokenEdgeHUD.cpp)、[`CharacterOverlay.h`](../Source/BrokenEdge/UI/CharacterOverlay.h)
- 会话子系统：[`MultiplayerSessionsSubsystem.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/MultiplayerSessionsSubsystem.h)、[`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp)
- 会话菜单：[`Menu.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/Menu.h)、[`Menu.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/Menu.cpp)
- 地图和 OSS 配置：[`DefaultEngine.ini`](../Config/DefaultEngine.ini)、[`DefaultGame.ini`](../Config/DefaultGame.ini)
- Dedicated Server Target：[`BrokenEdgeServer.Target.cs`](../Source/BrokenEdgeServer.Target.cs)

### 参考项目

```text
D:\SourceFiles\UnrealProjects\UE5BlasterLearning-main\
├─ Source\BlasterLearing\GameState\BlasterGameState.cpp
├─ Source\BlasterLearing\GameMode\BlasterGameMode.cpp
├─ Source\BlasterLearing\GameMode\TeamsGameMode.cpp
├─ Source\BlasterLearing\GameMode\LobbyGameMode.cpp
├─ Source\BlasterLearing\GameMode\CaptureTheFlagGameMode.cpp
├─ Source\BlasterLearing\PlayerController\BlasterPlayerController.cpp
├─ Source\BlasterLearing\PlayerState\BlasterPlayerState.cpp
├─ Source\BlasterLearing\Character\BlasterCharacter.cpp
├─ Source\BlasterLearing\HUD\BlasterHUD.cpp
├─ Source\BlasterLearing\HUD\ReturnToMainMenu.cpp
└─ Plugins\MultiplayerSessions\Source\MultiplayerSessions\
   ├─ Private\MultiplayerSessionsSubsystem.cpp
   └─ Private\Menu.cpp
```

参考项目用于确认依赖关系和目标形态。实现时仍应结合 BrokenEdge 的现有类名、地图、Widget、网络拓扑和设计目标，避免逐行照搬。
