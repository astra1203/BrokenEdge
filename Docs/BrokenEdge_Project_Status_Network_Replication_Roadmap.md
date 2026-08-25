# BrokenEdge 与 UE5BlasterLearning 功能对比及教学完善路线

> 当前项目：`C:\UnrealProjects\BrokenEdge`
>
> 参考项目：`D:\SourceFiles\UnrealProjects\UE5BlasterLearning-main`
>
> 当前源码基线：分支 `master_20260824`，提交 `4ddc16f`（2026-08-25 审计）
>
> 引擎基线：Unreal Engine 5.5.4
>
> 用途：教师备课、学生任务拆分、阶段验收和多人网络排错

## 1. 结论先行

BrokenEdge 已经完成了多人第三人称射击项目的“基础设施层”：

- Enhanced Input 移动、视角、跳跃、蹲伏、装备、瞄准、换弹和丢枪输入；
- 角色网络移动、Aim Offset、原地转向和动画实例数据；
- 武器重叠提示、主副武器装备、背包挂接、切枪和基础掉落状态；
- 动态准星、屏幕中心追踪、弹匣/备用弹药 HUD 和换弹主体代码；
- 生命、护盾、弹药的复制与本地 HUD 桥接；
- 生命、护盾、速度、跳跃和弹药五类拾取物；
- 速度/跳跃 Buff 的 Multicast 表现同步；
- PlayerState、GameState、队伍分数 HUD 和比赛状态字段；
- OnlineSubsystemNull 会话插件、Game/Editor/Server Target 及客户端/服务端打包脚本。

但它目前还不是一个完整可玩的联机射击闭环。最关键的断点是：

```text
当前已有：瞄准 + 准星追踪 + 武器 + 弹药 + Health/Shield
                         ↓
当前缺失：IA_Fire → 射速/扣弹 → ServerFire/MulticastFire
                         ↓
当前缺失：HitScan/Projectile → 服务器伤害 → 淘汰/重生
                         ↓
当前缺失：计分事件 → 比赛倒计时 → 队伍规则 → 完整对局
```

因此最适合学生的下一阶段不是直接做延迟补偿、夺旗或复杂弹丸，而是：

1. 先修完现有网络状态闭环和明确缺陷；
2. 完成“基础联机命中扫描射击”；
3. 接上护盾/生命、淘汰和重生；
4. 再让现有 PlayerState、GameState 和比赛状态框架真正工作；
5. 最后扩展队伍、更多武器、会话和服务器倒带。

## 2. 审计方法与结论边界

本次对比实际检查了：

- BrokenEdge 的 `Source/BrokenEdge`、`Plugins/MultiplayerSessions`、`Config`、Target 和打包脚本；
- 当前 Input Action、Input Mapping Context、角色/GameMode/HUD/PlayerState/GameState Blueprint 中可读取的资源引用；
- `MainMap` 中可读取的武器、拾取物和 GameMode 引用；
- 完整参考项目的 Character、Combat、Weapon、GameMode、GameState、PlayerState、HUD、Pickup、Session 和 Lag Compensation 源码；
- 当前的 [武器手部 IK 问题诊断与修复指南](UE5_Weapon_Hand_IK_Diagnosis_and_Fix.md)。

本文使用以下状态：

| 状态 | 含义 |
| --- | --- |
| 已完成主体 | 源码和必要 Blueprint/资源入口存在，主要调用链闭合 |
| 部分完成 | 主体存在，但至少一个联机、表现、边界或配置环节未闭合 |
| 已搭架 | 只有字段、类型、空回调、TODO 或零散 UI，不能算可用功能 |
| 未实现 | 当前工程没有对应的业务入口或核心类 |

“已完成主体”仍是源码和资源静态结论，不自动代表已通过 PIE、Dedicated Server、高延迟、丢包、Late Join 或打包运行验收。参考项目用于说明功能依赖和目标形态，不建议逐行照抄，也不把参考代码本身视为无缺陷标准库。

## 3. 当前 Blueprint 和资源的实际落地情况

### 3.1 已确认的实际 GameMode 配置

`Content/Blueprints/GameMode/BP_BrokenEdgeGameMode.uasset` 中能读取到以下引用：

- `BP_BrokenEdgeCharacter`；
- `BP_BrokenEdgePlayerController`；
- `BP_BrokenEdgePlayerState`；
- `BP_BrokenEdgeGameState`；
- `BP_BrokenEdgeHUD`。

这说明当前实际玩法框架不只依赖 `ABrokenEdgeGameMode` 构造函数里的 C++ 默认 Pawn。判断当前运行配置时，应以 Blueprint Class Defaults、地图 World Settings 和实际 PIE GameMode 为准。

