# BrokenEdge 功能与实现全景

> **盘点日期**：2026-11-06　|　**引擎**：Unreal Engine 5.8.3（源码版）　|　**平台**：Windows 64
>
> **本文是什么**：对当前工作区 `Source/`、`Plugins/`、`Config/`、`Scripts/`、`Deployment/` 的一次**源码静态盘点**，回答「实现了哪些功能」以及「每个功能具体怎么实现」。所有关键结论都抽查过源码中的类、函数、RPC、复制注册和配置项。
>
> **本文不是什么**：没有启动 UE 编辑器、没有编译、没有运行 PIE / 独立专服 / 打包产物，也没有打开二进制 `.uasset` 目视确认蓝图连线。「已实现」= 源码中存在完整调用链，**不等于**已通过实机、画面、跨机器网络或长时间稳定性验收。二进制资产（行为树、Widget 控件树、DataAsset、蒙太奇）只确认被 C++ 引用，不确认其内部配置。
>
> **重要提醒**：[`BrokenEdge_Project_Status_Network_Replication_Roadmap.md`](BrokenEdge_Project_Status_Network_Replication_Roadmap.md) 是 **2026-08-25 的历史快照**（基于提交 `4ddc16f`、UE 5.5.4）。其中「未实现开火 / 命中扫描 / 淘汰重生 / 队伍规则 / Join 闭环」等结论**已被后续开发推翻**，本文以当前源码为准。旧文档仍可作为教学路线与排错思路参考，不要当作现状清单。

---

## 1. 项目定位与规模

**一句话**：以「航天基地搜打撤」为主玩法、同时保留自由模式 / 团队模式对战的 UE5.8.3 多人射击项目；专用服务器权威，客户端第一人称、远端角色保留世界人物表现。

| 项目 | 实测值 |
| --- | --- |
| C++ / 头文件（`Source/` + `Plugins/`） | 261 个文件、约 46,600 行 |
| 其中搜打撤模块 `Source/BrokenEdge/Extraction/` | 41 个 `.cpp`、约 13,500 行 |
| 编辑器/验收脚本 `Scripts/` | 345 个 Python（另有 4 个 ps1） |
| `Content/` 资产 | 约 27,200 个 `.uasset`、27 张 `.umap` |
| 运行时模块 | `BrokenEdge`（Runtime）、`MultiplayerSessions`（插件 Runtime） |
| 编辑器专用模块 | `StingerTools`（`UncookedOnly`，仅 Editor Target 加载） |
| 构建目标 | `BrokenEdge`（Game）、`BrokenEdgeEditor`、`BrokenEdgeServer` |
| 联机后端 | `OnlineSubsystemNull`（局域网），**未接 Steam / 公网目录** |
| 输入 | Enhanced Input（`IA_*` / `IMC_*`），`DefaultInput.ini` 保留遗留 Action/Axis 映射 |
| UI | UMG（经典 HUD / 大厅 / 搜刮）+ 纯 Slate（仓库 / 改枪 / 战术地图） |
| 大文件 | Git LFS |

**三个层次的玩法**（共享同一套角色、武器、网络基础，但 GameMode 与状态机各自独立）：

```text
大厅 Lobby（专用服务器常驻）
   ├─ FreeForAll  → ABrokenEdgeGameMode     经典个人计分 / 重生
   ├─ Teams       → ATeamsGameMode          分队 / 友伤 / 队伍分
   └─ Extraction  → AExtractionGameMode    搜打撤战局（主干，代码量最大）
                      └─ WeaponRangeGameMode  本地枪械训练场（隔离存档，不写长期档案）
```

---

## 2. 总体架构

```text
┌─ 会话与进程 ────────────────────────────────────────────────────┐
│ OnlineSubsystemNull → UMultiplayerSessionsSubsystem（GameInstance） │
│ 专服自动建会话 / 客户端 Find → Join → ClientTravel                  │
│ ALobbyGameMode → StartSession → ServerTravel(按 MatchType 选图)      │
└──────────────────────────────────────────────────────────────────┘
                              ↓ 无缝旅行（bUseSeamlessTravel）
┌─ 权威层（服务器） ───────────────────────────────────────────────┐
│ AExtractionGameMode / ABrokenEdgeGameMode                          │
│  ├ 阶段状态机、伤害仲裁、出生/撤离/结算                            │
│  ├ UExtractionPersistence（GameInstanceSubsystem，跨关卡存档）      │
│  ├ UExtractionCombatComponent / UCombatComponent（两套战斗系统）    │
│  ├ UExtractionInventoryComponent（背包事务）                        │
│  └ AExtractionGuard（AI Controller + 行为树）                      │
└──────────────────────────────────────────────────────────────────┘
       ↓ Replicated / ReplicatedUsing / COND_OwnerOnly / Client RPC
┌─ 表现层（客户端） ───────────────────────────────────────────────┐
│ ABrokenEdgeCharacter（第一人称网格 / 动画 / 遍历 CMC 预测）         │
│ UExtractionInteractionComponent（本地焦点 + 服务器校验）            │
│ UMG：WBP_CharacterOverlay / ULobbyWidget / UExtractionWidget /     │
│      ULootingMainView  ｜  Slate：仓库 / 进度 / 改枪 / 战术地图     │
└──────────────────────────────────────────────────────────────────┘
       ↑ 编辑器侧生成／摆放
┌─ 工具层（仅编辑器） ─────────────────────────────────────────────┐
│ StingerTools（C++ Builder）+ Scripts/*.py（UnrealEditor-Cmd 调用）  │
│ 生成 Widget / 蓝图、摆关卡 Actor、写 DataAsset、烘焙俯视图、验收   │
└──────────────────────────────────────────────────────────────────┘
```

**设计主线**：所有会改变结果的数据（伤害、物品、结算、战局阶段）只在服务器计算；客户端只做输入、预测与表现。物品容器内容**根本不复制**给客户端，只发「脱敏快照」；弹药、背包等敏感数据走 `COND_OwnerOnly`。

---

## 3. 功能总览

| # | 功能域 | 核心实现 | 状态 |
| --- | --- | --- | --- |
| 1 | 专服会话 / 房间发现 / 加入 | `UMultiplayerSessionsSubsystem` + `UMenu` | 已实现（仅 LAN / Null） |
| 2 | 大厅准备、模式与地图切换、无缝跳图 | `ALobbyGameMode` + `ALobbyGameState` | 已实现 |
| 3 | 经典 FFA / Teams 对战 | `ABrokenEdgeGameMode` / `ATeamsGameMode` | 主体完成，**比赛计时状态机未接线** |
| 4 | 角色输入、移动、蹲伏/趴下/冲刺/游泳、动画 | `ABrokenEdgeCharacter` + `UTraversalMovementComponent` | 已实现 |
| 5 | 武器拾取、主副武器、切枪换弹丢弃 | `UCombatComponent` + `AWeapon` | 已实现 |
| 6 | 双轨射击：hitscan / 投射物 / 霰弹 + 实体弹道 | `AHitScanWeapon` ↔ `AExtractionBullet` + `WeaponMechanics.json` | 已实现 |
| 7 | 伤害、护盾/护甲、创伤、Buff、拾取物 | `ReceiveDamage` / `UExtractionHealth` / `UBuffComponent` | 已实现 |
| 8 | 延迟补偿（服务器回溯） | `ULagCompensationComponent` | 老路径已接入，**搜打撤模式明确关闭** |
| 9 | 搜打撤战局状态机 / 出生 / 撤离 / 结算 / 观战 | `AExtractionGameMode` + `AExtractionGameState` | 已实现 |
| 10 | 物品唯一身份、胸挂/背包/安全箱网格、搜刮、配装改枪 | `ExtractionItems` / `ExtractionEquipment` / `UExtractionInventoryComponent` | 已实现（自定规则） |
| 11 | 账号、仓库、经济与任务、持久化 | `UExtractionPersistence` + `UExtractionAccountComponent` + `UExtractionProgression` | 已实现（服务器本地 JSON 日志） |
| 12 | 守卫 AI（感知 / 巡逻 / 交战 / 掩体 / 听觉） | `AExtractionGuard` + 行为树 Service/Task | 已实现（行为树本体在蓝图） |
| 13 | 场景交互（门 / 钥匙房 / 电闸 / 桥 / 水 / 滑索 / 撤离点） | `Extraction/`、`Traversal/` 下各 AActor | 已实现（多为项目自定规则） |
| 14 | 攀越 / 梯子 / 游泳 / 区域流式加载 | `UTraversalMovementComponent` + `AExtractionRegionStream` | 已实现（含网络预测） |
| 15 | HUD / 大厅 / 搜刮 / 仓库 / 战术地图 | UMG + Slate 混合 | 已实现 |
| 16 | 自定义动画节点、资产生成工具、打包与自动验收 | `StingerTools` + `Scripts/` + `*.bat` | 已实现 |