### 3.2 已确认的 Enhanced Input 资源

`BP_BrokenEdgeCharacter` 和 `IMC_CharacterDefault` 已引用：

```text
IA_Move
IA_Look
IA_Jump
IA_Crouch
IA_Equip
IA_Aim
IA_Reload
IA_Drop
```

当前 `Content/Input/Actions` 中没有 `IA_Fire`，角色源码也没有 Fire 输入绑定。这是“射击功能未实现”的直接资源和源码双重证据。

### 3.3 已确认的 HUD 与地图资源

- `WBP_CharacterOverlay` 已包含 Health、Shield、WeaponAmmo、CarriedAmmo、Score、Defeats、MatchCountdown、RedTeamScore 和 BlueTeamScore 控件名；
- `BP_BrokenEdgeHUD` 引用了 `WBP_CharacterOverlay`；
- `MainMap` 中能读取到 `BP_SMG_Ammo` 实例引用；
- 当前有 SMG 与狙击枪 Blueprint，但没有参考项目中的完整武器 Blueprint 族、弹丸 Blueprint、夺旗地图和队伍出生点资源。

## 4. 功能对比总表

| 功能域 | BrokenEdge 当前状态 | 完整参考项目 | 教学判断 |
| --- | --- | --- | --- |
| 角色移动与视角 | 已完成主体：Enhanced Input + CharacterMovement | 已完成 | 可作为网络移动基础，不必重写移动 RPC |
| 跳跃、蹲伏、瞄准 | 已完成主体 | 已完成 | 继续做多人状态验收 |
| Aim Offset / 原地转向 | 已完成主体，含 Simulated Proxy 计算 | 已完成 | 适合讲 Autonomous/Simulated Proxy 差异 |
| 武器手部 IK | 部分完成，Socket/ABP/开关存在已知问题 | 已完成主体 | 先按专项文档修复并多人验收 |
| 武器拾取提示 | 已完成主体：OwnerOnly `OverlappingWeapon` + RepNotify | 已完成 | 可作为 Owner-only 复制示例 |
| 主副武器装备 | 已完成主体 | 已完成 | 仍需补副武器远端挂接验收 |
| 主副武器切换 | 部分完成 | 已完成 | `OnRep_CombatState` 远端蒙太奇仍未接通 |
| 主动丢枪 | 已搭架且有缺陷 | 参考项目主要在替换/淘汰时掉落 | 当前没有 Server RPC，客户端路径不能作为权威实现 |
| 瞄准与动态准星 | 已完成主体 | 已完成并含 FOV/狙击镜 | 可复用命中目标点作为开火输入 |
| 换弹与备用弹药 | 部分完成 | 已完成，含逐发霰弹枪换弹 | 当前无开火扣弹，远端换弹表现仍不完整 |
| 弹药拾取 | 已完成主体，当前只有 SMG 资产 | 多武器弹药 + 生成点 | 先验收容量与剩余量，再扩资产 |
| 基础开火 | 未实现，无 `IA_Fire`、`Fire()` 主链 | Fire/CanFire/Timer/RPC/Multicast 完整 | 最适合的下一项核心功能 |
| 命中扫描 | `AHitScanWeapon` 为空 | 射线、头部伤害、枪口/弹道/命中特效 | 在开火框架后实现 |
| 弹丸武器 | 未实现 | 子弹、火箭、榴弹及移动组件 | 命中扫描稳定后再教 |
| 霰弹枪 | 只有类型和换弹注释 | 散射、多目标合并伤害、逐发换弹 | 后续综合题 |
| 手雷 | 只有 `CombatState` 枚举预留 | 输入、蒙太奇、Server RPC、生成弹丸 | 后续 Actor 复制课程 |
| 生命/护盾 | 属性复制、Buff 和 HUD 已完成主体 | 接入服务器伤害 | 只差伤害入口，不能称为战斗系统完成 |
| 受伤/淘汰/重生 | 未实现 | ReceiveDamage、Elim、溶解、Respawn | 基础射击之后的第二个核心闭环 |
| PlayerState 分数/失败数 | 字段、RepNotify、HUD 已搭架 | 接入淘汰、排行榜 | 当前没有业务事件调用计分 |
| GameState 队伍分数 | 已搭架，但当前 RepNotify 红蓝写反 | 正确复制并驱动 HUD | 适合作为短小的 RepNotify 修复练习 |
| 比赛 Warmup/Match/Cooldown | 只有字段、RPC 壳和空回调 | GameMode Tick、状态切换、时间同步、Late Join | 当前不能算比赛流程 |
| 队伍分配与友伤 | `TeamsGameMode::PostLogin` 为空 | 自动分队、退出清理、友伤规则、队伍得分 | 在淘汰计分之后实现 |
| 夺旗 | 只有 Flag 类型和状态预留 | Flag、FlagZone、CTF GameMode 和地图 | 明确延期 |
| 拾取物刷新 | 地图固定实例 | `PickupSpawnPoint` 服务器随机刷新 | 射击闭环后的小型 Actor 生命周期课程 |
| 会话 Host/Find/Join | Host/Find 主体存在，Join UI 链不完整 | 查找、筛选、加入、Travel、销毁 | 与核心战斗分开作为网络服务专题 |
| Dedicated Server | Target、配置和打包脚本存在 | 参考工程以多人流程为主 | 基础设施存在，仍需实际双客户端运行验收 |
| 返回主菜单/离开会话 | 未实现 | ReturnToMainMenu + DestroySession | 会话专题后半段 |
| 时间同步/高 Ping | 未实现 | Server time、SingleTripTime、高 Ping 回调 | 比赛倒计时和 SSR 的前置课 |
| 延迟补偿 | 只有角色 HitBox | 历史帧、回退、HitScan/Projectile/Shotgun 请求 | 最后阶段，不能提前 |

## 5. BrokenEdge 已完成了哪些功能

### 5.1 角色输入、移动与网络移动

输入绑定集中在 [`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp)：

```text
Enhanced Input
  -> Move / Look / Jump / Crouch
  -> Equip / Aim / Reload / Drop
  -> ACharacter / UCharacterMovementComponent
```

移动使用 UE 的 CharacterMovement 客户端预测、服务器校验和模拟代理平滑，没有为每一帧 Move/Look 自建 Reliable RPC，这是正确的多人移动基础。

### 5.2 角色动画数据与远端转向

[`BrokenEdgeAnimInstance.cpp`](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.cpp) 已收集：

- 速度、腾空、加速度和蹲伏；
- 是否装备、是否瞄准；
- Yaw Offset、Lean、AO_X、AO_Y；
- FABRIK、Aim Offset 和右手变换开关；
- 淘汰、换弹和切枪相关状态入口。

[`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp) 已实现本地 Aim Offset、远端 Pitch 映射、`OnRep_ReplicatedMovement()` 和 `SimProxiesTurn()`。这部分可直接用于讲解“本地控制角色”和“其他客户端看到的模拟代理”为何采用不同的数据来源。

### 5.3 武器拾取、装备和主副武器结构

已存在的主要链路：

```text
服务器 Weapon AreaSphere 重叠
  -> Character::SetOverlappingWeapon
  -> OverlappingWeapon OwnerOnly 复制
  -> OnRep_OverlappingWeapon
  -> 仅拥有者显示 Pickup Widget

Equip 输入
  -> ServerEquipButtonPressed
  -> CombatComponent::EquipWeapon
  -> EquippedWeapon / SecondaryWeapon RepNotify
  -> 右手或背包 Socket 挂接
```

武器 Actor 已开启复制和移动复制，`WeaponState` 使用 RepNotify 控制 Equipped、Secondary 和 Dropped 三种状态。拾取提示的本地分支与远端 Owning Client RepNotify 都已存在。

### 5.4 瞄准、准星追踪和 HUD

[`CombatComponent.cpp`](../Source/BrokenEdge/Component/CombatComponent.cpp) 已完成：

- 本地屏幕中心 Deproject；
- Visibility 射线与 `HitTarget`；
- 移动、腾空、瞄准造成的准星扩散；
- 瞄准状态本地响应和 `ServerSetAiming`；
- 武器准星纹理写入 HUD。

[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp) 和 [`CharacterOverlay.h`](../Source/BrokenEdge/UI/CharacterOverlay.h) 已建立本地 HUD 桥接与 Widget 晚初始化缓存，涵盖生命、护盾、当前弹匣、备用弹药、个人分数、失败数和队伍分数。

### 5.5 换弹、备用弹药和弹药拾取

现有换弹链包含：

```text
IA_Reload
  -> CombatComponent::Reload
  -> ServerReload
  -> CombatState = Reloading
  -> Reload Montage / Anim Notify
  -> FinishReloading
  -> 服务器 UpdateAmmoValues
  -> Owner HUD 更新
```

`CarriedAmmo` 使用 `COND_OwnerOnly`，`ClientAddAmmo` 已用于把服务器装填结果发给所属客户端。`AAmmoPickup` 会调用 `PickupAmmo()`，按 `MaxCarriedAmmo` 截断并保留未被取走的剩余数量；当前地图和资源只确认了 SMG 弹药拾取。