---

## 4. 联机与大厅：项目是怎么跑起来的

### 4.1 会话层（`Plugins/MultiplayerSessions/`）

| 能力 | 实现方式 |
| --- | --- |
| 创建会话 | `UMultiplayerSessionsSubsystem::CreateSession()`（`MultiplayerSessionsSubsystem.cpp:168`）**只允许专服**（`IsDedicatedServerInstance()`），`FOnlineSessionSettings` 设 `bIsDedicated / bShouldAdvertise = true`，写自定义键 `MatchType`、`ProjectName`、`RoomName`，调用 `SessionInterface->CreateSession(0, NAME_GameSession, ...)`，完成回调 `OnCreateSessionComplete`。Extraction 模式下 `bAllowJoinInProgress=false`。 |
| 专服初始化 | `InitializeDedicatedServer()`（`:217`）从 `GGameIni`（`Config/DefaultGame.ini` 的 `[MultiplayerSessions.DedicatedServer]`）读 `NumPublicConnections=8`、`MinPlayersToStart=2`、`MatchType=Teams`、`bAutoStartSession=True`，再用 `FParse::Value` 读命令行 `-NumPublicConnections= / -MinPlayersToStart= / -MatchType= / -RoomName=` 覆盖，Clamp 后调 `CreateSession`。 |
| 搜索 | `FindSessions()`（`:240`）用 `ULocalPlayer` 的 NetId，`bIsLanQuery`（Null）、`SEARCH_DEDICATED_ONLY=true`、`MaxSearchResults=10000`，`OnFindSessionsComplete` 把结果**快照**广播出去。 |
| 加入 | `JoinSession()`（`:264`）校验会话是专服，成功后 `OnJoinSessionComplete` → `GetResolvedConnectString()` → `APlayerController::ClientTravel(Address, TRAVEL_Absolute)`（`Menu.cpp:236-244`）。 |
| 房间列表 | `UMenu` 用纯 C++ 构建 UMG（`Initialize()`），`OnFindSessions` 按 `ProjectName == FApp::GetProjectName() && bIsDedicated` 过滤，`RebuildRoomList` 显示「房名#ID 后 6 位 / 模式 / 人数 / Ping」。**Host 按钮已折叠**，点击只提示「请启动专用服务器」——建房能力被刻意废弃。 |
| 失败恢复 | `GEngine->OnNetworkFailure / OnTravelFailure` → `CleanUpSession()` → `DestorySession()` → 复原并可重搜。 |
| 模式热更新 | `UpdateMatchType()`（`:120`）允许运行中改 `MatchType` / 容量 / 房名，`OnUpdateMatchTypeComplete` 提交；大厅的模式按钮通过四个 `Server, Reliable` RPC（`ServerSetLobbyReady / ServerSetExtractionSquad / ServerSetLobbyMatchType / ServerSetExtractionMap`）走 `ALobbyGameMode::SetMatchType`。 |

> **已知边界**：`Menu.cpp:199` 的「可加入」条件只列 `FreeForAll | Teams`，Extraction 房间不能从房间列表直接进入，只能靠已有会话 / 大厅准备流程。`bAutoStartSession` 没有同名命令行覆盖。客户端建房路径不存在（这是设计，不是遗漏）。

### 4.2 大厅准备与跳图（`Source/BrokenEdge/GameMode/LobbyGameMode.cpp`）

```text
专服 BeginPlay(:81)
  → UExtractionPersistence::AbortPreparedRaids()      清理上次崩溃残留的出战记录
  → Sessions->InitializeDedicatedServer()             自动建会话，失败每 5 秒重试
  → GameSession->MaxPlayers = DesiredNumPublicConnections
  → SetTimer(ReadyCheckTimer, TryStartGame, 0.25s)    每 0.25 秒检查开局条件

客户端 UI 点「准备 / 改模式 / 改地图 / 编队」
  → Server,Reliable RPC → ABrokenEdgePlayerState::SetLobbyReady(bLobbyReady Replicated)
  → ALobbyGameMode::TryStartGame(:258)
      ├ 全员 ready + 人数 ≥ MinPlayersToStart（Extraction 另校验档案 / 小队 ≤3 / 总 ≤6）
      ├ 地图包存在性检查（FPackageName::DoesPackageExist）
      ├ Extraction 额外校验 MapRegistry / Persistence / PIE 无缝旅行条件
      ├ GS->CountdownEndTime = ServerWorldTime + 3s   3 秒倒计时（状态复制到客户端）
      └ 到点 bStartingSession=true → Sessions->StartSession()
  → OnStartSession(:328)
      ├ Extraction：Store->BeginRaid(Profiles, Raid) 保存出战票据，写 PS->ExtractionRaid
      └ ServerTravel("<目标地图>?MaxPlayers=N?ExpectedPlayers=M")(:369)
  → 失败时 AbortRaid() 回滚，CancelCountdown() 重试
```

**模式 → 地图**：`SessionModes::Map`（`Public/SessionModes.h`）把 `FreeForAll → /Game/Maps/FreeForAll`、`Teams → /Game/Maps/Teams`、`Extraction → /Game/Maps/SpaceCenter/L_SpaceCenterExtraction`；Extraction 实际由 `UExtractionMapRegistry->Find(SelectedMap)->Level` 覆盖，注册表来自 `DefaultGame.ini` 的两张 DataAsset。

**无缝旅行**：`ALobbyGameMode` 构造设 `bDelayedStart=true`、`bUseSeamlessTravel=true`；`ABrokenEdgePlayerState::CopyProperties()` 跨旅行保留 `ExtractionProfile / Raid / Squad`；`ABrokenEdgePlayerController::PreClientTravel()` 移除大厅 UI；`DefaultEngine.ini` 开 `net.AllowPIESeamlessTravel=1`。

**大厅状态复制**：`ALobbyGameState` 的 `RoomMatchType / ExtractionMapId / bModeUpdating / MinimumPlayers / Capacity / CountdownEndTime / bTravelling / LobbyStatus` 全部 `Replicated`，`ULobbyWidget::Refresh()` 由控制器的 `UpdateLobbyUI()` 每帧驱动，从 GameState + PlayerArray 重建名单与倒计时。`ALobbyShowcase` 是纯本地（`bReplicates=false`）的 3 人 3D 展示：3 个 SkeletalMesh + `SceneCaptureComponent2D` → 1600×880 RenderTarget，不生成 Pawn。

### 4.3 经典对战（FFA / Teams）

| 功能 | 实现 |
| --- | --- |
| 击杀计分 | `ABrokenEdgeGameMode::PlayerDefeated()` → 攻击者 `AddToScore(1)`、受害者 `AddToDefeats(1)`；`ABrokenEdgePlayerState` 的 `Defeats / Team` 是 `ReplicatedUsing`，`OnRep_Score` 覆盖后刷 HUD。 |
| 队伍 | `ATeamsGameMode::PostLogin()` 按红蓝人数均衡分配 `ETeam`，`Logout` 清理；`CalculateDamage()` 使同队伤害为 0（自伤保留）；`PlayerDefeated` 累加 `RedTeamScore / BlueTeamScore`（`ABrokenEdgeGameState` 的 `ReplicatedUsing`）。 |
| 重生 | 角色 `Defeated()` → `RespawnDelay` 定时器 → `ABrokenEdgeGameMode::RequestRespawn()`：销毁旧 Pawn，从 `APlayerStart` 随机选点 `RestartPlayerAtPlayerStart`。Extraction 覆写为倒地/观战，不重生。 |
| 淘汰播报 | GameMode 遍历所有控制器调 `ClientDefeatedAnnouncement(AttackerPS, DefeatedPS)`（Client RPC）→ `ABrokenEdgeHUD::AddDefeatedAnnouncement()` 创建 `UDefeatedAnnouncement`，上移旧消息，3 秒定时器移除。 |
| 时间同步 | `ServerRequestServerTime` / `ClientReportServerTime` 每 5 秒算 RTT/2，HUD 显示「延迟：xx ms」。 |
| **未完成** | `ABrokenEdgeGameMode` 有 `WarmupTime=10 / MatchTime=120 / CooldownTime=10` 字段但**无 Tick、无 SetMatchState/StartMatch**；`ServerCheckMatchState` 无调用点；`OnRep_MatchState` / `ClientJoinMidgame` 是 `//@TODO` 空函数；`MatchCountdownText` 全项目无赋值。也就是说经典模式没有真正的比赛倒计时与 Late Join。 |

---

## 5. 角色、输入与动画