### 5.6 五类服务器权威拾取物

当前已有：

- `AHealthPickup`；
- `AShieldPickup`；
- `ASpeedPickup`；
- `AJumpPickup`；
- `AAmmoPickup`。

`APickup` 只在 Authority 端延迟绑定重叠，派生类由服务器应用效果。Health/Shield 最终值通过 Character 属性复制；Speed/Jump 会调用 NetMulticast，把客户端预测所用的 CharacterMovement 参数同步过去。

### 5.7 基础复制框架

当前主要复制数据如下：

| 所属类 | 属性 | 策略 |
| --- | --- | --- |
| Character | `OverlappingWeapon` | OwnerOnly + RepNotify |
| Character | `Health`、`Shield` | RepNotify |
| Character | `bDisableGameplay` | 普通复制 |
| CombatComponent | `EquippedWeapon`、`SecondaryWeapon` | RepNotify |
| CombatComponent | `bAiming`、`CombatState` | RepNotify |
| CombatComponent | `CarriedAmmo` | OwnerOnly + RepNotify |
| Weapon | `WeaponState` | RepNotify |
| PlayerState | `Defeats`、`Team` | RepNotify |
| GameState | `RedTeamScore`、`BlueTeamScore` | RepNotify |
| PlayerController | `bShowTeamScores` | 普通复制 |
| PlayerController | `MatchState` | 声明了 RepNotify，但当前未注册到 Lifetime Props |

这已经足够让学生对照学习 Server RPC、Client RPC、NetMulticast、普通属性复制、Owner-only 复制和 RepNotify 的不同职责。

## 6. 已搭架但不能写成“已完成”的功能

### 6.1 丢枪不是服务器权威闭环

当前 `DropButtonPressed()` 直接调用 `CombatComponent::Drop()`，没有 Server RPC。远端客户端本地修改 `EquippedWeapon` 和 `WeaponState` 不能替代服务器权威修改，而且源码已经标注“有 bug”。

建议目标：

```text
Owning Client Drop 输入
  -> ServerDropWeapon RPC
  -> 服务器校验 CombatState / 当前武器
  -> Weapon::Dropped + 清理 EquippedWeapon
  -> RepNotify/Attachment 让所有客户端得到一致结果
```

还要明确有副武器时的策略：自动切到副武器、保留空手，或禁止丢弃最后一把武器。

### 6.2 远端切枪和换弹表现不完整

- `OnRep_SecondaryWeapon()` 中背包挂接仍被注释；
- `OnRep_CombatState()` 中 Reloading 和 SwappingWeapon 的远端蒙太奇分支仍被注释；
- `FinishSwapAttachWeapons()` 只允许 Authority 修改状态，必须依靠正确的 RepNotify 让其他机器更新表现；
- Ammo 不是复制属性，当前依赖装备时 Multicast 和换弹时 Client RPC，需要做丢包、切枪和 Late Join 验收。

### 6.3 队伍分数 RepNotify 有确定错误

[`BrokenEdgeGameState.cpp`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.cpp) 当前存在红蓝写反：

- `OnRep_RedTeamScore()` 调用了 `SetHUDBlueTeamScore(BlueTeamScore)`；
- `OnRep_BlueTeamScore()` 调用了 `SetHUDRedTeamScore(BlueTeamScore)`。

这不是“未来优化建议”，而是当前应先修的确定性缺陷。它非常适合作为一节 RepNotify 调试练习：让学生分别在 Server、Owning Client 和另一 Client 记录红蓝分数与对应 TextBlock。

### 6.4 PlayerState、Team 和 MatchState 只是框架

当前已经有 Score、Defeats、Team、Red/Blue Team Score 和 MatchState 字段，但缺少驱动它们的完整规则：

- 没有伤害/淘汰事件调用 `AddToScore()` 和 `AddToDefeats()`；
- `TeamsGameMode::PostLogin()` 仍是 TODO；
- 当前 PlayerState 没有完整的 `GetTeam()/SetTeam()` 业务接口，`OnRep_Team()` 为空；
- GameMode 没有 `bDelayedStart`、比赛状态 Tick、`OnMatchStateSet()` 广播和重开逻辑；
- `MatchState` 虽声明为 `ReplicatedUsing`，但当前 `GetLifetimeReplicatedProps()` 没有 `DOREPLIFETIME` 注册；
- `bShowTeamScores` 只有普通复制，没有复制通知；Overlay 已初始化后再收到变化时不会主动切换队伍分数显示；
- `ClientJoinMidgame()` 与 `OnRep_MatchState()` 为空；
- `ServerCheckMatchState()` 没有在 `BeginPlay()` 中形成实际调用闭环；
- HUD 虽有 `MatchCountdownText`，但 Controller 没有倒计时更新函数。