| 功能 | 实现方式 |
| --- | --- |
| 输入 | `ABrokenEdgeCharacter::SetupPlayerInputComponent()` 用 `UEnhancedInputComponent::BindAction` 绑定 `IA_Jump / Move / Look / Crouch / Equip / Aim / Reload / Drop / Fire / Sprint`；`NotifyControllerChanged()` 里 `AddMappingContext(DefaultMappingContext, 0)`。另保留旧式字符串绑定 `HoldBreath`、`InteractDoor`。 |
| 移动 | `Move()` 按 ControlRotation 的 Yaw 拆前后/左右 `AddMovementInput`；移动本身交给 UE 的 `UCharacterMovementComponent` 客户端预测 + 服务器校验，没有逐帧 RPC。 |
| 蹲伏 / 趴下 / 冲刺 | `CrouchButtonPressed` 切 `Crouch/UnCrouch`；`ToggleProne` → `ServerSetProne`（Server Reliable），`bProne` Replicated，动态蒙太奇 StandToProne / ProneIdle / ProneToStand；`SprintPressed` → `ServerSetSprintRequested`，`UpdateSprint()` 每帧按朝向点积 >0.5 且非瞄准/蹲/趴/治疗/骨折判定，置 `bSprinting`（Replicated）、`MaxWalkSpeed=850`，搜打撤另扣 `Stamina`（`COND_OwnerOnly`）。 |
| 原地转向 / AimOffset | `CalculateAO_Pitch` / `AimOffset` / `TurnInPlace`（±90° 触发 `ETIP_Left/Right`，`FInterpTo` 回中）；模拟代理由 `OnRep_ReplicatedMovement → SimProxiesTurn()` 单独计算。 |
| ADS / FOV | `UCombatComponent::SetAiming` → `ServerSetAiming`，`bAiming`（RepNotify）；`InterpFOV` 用 `FInterpTo` 在 `DefaultFOV` 与 `ZoomedFOV(30)` / `FirstPersonAimFOV(65)` 间插值；`ABrokenEdgeHUD::DrawHUD` 画 `FHUDPackage` 动态准星与一阶镜 `ScopeOverlay`。 |
| 第一人称 | `InitializeFirstPersonView()` 建 `DedicatedFirstPersonMesh` 挂相机并隐藏头骨（无专用资产时用 `UPoseableMeshComponent` 复制姿态）；`UpdateFirstPersonWeapon()` 建 `FirstPersonWeaponMesh`（`SetOnlyOwnerSee`），按 `FStingerViewWeaponFit.Hip/Aim` 插值；`AWeapon::RefreshOwnerViewVisibility()` 隐藏世界枪；`SyncFirstPersonAction()` 同步 1P/3P 蒙太奇。**当前固定第一人称，V 键切视角已废弃。** |
| 动画 | `UBrokenEdgeAnimInstance::NativeUpdateAnimation` 算 Speed / InAir / Lean / YawOffset / AO，左手 FABRIK、右手 `FindLookAtRotation`；`UStingerFirstPersonAnimInstance` 用原生 `FStingerFirstPersonProxy`（SequencePlayer + Slot + TwoBoneIK）做 Idle/Aim/Sprint/Airborne/Knife/Reload/Equip 状态机。开火/换弹/切枪/受击/淘汰/技能各有 `Play*Montage()` 按武器类型跳 Section、按 `WeaponMechanics` 时长改 PlayRate。 |
| 手部 IK | `FAnimNode_WeaponGripReach`：在左手 FABRIK 前，12 次迭代把目标钳进双臂可达球，再用 `AnimationCore::SolveTwoBoneIK` 只移动右臂，保持枪的旋转。`FAnimNode_RifleLocomotion` 是可选（opt-in）步枪移动表现，`FAnimNode_TraversalContact` 叠加 `FExtractionMeleePose` 与 `FTraversalContactPose`（手脚抓杆 TwoBoneIK）。 |

---

## 6. 武器与战斗：两套系统如何共存

这是本项目最需要讲清楚的一处设计。**老系统**服务经典 FFA/Teams，**新系统**服务搜打撤，两者共用 `ABrokenEdgeCharacter`、`AWeapon`、`ReceiveDamage` 链，在 `AHitScanWeapon::Fire()` 与 `UCombatComponent::FireHitScanWeapon()` 两处分流。

### 6.1 老系统（`UCombatComponent`）

| 环节 | 实现 |
| --- | --- |
| 拾取装备 | 服务器 `AWeapon::AreaSphere` 重叠 → `SetOverlappingWeapon`（`COND_OwnerOnly` + RepNotify 显示 PickupWidget）→ `ServerEquipButtonPressed` → `EquipWeapon()`；主武器挂 `RightHandSocket`，副武器挂 `BackpackSocket`（`EWS_EquippedSecondary`）。 |
| 切枪 / 丢弃 | `SwapWeapons()` 置 `ECS_SwappingWeapon` + `PlaySwapMontage`，`FinishSwapAttachWeapons()` 交换两槽；`DropEquippedWeapon()` → `AWeapon::Dropped()`（Detach、物理/重力、自定义深度）。 |
| 换弹 | `ServerReload` → `CombatState=Reloading` → 蒙太奇 / AnimNotify → `FinishReloading` → `UpdateAmmoValues / AddAmmo / ClientAddAmmo`；霰弹走 `ShotgunReloadOnce` 逐发。`CarriedAmmoMap` 按 `EWeaponType` 存备用弹药（`COND_OwnerOnly`）。 |
| 开火 | `FireButtonPressed` → `CanFire()` 检查动作状态/空仓 → `FireTimer` + `FireDelay` 控制射速与连发 → 按 `EFireType` 分派 `FireHitScanWeapon / FireProjectileWeapon / FireShotgunWeapon` → `ServerFire`(Server, Reliable, WithValidation) → `MulticastFire` → `AWeapon::Fire(HitTarget)`。 |
| 命中扫描 | `AHitScanWeapon::WeaponTraceHit()` 从 `MuzzleFlash` 插槽 `LineTraceSingleByChannel(ECC_Visibility)`，末端外推 1.25 倍；命中后服务器 `UGameplayStatics::ApplyDamage(Damage=20 / HeadShotDamage=40)`。**固定伤害，无距离衰减**。 |
| 霰弹 | `AShotgun::ShotgunTraceEndWithScatter()`（10 颗、散射球 `DistanceToSphere=800 / SphereRadius=75`）→ `ServerShotgunFire` → `FireShotgun` 逐条射线汇总 `HitMap / HeadShotHitMap` → 一次 `ApplyDamage`。 |
| 投射物 | `AProjectileWeapon::Fire` 生成 `ProjectileClass`（`UProjectileMovementComponent`，`InitialSpeed=15000`）；`AProjectileRocket::OnHit` → `ExplodeDamage`（`ApplyRadialDamageWithFalloff`，内/外半径 200/500）。 |
| 伤害链 | `ApplyDamage` →（服务器 BeginPlay 绑定 `OnTakeAnyDamage`）`ABrokenEdgeCharacter::ReceiveDamage()` → `GameMode::CalculateDamage()` → 护盾优先吸收 → `Health=Clamp` → `UpdateHUDHealth/Shield` + `PlayHitReactMontage` → 归零时 `GameMode::PlayerDefeated()` / `ExtractionGameMode::DefeatCombatant()`。 |
| 增益 | `UBuffComponent::BuffHeal / ReplenishShield` 按 `DeltaTime` 渐增；`BuffJump / BuffSpeed` 用定时器 + `MulticastJumpBuff / MulticastSpeedBuff`（NetMulticast），到期恢复缓存的 `InitialJumpZVelocity / InitialBaseSpeed`。五类拾取物 `AAmmoPickup / AHealthPickup / AShieldPickup / ASpeedPickup / AJumpPickup` 都只在服务器绑重叠并生效。 |
| 延迟补偿 | `ULagCompensationComponent`：角色构造 19 个 `UBoxComponent` 命中盒（通道 `ECC_HitBox`），服务器每帧存 `FFramePackage`（`TDoubleLinkedList`，`MaxRecordTime=4s`），按 `HitTime` 找帧 + `InterpBetweenFrames` 插值，搬运碰撞盒后 `HitScanConfirmHit / ProjectileConfirmHit / ShotgunConfirmHit` 复核。三处真实接入：`AHitScanWeapon::Fire`、`AShotgun::FireShotgun`、`AProjectileBullet::OnHit`。**但**三个 `*_Implementation` 开头都 `if (GetWorld()->GetGameState<AExtractionGameState>()) return;`，搜打撤模式明确关闭 SSR。 |

### 6.2 新系统（搜打撤）：实体弹道 + 确定性射击

**参数来源**：`Content/Extraction/Data/WeaponMechanics.json` 的 `profiles`，由 `WeaponMechanics::Find(FName Definition)`（`WeaponMechanics.cpp:49`）解析成 `FResolvedWeaponStats`：初速 52500–63000 cm/s、`Gravity = GravityParam * 100 = 980 cm/s²`、`GravityFreeDistance=2500`、`MaxDistance=100000`、`RangeStart=4000`、分段 `FalloffDistances/Rates`、单发/连发/循环后坐力、基础/最大/每发/恢复散布、屏息与晃动等。源码注释明确这是**兼容性策略，不是官方公式**。

**分流点**（已核对 `HitScanWeapon.cpp:61-90`）：

```cpp
const auto* Mechanics = GetWorld()->GetGameState<AExtractionGameState>()
    ? WeaponMechanics::Find(ExtractionDefinition) : nullptr;
if (Mechanics && (HasAuthority() || OwnerPawn->IsLocallyControlled()))
{
    // 生成 AExtractionBullet；霰弹按 Pellets 用 VRandCone 一次射多颗
    ...
}
FHitResult FireHit;
if (!Mechanics) WeaponTraceHit(Start, HitTarget, FireHit);   // 老 hitscan 回退
```

上层另由 `UCombatComponent::FireHitScanWeapon()` 判 `ExtractionCombat->IsEnabled()`：启用则转 `UExtractionCombatComponent::RequestShot` → `ServerRequestShotVersioned`（校验 ShotId 去重、武器归属、装配 Revision、射速、弹药、方向点积 >0.7、距离）→ `Weapon->UseImmediateServerHitScan()`（把服务器 `bUseServerSideRewind` 置 false）→ `ResolveShot()` → `Weapon->Fire(End)` → `MulticastShotEffects`。

**`AExtractionBullet::Tick()`**（`ExtractionBullet.cpp:71-132`，已核对）：固定步长 `Step=1/120` 的 `Accumulator` 累积飞行时间，`SweepSingleByChannel(ECC_Visibility, MakeSphere(Stats.Radius))` 扫掠；`Distance >= GravityFreeDistance` 后 `End = Start + Velocity*Step - 0.5*g*Step²`、`Velocity.Z -= g*Step`；`TryPenetrate()` 对非 Pawn 静态物体按厚度 ≤ `4cm × 弹药穿透等级` 做反向射线探背面，每发最多一次，`PenetrationScale` 从 0.75 线性降到 0.35 并衰减速度；命中角色时 `(HeadDamage / BodyDamage) * Stats.Falloff(Distance) * PenetrationScale` 调 `UExtractionHealth::Bullet`；超 `MaxDistance` 销毁；`Impact` 为 NetMulticast 特效。服务器权威飞行，客户端可用相同规则做 `bPredicted` 预测。

**后坐力与散布**：`UExtractionCombatComponent::ResolveShot()` 用共享种子 `MechanicsSeed`（`COND_OwnerOnly`）+ `ShotId` 生成确定性散布，叠加移动/蹲伏/跳跃修正、Sway，`Stats.Recoil()` 写 ControlRotation 并用 `RecoilDebt / CompensatedDebt` 偿还；`TickComponent` 推 `AimAlpha / Breath / SpreadAccumulation / VisualKick`。服务器用同种子复算，避免客户端说了算。

**搜打撤伤害**：`UExtractionHealth::Bullet()` 应用弹药倍率 → `ArmorSlotForBone()` 定位护甲 → `Absorb()` 按穿透/等级扣物品 `Durability` → 掉血后按概率产生 `Bleeding`（每 0.1s 扣血）/`LegFracture` 创伤 → `Treat()`（医疗包/绷带/夹板）与 `ApplyStim()`（激素回血止痛）。守卫射手额外乘 `GuardDamageScale=0.15`，玩家拾取守卫武器不继承降伤。

**其他战斗能力**：`UExtractionMeleeComponent`（球体扫掠近战，刀槽不持久化）、`AExtractionFragGrenade`（引信 + 半径衰减 + `Exposed` 遮挡射线）、`AExtractionSmokeGrenade` / `AExtractionSmoke`（半径 550 / 15s / 16 层云，`Blocks()` 线段遮挡供 AI 视线判定）、`AExtractionStimDart`、`UExtractionAbilities`（救援 / 治疗 / 烟雾，服务器计时完成 + 救援锁）。换弹与切枪在新系统里改成**服务器定时器**（`BeginServerReload / BeginServerSwap`），不依赖 AnimNotify，避免动画通知丢失卡死状态。

---

## 7. 搜打撤战局：`AExtractionGameMode`

### 7.1 阶段状态机（已核对 `ExtractionGameMode.cpp:616-729`）

`EExtractionPhase { Waiting, Active, Ended }` 与 `EExtractionLife { Alive, Dead, Left, Extracted, TimedOut, Downed }` 定义在 `ExtractionState.h`，`ExtractionState.cpp` 只做 `GetLifetimeReplicatedProps`（只读复制读模型），**规则全在 `AExtractionGameMode::Tick`**：

```text
Waiting
  ├ StartWaitingPlayers() 逐个校验票据、解冻并分配出生点
  ├ 条件：Ready >= ExpectedPlayers 且 AExtractionRegionStream::IsReady()
  ├ 校验所有玩家来自同一份未关闭的 Raid 记录、档案不重复、人数一致
  ├ Persistence->ActivateRaid(Raid)
  └ → Phase=Active, Exit->StartRound(), Deadline = Now + RoundSeconds(1800),
       StartMatch(), SpawnGuards(), FreezePlayer(false)
  └ 加载超 120s → FinishRound("玩家加载超时")

Active（每帧）
  ├ UpdateDowned(Now)            倒地流血超时 → 最终死亡
  ├ AlivePlayers == 0           → FinishRound("无人存活")
  ├ Now >= Deadline             → FinishRound("本轮时间结束")
  └ UpdateExtraction(Now)        区域停留、丢包检查、SettlePlayer → Exit->Consume()

Ended
  ├ FinishRound() 结算所有存活/倒地成员为 TimedOut，RemoveGuards()，
  │ Phase=Ended, EndReason, ReturnAt = Now + 10, 全员冻结
  └ 10 秒后 EndSession() → SessionEnded() → ServerTravel("/Game/Maps/Lobby?MaxPlayers=N")
```

`PreLogin` 只在 Waiting 准入，`FreezePlayer()` 停移动/射击/交互；`bTravelRequested / bEndRequested` 防止重复跳图，会话结束失败时延后 2 秒重试。

**票据机制是大厅与战局之间的契约**：`ALobbyGameMode::OnStartSession` 调 `Store->BeginRaid(Profiles, Raid)` 保存 `Original` 物品快照并写 `PS->ExtractionRaid`；战局 Tick 逐项核对票据有效性，注释写明「A prepared lobby ticket is the only valid way to enter an active raid」。硬跳图丢失 PlayerState 时，服务器存档仍持有记录，可 `AbortPreparedRaids()` 回滚。

### 7.2 出生、撤离与结算

| 环节 | 实现 |
| --- | --- |
| 出生 | `ChoosePlayerStart_Implementation()` 按 `PS->ExtractionSquad` 分配 `UExtractionMapDefinition::SpawnGroups`（要求 ≥2 组、每组 ≥3 点），地面 `LineTraceSingleByChannel` + 胶囊 `OverlapBlockingTestByChannel` 校验，按 `ExtractionSpawnSeed` 随机；失败回退 authored 组。`HandleStartingNewPlayer_Implementation()` 调 `Inventory->InitializeLoadout()` 从票据恢复配装。 |
| 撤离点 | `FExtractionExitDefinition`（`Id / Label / Transform / HalfExtent / HoldSeconds / OpensAfterSeconds / ClosesAfterSeconds / Capacity / bRequireEmptyPack / SwitchLocations / SwitchesRequired / SwitchOpenSeconds`）。`AExtractionExit::StartRound()` 每局洗牌随机启用指定数量电闸；`Contains()` 用**局部坐标盒判定**（不是碰撞重叠）；`UnavailableReason()` 统一判断名额 / 开放 / 关闭 / 电闸；`Consume()` 扣名额。`UpdateExtraction()` 每帧检测区域，写 `PS->ExtractAt = Now + HoldSeconds`，先 `SettlePlayer` 再清空背包并转入观战。 |
| 电闸 | `AExtractionExitSwitch::Pull()` 三选一联动 `Exit->SwitchPulled()` / `Bridge->Lower()` / `Water->StartFill()`，`bActive / bPulled` 复制，动态材质改灯色。 |
| 结算 | `SettlePlayer()` → `CaptureWeapons()` + 汇总钥匙房 `FileLots` → `UExtractionPersistence::Settle()`：**仅 `Extracted` 全额入库并累计任务**；`Dead / Left / Disconnected / TimedOut` 只保留 `ExtractionItems::Protected()`（安全箱本体及箱内物品），其余损失；装不回装备槽的进 `Overflow`。`DropAll()` 死亡时生成地面容器。 |
| 倒地 / 观战 | `DefeatCombatant()`：同队仍有存活者 → `Downed`（`DownHealth + BleedSeconds`），`UpdateDowned` 超时或无救援者 → 最终死亡；`RequestRespawn` 覆写为不生成新 Pawn。`AExtractionPlayerController::BeginExtractionSpectating()`：`UnPossess` + `NAME_Spectating` + 只选同小队存活者，`ServerCycleSpectator` 只收 ±1；HUD 的「离开」→ `ServerLeaveExtraction` → `LeavePlayer` → 销毁本地会话 → `OpenLevel("/Game/Maps/Lobby")`。 |