因此最新提交“搭建比赛状态同步框架”的准确含义是“字段和 RPC 入口已搭建”，不是“比赛流程已完成”。

### 6.5 手部 IK 尚未修复和运行验收

专项检查已确认：

- 当前狙击枪缺少 `LeftHandSocket`；
- 右手 Transform Bone 的 Rotation Mode 为 Ignore；
- IK 开关在武器失效后可能残留；
- 当前本地右手计算与动画蓝图门控仍需统一；
- 尚未完成 Standalone、Listen Server 和 Client 动画验收。

详细证据、修复顺序和检查表见 [武器手部 IK 问题诊断与修复指南](UE5_Weapon_Hand_IK_Diagnosis_and_Fix.md)。

### 6.6 会话系统仍未形成 Join 闭环

当前 Host 和 Find 已有主体，但仍有关键缺口：

- Menu 中 Find/Join 委托绑定被注释；
- Join 按钮只调用 `FindSessions()`，没有筛选结果、`JoinSession()` 和 `ClientTravel()`；
- Join/Destroy 的原生完成委托没有完成初始化和回调实现；
- 已存在同名会话时只设置了“销毁后重建”标记，实际 `DestorySession()` 调用仍被注释；
- Dedicated Server 路径仍取得 `ULocalPlayer` 并解引用，而专用服务器没有本地玩家；
- `DefaultGame.ini` 的自动启动配置没有对应的已确认启动调用链。

所以打包脚本和 Server Target 存在，不等于会话发现、加入、地图旅行和断线清理已经可用。

### 6.7 生命和护盾尚未接入战斗

Health/Shield、Buff、RepNotify 和 HUD 已经存在，但 Character 没有：

- `OnTakeAnyDamage` 绑定；
- `ReceiveDamage()`；
- GameMode 伤害修正；
- 护盾优先吸收、生命扣除后的淘汰入口；
- Elim、武器掉落、输入禁用、重生计时器和 Respawn。

因此当前“生命/护盾系统”是属性与 UI 系统，不是完整伤害系统。

## 7. 推荐给学生的教学完善顺序

### P0：先建立可信的多人基线（2～3 课时）

目标：不增加大玩法，先让已有状态在 Listen Server + 2 Clients 中一致。

任务：

1. 修复 GameState 红蓝分数 RepNotify 写反；
2. 为主动丢枪增加 Server RPC、权限校验和主副武器策略；
3. 完成 `OnRep_SecondaryWeapon()` 的背包挂接；
4. 完成 `OnRep_CombatState()` 的远端换弹/切枪表现；
5. 处理未命中时准星颜色恢复、Viewport/Controller 无效路径；
6. 按 IK 专项文档补齐 Socket、ABP 配置和状态清零；
7. 为关键日志统一打印 NetMode、LocalRole 和 `IsLocallyControlled()`。

验收：

- Client A 丢枪后，Server、Client A、Client B 都看到同一把武器落地；
- 切枪和换弹在第三方观察者上只播放一次且挂接正确；
- 红蓝分数分别只更新对应 TextBlock；
- 未装备/丢枪/换弹/切枪后 IK 不残留；
- 无空指针、无客户端私自改变权威状态。

### P1：基础联机开火框架（3～4 课时）

这是当前最适合新增的核心功能。

任务拆分：

1. 新建 `IA_Fire`，加入 `IMC_CharacterDefault` 并在 Character Blueprint 赋值；
2. Character 绑定 Pressed/Released，转发 `FireButtonPressed(bool)`；
3. 在 CombatComponent 增加 `CanFire()`、`bCanFire`、`FireDelay` 和 FireTimer；
4. 在 Weapon 增加虚函数 `Fire()`、`SpendRound()`、自动/半自动配置；
5. 增加 `ServerFire` 校验和 `MulticastFire` 表现；
6. 处理拥有者本地预测，避免本地开火蒙太奇/特效播放两次；
7. 服务器权威扣弹，所属客户端 HUD 及时更新。

推荐链路：

```text
Owning Client 输入
  -> 本地即时反馈
  -> ServerFire(HitTarget, FireDelay)
  -> 服务器校验 Weapon / Ammo / CombatState / FireDelay
  -> 服务器扣弹并决定是否允许开火
  -> Multicast/复制状态让观察者播放表现
  -> Owner-only/Client RPC 校正弹药
```

验收：