---

## 8. 物品、背包、经济与持久化

### 8.1 物品模型（`Extraction/ExtractionItems.h/.cpp`）

| 概念 | 实现 |
| --- | --- |
| 装备槽与网格 | `ExtractionGear` 常量：`RigSlot=4 / BagSlot=5 / SafeSlot=6`，对应 `RigGrid=-2 / BagGrid=-1 / SafeGrid=-3`。`FGrid::Pockets`（`TArray<FIntRect>` 分仓），`PocketLayout()` 硬编码 4 种胸挂、4 种背包的口袋形状。已确认 4 种胸挂（6/10/16/22 格）、4 种背包（16/24/28/45 格）、3 种安全箱（4/6/8 格）。 |
| 容器 | `FExtractionContainer { Id, Revision, Width, Height, WeaponSlots, GearGrids, TArray<FExtractionItem> Items }`，提供 `Grids / GridOf / Fits / AutoPlace / Sort / Validate / Transfer / AttachGear / DetachGear`。**所有操作在副本上执行、校验通过才 `++Revision` 提交，失败不改动原容器**——`Revision` 兼作并发条件。 |
| 物品实例 | `FExtractionItem { FGuid Id, Definition, Quantity, Cell, Size, Rotated, Slot, Magazine, MagazineAmmo, Durability, Attachments, TuneValues, Origins, MagazineOrigins }`。`Make()` 用 `FGuid::NewGuid()` 生成本体/附件 ID。 |
| **唯一身份与溯源** | `FExtractionOrigin { FGuid Lot; int32 Quantity }`（`NotReplicated`，仅服务器）。`SeedOrigins()` 给本体/弹匣/附件各生成 Lot；`TakeOrigins / MergeOrigins` 在堆叠、拆分、装填、射击、掉落、结算时守恒；`Transfer` 部分拆分换新 GUID；`Recover` 部分返还换新 GUID 防冲突；`BeginRaid` 重新播种，`Settle` 汇总 `Raid.Exported` 批次防重复返还。`UExtractionItemDefinition::Ids() / Get()` 按 `/Game/Extraction/Items/DA_Item_%s` 白名单加载（当前 38 个可识别定义，另有 320 种收集品 ID）。`ValidateItem(Provenance=true)` 在持久化边界校验 Lot 唯一、数量守恒、附件父链/环/冲突/槽位/精调范围。 |
| 物品定义白名单 | `UExtractionItemDefinition::Ids()` 由源码硬编码，脚本 `Scripts/Extraction/configure_gear.py`、`generate_collectibles.py` 负责生成对应资产。 |

### 8.2 背包与搜刮事务

- `UExtractionInventoryComponent`：`Contents` 走 `COND_OwnerOnly`，`OnRep_Contents` 只刷备弹；`InitializeLoadout()` 从票据 `Original` 恢复；`SyncWeapons()` 按 `ExtractionItemId` 复用/生成 `AWeapon` 并同步 `ExtractionAssembly / Parts / Configuration`；`CaptureWeapons()` 在结算时把武器状态写回物品；`DropAll()` 排除 `Protected()`；`InitializeGuard()` 给守卫配装。
- `UExtractionInteractionComponent`：本地 `UpdateFocus()` 用 `TActorIterator` 按朝向点积 + 距离评分选焦点（战利品 / 普通门 / 钥匙门 / 电闸 / 滑索）；`CanReach()` = 200cm + `LineTraceSingleByChannel(ECC_Visibility)`；钥匙门用 `InteractionZone` 重叠盒；`Revealed` 是 `TMap<FGuid, TSet<FGuid>>`，**逐件搜索**（每件 1 秒），未揭示物品在发给客户端的 `LootView` 快照里清空 `Definition`（脱敏）；`Session / Sequence / InventoryRevision / LootRevision` 防重放。
- `AExtractionPlayerController` 绑定 E/F/G/Alt+G/V/中键/Tab/M/Esc，含 `ServerItemOperation / ServerOpenInventory / ServerUnlockDoor / ServerToggleDoor / ServerPullSwitch / ServerUseZipline / ServerThrowGrenade / ServerPing(Unreliable)`，以及 `ClientItemResult / ClientPing / ClientDeathRecap / ClientSquadNotice`。所有变更都是「客户端发请求 → 服务器校验事务 → 复制结果」。
- `ULootingMainView`（UMG 蓝图提供控件树、C++ 提供数据）：`Refresh()` 比较 `Id / Revision / 已揭示件数` 增量重建，`QuickTransfer / SortOwn / TakeAllLoot / MenuAction`、`ULootingDragDropOperation::ResolveDrop`。
- 配装改枪：`ExtractionEquipment.cpp::EquipmentActions()` 生成 `Transfer / Attach / Detach / Tune / Unload / SelectAmmo` 候选请求，`Configure()` 在副本上预演并 `Validate()` 后提交；`UExtractionGunsmith::Open()` 是**纯 Slate** 面板，预演前后 `WeaponDetails` 对比；`UExtractionWeaponVisuals` 按 `ExtractionAssembly` 变化重建世界/第一人称附件网格（纯表现）。

### 8.3 账号、仓库与持久化（重点核对）

**不是 USaveGame、不是数据库**，而是自定义的**追加式 JSON 日志**（`ExtractionPersistence.cpp`，已核对）：

```text
路径：FPaths::ProjectSavedDir() / "Extraction/Persistence/Store.journal"
      （命令行 -ExtractionStore= 可覆盖；训练地图改用 Saved/Extraction/Training/<GUID>/ 隔离目录）
格式：每条 = uint32 Header[4] { Magic=0x34455842, Ver=1, Length, CRC32 } + UTF-8 JSON
内容：完整 FExtractionStoreState 快照（Version=6、Sequence 递增、Profiles / Raids）
写入：OpenWrite(append) + Flush(true) 成功后才替换内存 State；失败置 Faulted 熔断
读取：逐帧校验 Magic / 长度 / CRC / Sequence / 内容合法性，尾部残包截断；
      旧版本先备份 .vN.bak，再迁移 v3 → v4 → v5 → v6
```

`UExtractionPersistence` 是 `UGameInstanceSubsystem`，**跨关卡无缝旅行仍存活**，所以大厅 → 战局 → 回大厅是同一份状态；跨进程重启靠日志重放 + `Recover()`。客户端只有凭据文件 `Saved/Extraction/Clients/<Label>/<StoreId>.credential`（两行：ProfileId + 64 字符凭据，SHA1 校验），`UExtractionAccountComponent` 用 `ClientBind / ServerBind / ClientCredentials / ServerCredentialSaved` 握手，`Profile / Bound / Locked / Message` 走 `COND_OwnerOnly`。**档案不能跨服务器 / 跨设备迁移**。

- `UExtractionStashWidget`（纯 Slate）→ `RequestMove` → `ServerMove` → `Persistence::Move()`（候选副本、Operation 幂等，Overflow 只能先领回 Stash）。
- `UExtractionProgression`：`FExtractionProgression { Credits, Experience, 3 × FExtractionQuestState }`，`Execute()` 支持 Buy / Sell / AcceptQuest / DeliverQuest / ClaimQuest / SelectSafe，用 `Signature()` + `FExtractionEconomyReceipt` 做幂等；`Extract()` **只在成功撤离时**累计 `GuardKills / EligibleFiles`（文件任务要求本人带出的 `Document` 批次）。
- `AbortPreparedRaids()` / `Recover()` 处理崩溃或取消：从 `Original` 扣掉 `Exported` 后返还剩余，标记 `Recovered / Aborted`。

---

## 9. 守卫 AI