- 无武器、空弹匣、换弹中和切枪中不能开火；
- 快速连点不能突破 FireDelay；
- 自动武器按住连续射击，松开立即停止；
- 三台实例只看到一次开火表现；
- 最终弹药以服务器为准。

### P2：命中扫描、护盾和服务器伤害（2～3 课时）

任务：

1. 实现 `AHitScanWeapon::Fire()`；
2. 从 Muzzle Socket 向 `HitTarget` 延伸射线；
3. 区分普通命中和头部命中；
4. 只由服务器调用 `ApplyDamage()`；
5. Character 绑定 `OnTakeAnyDamage` 并实现 `ReceiveDamage()`；
6. 伤害先扣 Shield，再扣 Health；
7. 保留 `OnRep_Shield/Health` 更新远端 HUD 与受击表现；
8. 增加枪口、弹道、Impact 和声音，但把“表现广播”和“伤害判定”分开。

验收：普通客户端不能直接修改别人 Health；Server、受击者和第三方观察者最终数值一致；Listen Server 主机不会因为本地/服务器双路径受到两次伤害。

### P3：淘汰、重生和个人计分（3～4 课时）

任务：

- GameMode 增加 `PlayerEliminated()` 和 `RequestRespawn()`；
- Character 增加 Elim 状态、Multicast 表现、输入禁用、碰撞关闭和定时重生；
- 淘汰时处理主副武器掉落或销毁；
- 攻击者 `AddToScore()`，受害者 `AddToDefeats()`；
- GameState 维护最高分玩家；
- HUD 增加淘汰播报；
- 处理自杀、空 Attacker、玩家离开和重生失败边界。

验收：Client A 淘汰 Client B 后所有机器看到同一结果；分数只增加一次；B 在服务器选择的出生点重生，HUD 和武器状态重新初始化。

### P4：比赛状态、倒计时和 Late Join（3～4 课时）

复用当前已经搭好的字段，补齐：

- GameMode `bDelayedStart`；
- WaitingToStart → InProgress → Cooldown → RestartGame；
- `OnMatchStateSet()` 通知所有 PlayerController；
- PlayerController 的 `ClientJoinMidgame()` 与 `OnRep_MatchState()`；
- 客户端/服务器时间同步和倒计时显示；
- Cooldown 胜者文本、玩法禁用和 HUD 切换；
- Late Join 获取当前比赛状态和剩余时间。

验收：服务器与两个客户端倒计时误差稳定在可接受范围；中途加入的客户端不会从完整 Warmup 重新计时，也不会漏掉当前 MatchState。

### P5：队伍模式（2～3 课时）

任务：

- PlayerState 补 `GetTeam()/SetTeam()`，`OnRep_Team()` 更新角色材质；
- TeamsGameMode 在 PostLogin/HandleMatchHasStarted 分配人数较少的队伍；
- Logout 清理队伍数组；
- 同队伤害规则；
- 淘汰时增加正确队伍分数；
- 队伍出生点和队伍颜色；
- 修复并验收现有队伍 HUD。

此阶段完成后，再考虑夺旗。当前 Flag 字段不是夺旗功能完成的证据。

### P6：更多武器和拾取循环（按课程长度选做）

推荐顺序：

1. `PickupSpawnPoint`：服务器生成、随机计时、销毁后重刷；
2. 完善各武器类型弹药拾取资产；
3. Projectile Weapon：Actor 生成复制与 ProjectileMovement；
4. 手雷：投掷蒙太奇、Server RPC、弹跳与径向伤害；
5. Shotgun：散射、多目标伤害合并和逐发换弹；
6. Sniper：FOV 插值、Scope Widget；
7. Capture The Flag：Flag、FlagZone、重置和计分。

每增加一种武器都必须复用统一的 Fire/Ammo/Damage 接口，避免在 Character 中堆积武器类型分支。

### P7：会话、退出和 Dedicated Server 专题（3～5 课时）

建议与核心战斗课程分开：

1. 补全 Find/Join 委托；
2. 按 MatchType 筛选并调用 JoinSession；
3. 解析连接字符串并 ClientTravel；
4. 已有会话先销毁再创建；
5. ReturnToMainMenu、DestroySession 和离开游戏；
6. Dedicated Server 使用无 `ULocalPlayer` 的创建路径；
7. 分别验证 Listen Server 与真正 Dedicated Server；
8. 最后再讨论 Steam/互联网 OSS，先不要把 Null/LAN 验证等同于公网服务。

### P8：服务器倒带延迟补偿（最后阶段）

前置条件必须全部满足：

- 基础射击、伤害和服务器权威稳定；
- 客户端/服务器时间同步稳定；
- HitBox 名称与骨骼对应；
- HitScan、Projectile 和 Shotgun 各自已有无 SSR 的正确版本；
- 已有高延迟/丢包测试记录。

随后再实现历史帧缓存、帧插值、碰撞盒回放、Server Score Request 和安全校验。当前仅创建 HitBox 不等于已实现 SSR。

## 8. 建议的课堂阶段表

| 阶段 | 建议课时 | 学生交付物 | 必须通过的多人验收 |
| --- | ---: | --- | --- |
| P0 现有功能收口 | 2～3 | RepNotify/丢枪/IK 修复记录 | Listen + 2 Clients 状态一致 |
| P1 开火框架 | 3～4 | IA_Fire、FireTimer、RPC、扣弹 | 射速与弹药服务器权威 |
| P2 命中与伤害 | 2～3 | HitScan、Shield/Health 伤害链 | 三方数值一致、无双重伤害 |
| P3 淘汰与重生 | 3～4 | Elim、Respawn、Score/Defeats | 一次淘汰一次计分，正确重生 |
| P4 比赛状态 | 3～4 | 倒计时、Late Join、Cooldown | 三端时间与状态一致 |
| P5 队伍模式 | 2～3 | 分队、友伤、队伍分数 | 退出/重连/得分边界正确 |
| P6 武器扩展 | 选做 | Projectile/Shotgun/Grenade/PickupSpawn | Actor 生成和伤害仍由服务器决定 |
| P7 会话与专服 | 3～5 | Host/Find/Join/Return/DS 报告 | 两独立客户端能加入与退出 |
| P8 延迟补偿 | 高阶选做 | 帧历史、回退判定、对比数据 | 有/无 SSR 在模拟延迟下可对比 |

## 9. 第一轮最具体的学生任务单

若下一次课就开始实施，建议按以下顺序发任务：

### 任务 1：修复队伍 HUD RepNotify

- 找出红蓝回调写反；
- 用 Server 按键或临时权威测试入口分别加红/蓝分；
- 记录三台实例上的 GameState 值和 TextBlock；
- 删除临时测试入口前保留验收截图或日志。

### 任务 2：把丢枪改成服务器权威

- 添加 `ServerDropWeapon`；
- 服务器检查武器与 CombatState；
- 明确副武器接替策略；
- 通过 RepNotify 修复三端挂接和准星/HUD；
- 测试客户端快速连续按 Drop。

### 任务 3：补远端换弹和切枪表现

- 实现 `OnRep_SecondaryWeapon()`；
- 实现 `OnRep_CombatState()` 的 Reloading/Swapping；
- 保证拥有者预测与 Multicast/RepNotify 不重复播放；
- 用第三个客户端观察另一个客户端。

### 任务 4：按专项文档修手部 IK

- 先补 `LeftHandSocket`；
- 再修 Transform Bone Rotation Mode 和本地门控；
- 最后处理无武器时的 IK 状态清零；
- 不允许通过盲调 RightHandSocket 掩盖左手目标点错误。

### 任务 5：建立最小 IA_Fire 闭环

- 只做到输入、状态门禁、射速和扣弹；
- 第一版先不造成伤害；
- 验收稳定后再进入 HitScan，避免一次同时调试输入、RPC、射线和伤害。

## 10. 多人验收矩阵

| 场景 | 必测内容 |
| --- | --- |
| Standalone | 输入、动画、HUD、基础功能无网络依赖错误 |
| Listen Server + 1 Client | 主机和客户端双向拾取、丢枪、切枪、换弹、射击 |
| Listen Server + 2 Clients | Owning Client 与 Simulated Proxy 表现是否一致 |
| Dedicated Server + 2 Clients | 不依赖服务器本地 HUD、PlayerController 或 ULocalPlayer |
| 延迟 100 ms | 本地预测、服务器回正、蒙太奇是否重复 |
| 丢包 5% | 最终状态是否一致，Unreliable 表现丢失是否可接受 |
| Late Join | 武器、弹药、生命、分数、MatchState 是否补齐 |
| Disconnect/Reconnect | PlayerState、队伍数组、武器 Owner、Session 清理 |

日志至少同时记录：

```text
NetMode
LocalRole / RemoteRole
IsLocallyControlled
RPC 发起端与执行端
OnRep 执行端
服务器最终值
Owning Client 最终值
第三方观察者最终值
```

## 11. 当前项目关键证据索引