**C++ 提供控制器、Service、Task 与规则；行为树 `BT_ExtractionGuard` / 黑板 `BB_ExtractionGuard` 本体是编辑器资产**（C++ 硬引用 `/Game/Blueprints/Ai/BT_ExtractionGuard`，不保证连线内容）。

| 能力 | 实现 |
| --- | --- |
| 启动 | `AExtractionGuard::OnPossess / StartGuardAI()`：`SetTimerForNextTick` 等一帧，置 `bExtractionGuard`、血量 100、`GuardInventory->InitializeGuard()`、走速 525，`RunBehaviorTree()`；`StopGuardAI()` 用 `StopTree(Forced)`。 |
| 感知 | 引擎 `UAIPerceptionComponent` + `UAISenseConfig_Sight`（半视角 60°、半径读 `UExtractionRules`）。`BTService_UpdateGuardContext::TickNode()`（0.1s）用 `GetCurrentlyPerceivedActors(UAISense_Sight)` 过滤守卫/死亡/非玩家，再 `LineOfSightTo` + `AExtractionSmoke::Blocks()` 复核，按「未倒地优先 → 最近」写黑板 `TargetActor`。 |
| 反应 / 追击 / 搜索 | 写 `ReactionReadyTime = Now + 2s`、`LastSeenTime`、`LastKnownLocation`、`SearchActive`；丢失目标时 `BuildPursuitDestination()` 按缓存速度预判 → `ProjectPointToNavigation` → 完整路径 + 8000cm 预算；搜索窗口 12 秒后 `ReturningToRoute`。 |
| 巡逻 | `BTTask_SetPatrolDestination` 随机模式从 `AExtractionPatrolArea` 盒取 NavMesh 可达点并 `FindPathToLocationSynchronously` 验证完整路径，关闭时用 `AExtractionPatrolRoute::Points[PatrolIndex]`，移动由内置 Move To 执行；`BTTask_CompletePatrolArrival` 按 110cm 半径确认到点。 |
| 交战 | `BTTask_EngageTarget::UpdateEngagement / UpdateMovement`：`SetFocus`，`FindTacticalPoint()` 16 次采样侧向点 + 眼/胸 `LineTrace` 遮挡 + 完整路径实现掩体与横移（换弹/中弹触发，`CoverUntil=2s`、冷却 6s）；过反应时间与 `NextFireTime` 后 `Combat->FireGuard(Aim)`，空弹匣 `BeginServerReload`，实际间隔 `max(FireDelay, GuardShotInterval=0.15)`。守卫瞄准额外 `ExtractionGuardAim::ApplySpread`（`VRandCone`，种子由 `MechanicsSeed/ShotId` 混合，额外 3°）。 |
| 听觉 | 自定义而非引擎听觉：`HearShot()` 由 `UExtractionCombatComponent` 在玩家开枪时调用，`TActorIterator<AExtractionGuard>` 半径内写 `LastKnownLocation / SearchActive` 进入搜索；消音配件缩小半径。 |
| 生成 | `AExtractionGameMode::SpawnGuards / RemoveGuards`（实现在 `ExtractionGuard.cpp` 底部）：默认/上限 8 名，命令行 `-ExtractionGuards=` 覆盖，按巡逻路线名排序 + NavMesh 投影，`SpawnActorDeferred<ABrokenEdgeCharacter>` + `Possess` + `StartGuardAI()`；`InitializeGuard()` 给 AK/M4A1/SCARH/霰弹/栓狙等配装（`DefaultGame.ini` 的 `+GuardWeapons=`）。 |
| 受击死亡 | `UExtractionHealth` 记 `LastBulletAt`（触发找掩体），`ABrokenEdgeCharacter::Defeated()` → `Guard->StopGuardAI()`，`MulticastDefeated` 播倒地蒙太奇。 |

**黑板键**（`Character/GuardBlackboardKeys.h`）：`CanRunAi / LastKnownLocation / HasLastKnownLocation / LastSeenTime / SearchActive / ReactionReadyTime / NextFireTime / PatrolLocation / ReturningToRoute / TargetActor`。

---

## 10. 场景交互与遍历

**关键事实**：场景对象**全部是 C++ `AActor`**，由 `Scripts/Extraction/place_*.py` 通过 `UnrealEditor-Cmd -run=pythonscript` 的 `spawn_actor_from_class` 摆进**静态 `.umap`**；地图不是运行时程序化生成几何。运行时只从 DataAsset 读配置生成**玩法 Actor**：守卫、战利品容器、撤离点/电闸，以及每局随机变体。

| 对象 | 实现要点 |
| --- | --- |
| 门 | `AExtractionDoor::Toggle()`：`CanReach` 220cm + 视线、`SweepOccupied()` 16 步 `OverlapAnyTestByObjectType` 防夹、`bOpen / MotionStartedAt` 复制、`ApplyMotion()` 按 `GameState->GetServerWorldTimeSeconds()` 插值 Yaw，到点 `FNavigationSystem::UpdateComponentData`。 |
| 钥匙房 | `AExtractionKeyRoom::Unlock()` 校验距离（`InteractionZone` 重叠盒）/视线/背包 Revision，消耗钥匙卡耐久，0.8 秒后 `OnRep_Door` 撤 Barrier 碰撞并滑门；`BeginPlay` 服务器 `SpawnContainer` 生成机密箱，`FileLots` 不复制。 |
| 吊桥 / 断桥 | `AExtractionDrawbridge::Lower()`：`LoweredAt` 复制，6 秒内两片 Deck 由 Roll ±70° 放平；`AExtractionBrokenBridge::BeginPlay()` 按 `Variants` 位掩码每局随机断法（`BrokenSpan` 复制），ISM 实例缩小下沉、生成坍塌板。 |
| 水域 | `AExtractionWater` 继承 `APhysicsVolume`（`bWaterVolume`、`FluidFriction=0.3`），`StartFill()` 后抬水面并 `UpdatePhysicsVolume(true)`；浮力/游泳靠引擎。**源码中没有减速或伤害逻辑**。 |
| 滑索 / 电闸 / 烟雾 | `AExtractionZipline` 只管外观和起止点，`Use()` → `Character->StartZipline()`；电闸见 §7.2；`AExtractionSmoke::Blocks()` 按线段到烟心距离判定遮挡，无碰撞、不影响导航。 |
| 物资 | `UExtractionLootLayout::Select()`：`FRandomStream(Seed)`，空 Group 的固定点全选、命名组按 `Weight` 选一，排序保证可复现；`Spawn()` 每点独立随机流 `Seed ^ Hash(Id)`，配方 `RollPool` 按品质权重 `{40,30,16,9,4,1}` 抽收集品。`AExtractionLootActor` **只复制外观 / Loose / Label / Empty / ContainerId / SourcePointId / bOpened / AccessDoor**，`Contents` 留服务器。 |
| 地图数据 | `UExtractionMapDefinition`（DataAsset，纯配置）含 `Level / GuardCount / FloorBounds / OverviewTexture / SpawnGroups / Exits / LootLayout`；`UExtractionMapRegistry`（`UGameInstanceSubsystem`，`Config=Game` 的 `Maps`）提供 `Find / FindWorld / Validate`，命令行 `-ExtractionMap=` 覆盖。当前注册 `DA_SpaceCenterGameplay` 与 `DA_SovietMilitaryGameplay`。 |
| 区域流式加载 | `AExtractionRegionStream::Tick()` 按玩家 / 观战目标 / 相机到 `RegionMin/Max` AABB 的距离做 `PreloadDistance=3000 / UnloadDistance=5000 / UnloadDelay=5` 滞回，调 `ULevelStreaming::SetShouldBeLoaded/Visible`；`ClientReady()` 查 `NetConnection->ClientVisibleLevelNames`；`ProtectBoundary()` 把未就绪的 Pawn 传回 `SafePositions` 并停移动。`IsReady()` 是开局门槛。 |
| 攀越 / 梯子 | `UTraversalMovementComponent::TryObstacle()`：前方 `Reach=105` 射线取法线、顶部射线定高、`VaultHeight=120 / MantleHeight=180`、5 点落点检查；`StartPath()` 三段胶囊扫掠 + `SetMovementMode(MOVE_Custom,1)`，`PhysCustom` 分段插值。`FindLadderEntry()` 用胶囊-盒判定 + 朝向点积，`LadderEnter → Ladder → LadderExit`（W/S × `LadderSpeed=150`）。网络用 `FTraversalSavedMove` 的 `FLAG_Custom_0..3` 传标志位，`FTraversalMoveResponse::Serialize` 校正时序列化 `FExtractionTraversalState`；`UExtractionTraversalComponent` 只向 SimulatedProxy 复制 `VisualState`。 |
| 战术地图 | `SExtractionTacticalMap::OnPaint`（纯 Slate）读复制的 `GameState->MapDefinition` + `OverviewTexture`，投影绘制撤离点状态、电闸、小队标点、同队位置；只读，不驱动判定。`ExtractionWidget::UpdateMinimap()` 从同一俯视图截局部。 |

---

## 11. UI 与 HUD

| UI | 技术 | 数据来源 |
| --- | --- | --- |
| 经典 HUD | `ABrokenEdgeHUD::DrawHUD`（准星 `FHUDPackage` 5 张纹理 + 扩散、ScopeOverlay）+ `UCharacterOverlay`（UMG `BindWidget`） | `SetHUDHealth/Shield/WeaponAmmo/CarriedAmmo/Score/Defeats/RedBlueTeamScore`；生命护盾来自角色 `OnRep`，弹药来自 `CombatComponent`，分数来自 `PlayerState / GameState`，延迟来自时间同步 RPC。`MatchCountdownText` 无赋值。 |
| 大厅 | `ULobbyWidget`（C++ 动态构建整棵 WidgetTree）+ `ALobbyShowcase`（本地 3D 人像） | `ALobbyGameState` 与 `PlayerArray` 中各 `ABrokenEdgePlayerState`；由控制器 Tick 每帧 `UpdateLobbyUI()` |
| 搜打撤 HUD | `UExtractionWidget`（`Abstract, Blueprintable`，控件树由 `WBP_ExtractionHUD` 提供） | `Refresh()` 读 GameState / PlayerState / Account，`NativePaint` 画命中标记、受击方向弧、小地图 |
| 搜刮 | `ULootingMainView`（`WBP_LootingPickupView_Pc` 控件树） | 服务器下发的脱敏 `LootView` 快照 |
| 仓库 / 进度 / 改枪 / 战术地图 | **纯 Slate**（`SExtractionStashPanel` / `SExtractionProgressionPanel` / `SGunsmith` / `SExtractionTacticalMap`） | `Account()->Profile` 只读 + `ServerMove / ServerEconomy / ServerItemOperation` 写 |
| 视觉规范 | `ExtractionUIStyle.h`（inline）：1280×720 设计缩放、6 级品质色、Roboto 字体、圆角笔刷、图标软引用缓存 | — |

> 注意：`ExtractionUIStyle.h` 与 `ExtractionWidget.h` 在 `Extraction/` 目录，不在 `UI/`。

---

## 12. 编辑器工具链与自动化

`StingerTools` 是 `UncookedOnly` 模块，只在 `BrokenEdgeEditor.Target.cs` 的 `ExtraModuleNames` 里，Game/Server 目标不包含；依赖 `UnrealEd / UMGEditor / BlueprintGraph / AnimGraph / AssetRegistry / WidgetRenderer` 等。

| 能力 | 实现 |
| --- | --- |
| 自定义动画节点包装 | 运行时 `FAnimNode_*` 是 `BlueprintInternalUseOnly` 的 USTRUCT，编辑器图表里无法手动放置。三个 `UAnimGraphNode_*`（`WeaponGripReach / RifleLocomotion / TraversalContact`）把运行时结构体暴露成可编辑节点，使 `FGraphNodeCreator` 能自动插节点、改引脚、编译动画蓝图（`StingerAssetLibrary.cpp` 的 `ConfigureRifleLocomotion / ConfigureThirdPersonGripIK / ConfigureTraversalSlot`）。 |
| 自动生成 Widget / 蓝图 | `UExtractionBlueprintBuilder::CreateExtractionBlueprints()` 用 C++ 构造 WidgetTree 生成 `WBP_ExtractionHUD`、`BP_ExtractionLootActor`；`UInventoryWidgetBuilder::CreateInventoryWidgets()` 生成 10 个搜刮 WBP（含 `WBP_LootingPickupView_Pc`）。 |
| 资产库 | `UStingerAssetLibrary`：创建 Skeleton、TTF→UFont、填充 BlendSpace、蒙太奇/Socket/骨骼重映射、`RenderWeaponIcon`（PNG）、`RenderLevelOverview`（高度图 + 山体阴影）、动画图/碰撞诊断 JSON、PIE 验证（注入 Enhanced Input、固定步长、状态快照）。 |
| 关卡自动化 | `USovietMilitaryBuilder`：`RepairWindows / BuildNavigation / RefreshCollision / ConfigureStairCollision / ConfigureDoorCollision / ConfigureBackdrop`；`Scripts/Extraction/place_*.py` 摆放 Actor，`prepare_expansion_map.py` 建 DataAsset，`bake_overview_map.py` 烘焙俯视图。 |
| 编辑器测试 | `AExtractionBlueprintTestMode`（绕过 raid ticket 的测试 GameMode）、`ExtractionPIELobbyTests.cpp` / `ExtractionPIETeardownTests.cpp` 的 latent automation tests。 |
| 验收脚本 | `Scripts/Extraction/run_build.py`（Editor/Client/Server、打包、Cook、断点续跑）、`pe_validation.py`（校验 PE 头防把中断链接当成功）、`run_network.py`、`run_pie_lobby.py`、`run_storage_restarts.py`、`run_phase*_acceptance.py`（groups + `--tag` 隔离输出 `Saved/Extraction/<tag>/Report.json`）、`check_runtime_cook_coverage.py`、`Scripts/Test-LanSessions.ps1`（1 专服 + 2 客户端跑 `MultiplayerSessions.LAN.MenuFlow`）。 |

---

## 13. 配置、构建与启动

| 项 | 实际值 / 位置 |
| --- | --- |
| 默认地图 | `DefaultEngine.ini`：`GameDefaultMap = /Game/Maps/Lobby`、`ServerDefaultMap = Lobby`、`EditorStartupMap = /Game/Maps/MainMap` |
| OSS | `[OnlineSubsystem] DefaultPlatformService=Null`；`net.AllowPIESeamlessTravel=1` |
| GameMode | `DefaultEngine.ini` **没有 `GlobalDefaultGameMode`**，依赖各地图 WorldSettings；`DefaultGame.ini` 只配了 `[/Script/BrokenEdge.ExtractionGameMode] MapDefinition=/Game/Extraction/Maps/DA_SpaceCenterGameplay` |
| 专服参数 | `DefaultGame.ini` `[MultiplayerSessions.DedicatedServer]`：`8 / 2 / Teams / True` |
| 规则热更 | `DefaultGame.ini` 的 `[/Script/BrokenEdge.ExtractionRules]`（守卫、烟雾）、`ExtractionTraversalRules`（Vault 120 / Mantle 180 / Reach 105 / Ladder 150）、`ExtractionMapRegistry`（2 张地图）、`ProjectPackagingSettings` 的 AlwaysCook / AlwaysStage 清单 |
| 碰撞通道 | `GameTraceChannel1 = "SkeletalMesh"`（Block）、`GameTraceChannel2 = "HitBox"`（Ignore） |
| 渲染 | DX12 SM6 / Lumen / VSM / 光追 / SkinCache，关静态光照与 MotionBlur |
| 输入 | EnhancedInput 类为默认；`DefaultInput.ini` 的传统 Action/Axis 映射属遗留兼容 |
| 模块依赖 | `BrokenEdge.Build.cs`：Public = Core/CoreUObject/Engine/InputCore/EnhancedInput/Niagara/UMG/Slate/SlateCore/MultiplayerSessions/OnlineSubsystem；Private = OnlineSubsystemUtils/AnimGraphRuntime/AnimationCore/Json/JsonUtilities/AIModule/NavigationSystem/GameplayTasks（Editor 另加 UnrealEd） |
| 打包 | `PackageClient.bat` / `PackageServer.bat` 走 `RunUAT BuildCookRun`，地图清单 `Lobby+FreeForAll+Teams+L_SpaceCenterExtraction`，输出 `Packaged/Client/Windows` 与 `Packaged/Server/WindowsServer`，复制 `Deployment/StartClient.bat` / `StartServer.bat` |
| 启动 | Server：`BrokenEdgeServer.exe /Game/Maps/Lobby -log -port=7777 -MatchType=FreeForAll -MinPlayersToStart=2 [-MULTIHOME=<IPv4>] -unattended`；Client：`BrokenEdge.exe /Game/Maps/Lobby -MatchType=<模式> -windowed -log`，或直接把 `IP:端口` 作为 travel URL 直连 |

> 搜打撤测试需要给专服传 `-MatchType=Extraction`。打包脚本、验收脚本、LAN 测试脚本都硬编码了 `C:\UnrealProjects\UnrealEngine-5.8.3-release`，换机器要逐个改。

---

## 14. 网络同步模型速查