- 角色输入、复制和动画入口：[`BrokenEdgeCharacter.h`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.h)、[`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp)
- 动画数据和手部 IK：[`BrokenEdgeAnimInstance.h`](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.h)、[`BrokenEdgeAnimInstance.cpp`](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.cpp)
- 武器、换弹、准星和备用弹药：[`CombatComponent.h`](../Source/BrokenEdge/Component/CombatComponent.h)、[`CombatComponent.cpp`](../Source/BrokenEdge/Component/CombatComponent.cpp)
- 武器状态和弹匣：[`Weapon.h`](../Source/BrokenEdge/Weapon/Weapon.h)、[`Weapon.cpp`](../Source/BrokenEdge/Weapon/Weapon.cpp)
- 空的命中扫描入口：[`HitScanWeapon.h`](../Source/BrokenEdge/Weapon/HitScanWeapon.h)、[`HitScanWeapon.cpp`](../Source/BrokenEdge/Weapon/HitScanWeapon.cpp)
- 属性增益：[`BuffComponent.h`](../Source/BrokenEdge/Component/BuffComponent.h)、[`BuffComponent.cpp`](../Source/BrokenEdge/Component/BuffComponent.cpp)
- 五类拾取物：[`Pickups`](../Source/BrokenEdge/Pickups)
- PlayerState：[`BrokenEdgePlayerState.cpp`](../Source/BrokenEdge/PlayerState/BrokenEdgePlayerState.cpp)
- GameState 和队伍分数：[`BrokenEdgeGameState.cpp`](../Source/BrokenEdge/GameState/BrokenEdgeGameState.cpp)
- 比赛字段和 HUD 桥接：[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp)
- GameMode 与队伍框架：[`GameMode`](../Source/BrokenEdge/GameMode)
- 会话插件：[`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp)、[`Menu.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/Menu.cpp)
- Dedicated Server Target：[`BrokenEdgeServer.Target.cs`](../Source/BrokenEdgeServer.Target.cs)
- IK 专项审计：[武器手部 IK 问题诊断与修复指南](UE5_Weapon_Hand_IK_Diagnosis_and_Fix.md)

## 12. 参考项目重点文件

```text
D:\SourceFiles\UnrealProjects\UE5BlasterLearning-main\Source\BlasterLearing\
├─ Character\BlasterCharacter.cpp
├─ BlasterComponent\CombatComponent.cpp
├─ BlasterComponent\LagCompensationComponent.cpp
├─ Weapon\Weapon.cpp
├─ Weapon\HitScanWeapon.cpp
├─ Weapon\ProjectileWeapon.cpp
├─ Weapon\Shotgun.cpp
├─ GameMode\BlasterGameMode.cpp
├─ GameMode\TeamsGameMode.cpp
├─ GameMode\CaptureTheFlagGameMode.cpp
├─ GameState\BlasterGameState.cpp
├─ PlayerState\BlasterPlayerState.cpp
├─ PlayerController\BlasterPlayerController.cpp
├─ Pickups\PickupSpawnPoint.cpp
└─ HUD\ReturnToMainMenu.cpp
```

参考工程的核心依赖顺序是：

```text
Fire/Ammo
  -> HitScan/Projectile/Shotgun
  -> ReceiveDamage
  -> Elim/Respawn
  -> PlayerState/GameState
  -> Match/Teams/CTF
  -> Session/Return
  -> Time Sync/Lag Compensation
```

教学时应按这个依赖顺序建立小闭环，而不是按参考工程文件夹顺序机械搬运。

## 13. 本次验证记录

已完成：

- 对照当前提交后的 Character、Combat、Weapon、Buff、Pickup、HUD、PlayerState、GameState、GameMode 和 Session 源码；
- 对照参考项目的开火、三类武器、伤害、淘汰、比赛、队伍、夺旗、拾取生成和延迟补偿源码；
- 检查当前 Input Action、Character/GameMode/HUD Blueprint 和 MainMap 中可读取的资源引用；
- 核对最新新增的 Drop、AmmoPickup、PlayerState/GameState、队伍分数 HUD 和 MatchState 框架；
- 把“已完成主体”“部分完成”“已搭架”“未实现”分开标注；
- 把 IK 专项诊断纳入第一轮教学任务。

本次未完成：

- 未在 Unreal Editor 中逐项打开 Blueprint Class Defaults、AnimGraph 和 World Settings 目视确认；
- 未执行编辑器外部完整构建；检查时 UnrealEditor 正在运行，本次也只修改了文档；
- 未运行 Standalone、PIE Listen Server、Dedicated Server 或网络模拟；
- 未把打包产物存在视为多人运行通过；
- 未修改任何 C++、Blueprint 或 Gameplay 业务逻辑；
- 文档中的“推荐”均为下一阶段任务，不是当前已实现功能。