| 场景 | 机制 |
| --- | --- |
| 移动、蹲伏、攀越、梯子 | `UCharacterMovementComponent` / `UTraversalMovementComponent` 客户端预测 + 服务器校正 + SimulatedProxy 表现 |
| 输入 → 权威动作 | `Server, Reliable` RPC（开火、装备、瞄准、切枪换弹、准备、模式、物品操作、拉闸、交互） |
| 状态广播 | `Replicated`（阶段、生命状态、容量、开关状态）+ `ReplicatedUsing`（Health/Shield、Score/Defeats、Ammo、Contents、bOpen） |
| 只给拥有者 | `COND_OwnerOnly`：`OverlappingWeapon / CarriedAmmo / Stamina / MechanicsSeed / Contents / Profile / 撤离个人字段` |
| 只给模拟端 | `DOREPLIFETIME_CONDITION(..., COND_SimulatedOnly)`：`UExtractionTraversalComponent::VisualState` |
| 只初始化一次 | `COND_InitialOnly`：`ExtractionDefinition / ExtractionInitialAmmo` |
| 表现 | `NetMulticast`：开火特效、爆炸、烟雾、换弹、受击/倒地蒙太奇、Buff |
| 定向结果 | `Client` RPC：淘汰播报、时间同步、射击结果、死亡回顾、小队通知、跳图前清 UI |
| 时间 | `ServerRequestServerTime / ClientReportServerTime` 每 5 秒同步，倒计时统一用 `GameState->GetServerWorldTimeSeconds()` 插值 |
| 物品 | 容器 `Contents` **不复制**；只给客户端「脱敏快照 + Revision」，写操作必须由服务器事务校验后提交 |
| 跨关卡 | `UGameInstanceSubsystem`（Persistence / MapRegistry / Sessions）+ `PlayerState::CopyProperties` 保留票据 |
| 防作弊 | 射击请求版本化校验、背包 Revision 条件提交、Origin/Lot 溯源守恒、服务端重算散布种子、写日志 CRC + Sequence |

---

## 15. 限制与未完成清单（按影响排序）

1. **经典对战没有真正的比赛流程**：`ABrokenEdgeGameMode` 无 Tick / 无 `SetMatchState`，Warmup/Match/Cooldown 字段从未推进，`OnRep_MatchState`、`ClientJoinMidgame` 是空 TODO，`ServerCheckMatchState` 无调用点，`MatchCountdownText` 永不更新。搜打撤有完整状态机，两者不要混为一谈。
2. **联机后端只有 Null / LAN**：`DefaultPlatformService=Null`，未接 Steam 或公网服务器目录 / 匹配；`OnlineSubsystemSteam` 在 Build.cs 里仍是注释状态。
3. **Extraction 房间不能从房间列表加入**：`Menu.cpp:199` 只允许 FFA/Teams，必须走大厅准备流程或已有会话。
4. **SSR 只服务老路径**：搜打撤明确关闭三类 `ServerScoreRequest`，改由服务器权威实体子弹判定；且 `AWeapon::bUseServerSideRewind` 默认 false，C++ 里只找到把它置 false 的代码，是否曾启用取决于蓝图配置。
5. **队伍 roster 不完整**：`TeamsGameMode` 的 `RedTeam / BlueTeam` 数组未复制，`OnRep_Team()` 为空；`ABrokenEdgeGameState` 的队伍分 HUD 写入用 `GetFirstPlayerController()`，专服多客户端下指向未必正确，且存在服务端 + OnRep 双重写入。
6. **持久化是服务器本地 JSON 日志**，不是账号服务：档案不跨服务器/设备，`Recover()` 依赖 `Exported` 批次守恒；客户端只有凭据文件。
7. **二进制资产未验证**：`BT_ExtractionGuard` / `BB_ExtractionGuard`、`WBP_ExtractionHUD`、`WBP_LootingPickupView_Pc`、`DA_Item_*`、`DA_SpaceCenterGameplay`、蒙太奇与武器资源只确认被 C++ 引用，未在编辑器中打开确认连线与 Class Defaults。行为树决策、控件树、动画资源属于蓝图侧。
8. **与官方玩法仍有差距**（仓库文档已明确）：守卫只有同类 AK/随机配枪，无 Boss / 包抄；干员能力只有救援/治疗/烟雾；收集品世界模型是占位方块、掉落权重为项目值；撤离的支付/阵营条件未接入；穿透按厚度而非材质；分仓布局、手雷引信、经济价位等是项目自定或推断值。源码里有 `EExtractionReferenceStatus { Temporary, Inferred, Verified }` 标注证据等级。
9. **占位 / 纯数据文件**：`ExtractionState.cpp`、`ExtractionPatrolArea.cpp`、`SpaceCenter\SpaceCenterInstanceGroup.cpp`（仅一个 HISM）、`RocketMovementComponent.cpp`（空）、`Animation/Notifies/AnimNotify_Example*`（教学示例）、`ExtractionNavigation.cpp`（仅 `WITH_EDITOR` 工具，运行时返回 false）。`APickup` 基类回调为空、销毁特效被注释。
10. **已知代码异味**：`LagCompensationComponent.cpp:478` 未命中分支返回 `{true,false}` 仍可能触发扣血（疑为 Bug）；`UCombatComponent::Drop` 有 `@TODO 有bug` 注释；`BuffComponent::MulticastSpeedBuff_Implementation` 未判空；`AWeapon::OnRep_WeaponState` 注释称「回调没触发」。
11. **配置与文档漂移**：`.bat` 的打包地图清单不含已登记的 `L_SovietMilitaryExtraction`，`run_build.py` 含；`DefaultEngine.ini` 的 `DefaultGraphicsRHI` 重复两行；`AndroidFileServerRuntimeSettings` 含明文 SecurityToken；`Animation/Notifies/README.md` 仍写 UE 5.5.4；旧 Roadmap 文档已过期。
12. **验收边界**：README 记录的打包 / LAN 会话验收只覆盖会话连接与地图切换，明确「不代表已验证完整战斗流程、画面或跨机器网络」；本次盘点也没有运行任何程序。

---

## 16. 证据索引与自查方式

| 想确认什么 | 看哪里 |
| --- | --- |
| 专服会话与客户端加入 | `Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp`、`Menu.cpp` |
| 大厅准备 → ServerTravel | `Source/BrokenEdge/GameMode/LobbyGameMode.cpp`（`TryStartGame:258`、`OnStartSession:328`） |
| 战局状态机 | `Source/BrokenEdge/Extraction/Core/ExtractionGameMode.cpp:610-730` |
| 射击分流 | `Source/BrokenEdge/Weapon/HitScanWeapon.cpp:61-90`、`Weapon/ExtractionBullet.cpp:71-132` |
| 武器参数 | `Content/Extraction/Data/WeaponMechanics.json`、`Source/BrokenEdge/Weapon/WeaponMechanics.cpp:49` |
| 物品网格与溯源 | `Source/BrokenEdge/Extraction/Inventory/ExtractionItems.h:20, 230-330`、`ExtractionItems.cpp` |
| 持久化格式 | `Source/BrokenEdge/Extraction/Persistence/ExtractionPersistence.cpp:16, 173-195, 412-442` |
| 守卫 AI | `Source/BrokenEdge/Extraction/AI/ExtractionGuard.cpp`、`Source/BrokenEdge/Character/BTService_UpdateGuardContext.cpp`、`BTTask_EngageTarget.cpp` |
| 遍历预测 | `Source/BrokenEdge/Traversal/TraversalMovementComponent.cpp` |
| 复制注册 | 各类的 `GetLifetimeReplicatedProps()`，重点 `ExtractionState.cpp`、`LobbyGameState.cpp` |
| 配置 | `Config/DefaultEngine.ini`、`Config/DefaultGame.ini`、`Config/DefaultInput.ini` |
| 资产生成 | `Source/StingerTools/Private/*Builder.cpp`、`Scripts/Extraction/place_*.py` |
| 官方差距与验收记录 | `Artifacts/Extraction/OvernightEvidence/CurrentCapabilities.md`、`Artifacts/Extraction/官方功能差距审计.md`、`Artifacts/Extraction/Overnight-2026-11-03.md` |

**若要自查某条结论**，建议在源码里直接搜函数名（本文所有函数名都来自源码），再追 `GetLifetimeReplicatedProps()` 确认复制注册、追 `UFUNCTION(Server/Client/NetMulticast)` 确认权威端。注意 `Artifacts/Extraction/Phase7CEvidence/files/`、`Phase7DEvidence/files/` 里有旧源码副本，别和 `Source/` 真身混淆。

---

*本文基于 2026-11-06 工作区源码静态盘点；源码变更后应重新核对，尤其是标注「已实现 / 未完成」的条目。*
