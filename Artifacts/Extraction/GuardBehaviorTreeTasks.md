# 守卫行为树 C++ Task 使用说明

## 交战走位与随机配枪（2026-11-03）

`Engage Guard Target` 不再固定站定：

- 每 `GuardStrafeInterval`（默认 3 s，±30%）向目标左右两侧横移最多 `GuardStrafeDistance`（300 cm），
  目标点必须导航可达、同层且站姿眼睛能看到目标，换位途中照常开火。
- 换弹（空弹匣且有备弹）或 1 秒内中弹时，在 `GuardCoverRadius`（900 cm）内找掩体：
  目标眼睛到掩体点站姿眼高和胸口高度都被挡住；至少躲 2 秒且换完弹后横移出来继续射击。
  两次找掩体间隔不少于 `GuardCoverCooldown`（6 s）。
- 开局从 `GuardWeapons`（AK / M4A1 / SCAR-H）随机配枪，弹药为该枪默认弹种 60 发。
- `GuardStrafeInterval=0`、`GuardCoverRadius=0`、`GuardWeapons=AK` 可恢复此前站定、全员 AK 的回归口径；
  旧的守卫回归场景是按站定射击写的。

## 武器射速与追击（当前实现）

当前默认 `GuardShotInterval=0.15`，即每发至少间隔 0.15 秒。`GuardShotInterval=0` 使用当前装备的 `Weapon.FireDelay`，与玩家长按自动射击一致。
武器配件改变射速后 AI 也跟随变化。正数覆盖值与 FireDelay 取较大值，只能额外降低射速。
仍保留反应时间、弹药、服务器射速校验和完整换弹流程。旧 Combat Check Interval 不再限速，
保留字段只为兼容已有任务资产，不需要重建节点。

沿用现有搜索分支，无需新增黑板键：

```text
Sequence [SearchActive Is Set, Observer Aborts = Both]
    Stop Guard Actions
    内置 Move To [LastKnownLocation, Allow Strafe 关闭]
    内置 Wait [60 秒，占位等待，由 Service 超时中断]
```

- 可见时缓存玩家位置、速度和时间，再次检查墙壁及烟雾。
- 丢失视野时清空 TargetActor，根据最后可见的平面速度向前预判 1.25 秒，位移最多 600 cm。
  追击点投影至 NavMesh，并校验路径完整且总长不超过 8000 cm；失败则尝试真实最后可见位置。
  两个点都不可达或超预算则请求返回。无导航时保留最后位置，由内置 Move To 返回失败。
- 进入搜索前将追击点写入 LastKnownLocation，随后冻结，避免每次更新不断向前漂移。
  该键在搜索阶段表示记忆推算的目的地，不再严格等于玩家最后实际位置。
  隐藏玩家的当前位置和速度不会用于继续更新追击点。
- NPC 默认移动速度为玩家当前普通移速 600 - 75 = 525 cm/s；搜索速度不低于该值，仍受骨折减速影响，可暂时离开巡逻区域追击。
- 从最后可见时刻起最多追击/观察 12 秒。到点后原地等待观察；重新看见玩家由交战分支抢占。
  超时后 SearchActive=false、ReturningToRoute=true，执行现有返回巡逻逻辑。

目前是向预判点移动后观察，不会在多个拐角间连续扫点。搜索寻路失败时仍按原 Selector
落入低优先级分支，可按后文增加失败兜底。导航范围需覆盖追击道路。
C++ 不修改行为树和黑板资产，也不重新实现内置 Move To。

配置在 `Config/DefaultGame.ini` 的 `[/Script/BrokenEdge.ExtractionRules]`：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| GuardShotInterval | 0.15 | 射击间隔，秒；仍受武器最小间隔约束 |
| GuardWalkSpeed | 525 | NPC 默认移动速度，cm/s |
| GuardPursuitSeconds | 12 | 从最后可见时刻计时，秒 |
| GuardPursuitPredictionSeconds | 1.25 | 速度预判提前量，秒 |
| GuardPursuitPredictionDistance | 600 | 预判位移上限，cm |
| GuardPursuitMaxDistance | 8000 | 进入追击时检查的导航路径长度上限，cm |
| GuardPursuitSpeed | 360 | 搜索速度下限；实际取其与 GuardWalkSpeed 较大值，当前为 525 cm/s |

回归检查实际开火间隔、追击超时与距离退出，并在真实 NavMesh 上检查速度预判、
位移上限、导航投影和不可达预判点回退。

## 任务与行为树配置

本次新增四个自定义任务，并补齐现有 `BTService_UpdateGuardContext` 的黑板上下文更新。
移动使用 UE 内置 `Move To`，等待使用内置 `Wait`，没有重复实现这两个节点。
本说明对应 `Content/Blueprints/Ai/BB_ExtractionGuard` 和 `BT_ExtractionGuard`。
行为树资产的节点连接需要在编辑器中按下述结构配置；本次没有重写现有 `.uasset`。

## 当前默认：在活动范围内随机巡逻

Controller 默认开启 **Use Random Patrol**。保持现有行为树连接：

```text
Set Guard Patrol Destination → 内置 Move To(PatrolLocation) → Complete Guard Patrol Arrival → 内置 Wait
```

不需要手工填写 Route.Points，也不需要重建任务节点。每次普通巡逻重新选择随机目标，
搜索结束后的返回分支优先复用尚未完成且仍可达的目标。等待时长仍由你在行为树中配置。

有两种活动范围配置方式：

1. **默认圆形范围**：以 Controller 初次接管 NPC 时的位置为固定圆心，Random Patrol Radius
   默认 1000 cm（10 米），Random Patrol Height 默认 200 cm。半径不会跟着 NPC 移动而漂移。
   可在敌人蓝图的 Event Possessed 中取得 ExtractionGuard 控制器，设置这两个属性。
2. **指定活动盒**：在关卡放置 ExtractionPatrolArea，调整其位置、旋转、缩放或 Box Extent。
   给敌人蓝图增加实例可编辑的 ExtractionPatrolArea 对象引用；在 Event Possessed 中
   Cast 到 ExtractionGuard，使用 Set Patrol Area 分配此实例。活动盒优先于默认圆形范围。

两种方式都需要 NavMeshBoundsVolume，并确认区域内存在可连通的绿色导航地面。
活动盒本身没有碰撞，不会生成导航。可以多个守卫共用一个盒，也可以每个守卫分配不同的盒。

选点会检查完整导航路径，拒绝部分路径、范围外目标和绕出活动范围的路径。
范围会为角色胶囊水平内缩。只约束随机巡逻；现有交战/搜索分支的范围规则不在此变更内。
若搜索后 NPC 已在区域外，返回路径允许先从外部进入，但不接受进入后再次离开的路径。
这些检查作用于选点时查询的路径；内置 Move To 随环境变化重新寻路时，仍需实际地图回归。

Set Guard Patrol Destination 的 Minimum Travel Distance 默认 250 cm，避免原地附近小碎步；
Max Random Attempts 默认 8，最多配置 32。区域很小时可降低最短移动距离。
没有导航或全部选点失败时清空 PatrolLocation 并返回 Failed，由末尾 Wait 延迟重试。

如果要使用原来的固定路线，关闭 Controller 的 **Use Random Patrol**，再分配 Route。
原 SpawnGuards 仍使用地图路线点确定出生位置；随机模式不会再按这些点的顺序移动。

## 1. 黑板配置与数据归属

键名统一定义在 `Source/BrokenEdge/Character/GuardBlackboardKeys.h`。
图中 `CanRunAi` 的末尾是小写 i；Unreal 的 FName 比较不区分大小写，建议仍按图保持命名一致。
所有运行数据键均保持 **Instance Synced 关闭**，否则不同守卫可能共享目标或计时。

| 键 | 类型 | 写入者 | 含义 |
|---|---|---|---|
| SelfActor | Object/Actor | 引擎 | 保留内置自身引用 |
| CanRunAi | Bool | Service | 守卫存在、存活、未禁用行为，且世界和感知组件有效 |
| TargetActor | Object/Actor | Service | 本轮筛选出的当前可见玩家；目标丢失则清空 |
| LastKnownLocation | Vector | Service | 可见时为实际位置；丢失时为一次性导航预判追击点 |
| HasLastKnownLocation | Bool | Service | 是否已经记录过有效目标位置 |
| LastSeenTime | Float | Service | 最近一次看见玩家的服务器世界时间 |
| SearchActive | Bool | Service | 丢失目标后，在距离预算内且尚未达到 12 秒时限 |
| ReactionReadyTime | Float | Service | 首次发现、重新发现或切换目标后，最早允许尝试战斗的时间 |
| NextFireTime | Float | Engage Task | 上一次成功射击后计算的下次允许开枪时间 |
| PatrolLocation | Vector | 巡逻 Task | 当前随机目标或固定路线点；完成后清空 |
| ReturningToRoute | Bool | Service / 到点 Task | 搜索结束时置 true，到达巡逻点后置 false |

时间键存储的是 `World->GetTimeSeconds()` 对应的绝对时间，不是剩余秒数。默认值保持 0。
Service 将关联数据全部写完后统一发出黑板观察通知，避免交战分支在反应时间尚未写入时启动。

**CanRunAi 沿用当前 Service 的独立感知设计，不强制依赖搜打撤 GameMode/存储。**
开枪仍调用 `ExtractionCombatComponent::CanAct()` 和服务端射击入口。在普通测试地图上，
服务器实际接管的守卫可以独立射击和换弹；普通玩家继续使用原战斗协议。
如果地图使用 ExtractionGameState，则守卫和玩家依然需要 Active 阶段；Waiting/Ended 不会开火。
装备、弹药、动作互斥、烟雾和场景遮挡检查仍然有效。

守卫转向使用 AI Controller 的期望朝向：巡逻跟随 Move Focus，交战跟随 Gameplay Focus。
角色跳过玩家专用的持枪原地转身逻辑，CharacterMovement 使用非零 Yaw 转速进行平滑转向。
AI 的后坐力不会反写 ControlRotation；手部动画也改为读取战斗 Focus，而不是空的玩家准星目标。
当前 BP_EnemyCharacter 的 Stinger 网格相对 Yaw 为 0；守卫运行时按 Guard Mesh Yaw Offset
默认 -90 度修正视觉朝向。若替换其他模型，请在敌人蓝图类默认值中调整此参数。

## 2. 四个 Task 的职责

### Stop Guard Actions

类：`UBTTask_StopGuardActions`。

- 调用 `StopMovement()`，停止当前路径跟随。
- 清除 Gameplay 优先级的 Focus，使守卫不再持续面向丢失的目标。
- 即时返回 Succeeded，不需要 Tick、定时器或自定义 Abort。
- 不清除 TargetActor，不抢占 Service 的目标管理职责。
- 不取消正常进行中的换弹；死亡时仍由原生命周期代码调用 CancelReload。

推荐放在“不可行动”分支的 Wait 之前，以及搜索分支的 Move To 之前。

### Engage Guard Target

类：`UBTTask_EngageTarget`。Blackboard Key 选择 **TargetActor**，与 Service 的目标键一致。

执行顺序：

1. 校验服务器权限、守卫角色、CanRunAi、目标对象类型和玩家生命状态。
2. 设置诊断状态 Engage，停止移动，使用 SetFocus 面向目标。
3. 保持 InProgress。每帧检查射击截止时间，到期才检查遮挡并射击；旧 CombatCheckInterval 不再限速。
4. 等待 ReactionReadyTime 和 NextFireTime；任务重进不重置这些时间。
5. 检查现有战斗组件的 CanAct 和战斗状态，换弹期间不重复请求换弹。
6. 开枪前补查普通遮挡和烟雾，降低感知更新滞后造成的误射。
7. 空弹匣且有备用弹药时调用 BeginServerReload；没有备用弹药时保持关注并等待。
8. 调用 FireGuard，复用服务端射速、装备、弹药、散布、伤害和表现逻辑。
9. 只有 AcceptedShots 实际增加，才累计 Guard.Shots 并推进 NextFireTime。

基础瞄准仍是目标 Actor 位置上移 10 cm，再叠加长度为 20 cm 的随机方向偏移。
服务器在武器原有散布计算之后，额外追加 GuardExtraSpreadDegrees 配置的圆锥散射，默认半角 3 度。
这层角误差随距离扩大，10 米处的额外圆锥半径约 52 cm；实际总散布还包括武器原有误差。
设置为 0 可以关闭额外散射。只对守卫生效，客户端复用服务端端点，不重复采样。
当前规则从 UExtractionRules 读取：反应时间 2 秒、射击间隔 0.15 秒（仍受武器 FireDelay 约束）、守卫伤害倍率 0.15；
视觉首次发现距离由 GuardSightRadius 控制，默认 6000 cm（60 米）；已发现目标的丢失距离
由 GuardLoseSightRadius 控制，默认 7500 cm（75 米）。接管角色时重新应用配置，避免旧蓝图
控制器的感知子对象保留原来的 30 米。总视角仍为 120 度，墙壁与烟雾遮挡检查继续生效。
实际射击频率还受武器射速限制。普通测试地图和搜打撤使用相同的守卫伤害倍率，玩家使用守卫掉落武器不会被降伤。

该任务使用 `bCreateNodeInstance=true`，每个 AI 的节点实例单独保存 Focus 弱引用与轮询时间。
没有把当前目标缓存到共享的 Task 模板中，也没有复制一套伤害/换弹逻辑。
AbortTask 和 OnTaskFinished 都会清理自己持有的 Gameplay Focus，不清空感知结果或射击间隔。
目标失效时任务也会主动 Failed，作为装饰器中断之外的兜底。

### Set Guard Patrol Destination

类：`UBTTask_SetPatrolDestination`。Blackboard Key 选择 **PatrolLocation**。
默认行为见上方随机巡逻说明；以下路线索引逻辑仅适用于关闭 Use Random Patrol 后的固定模式。

- 从 `Controller.Route->Points[PatrolIndex]` 读取世界坐标。
- 将索引归一化到路线范围内，兼容负数调试值或路线点数量变化。
- 写入 PatrolLocation，按 ReturningToRoute 更新 Patrol/Return 诊断状态。
- 不推进巡逻索引；交战中断后仍返回尚未完成的点。
- 不执行 MoveTo，也不在运行时重建 NavMesh。
- 无路线、空路线或非法坐标时清空旧目的地并 Failed。

### Complete Guard Patrol Arrival

类：`UBTTask_CompletePatrolArrival`。Blackboard Key 同样选择 **PatrolLocation**。

- 只能放在 Move To 成功之后；不要放在移动节点前面。
- 随机模式确认黑板目的地仍在活动范围内；固定模式确认目的地仍对应当前路线点。
- 默认水平距离不超过 110 cm，垂直差不超过 150 cm；地面路线点与胶囊中心的高度差在此范围内。
- 到达后递增 PatrolArrivals，恢复 Patrol 状态；只有固定模式才循环更新 PatrolIndex。
- 清空 PatrolLocation，防止对同一个目的地重复完成；清除 ReturningToRoute。
- 不负责等待，后面连接内置 Wait，设为 0.6 秒。

若实际地图角色胶囊更高或路线点高程不同，需要相应调整 Vertical Tolerance。
垂直检查是新增的防误判措施，避免与目标同一 XY、不同楼层时直接推进路线。

## 3. 在编辑器连接行为树

顶层使用 Selector，添加 **Update Guard Context** Service。间隔 0.1 秒，随机偏差 0。
各分支从左向右代表从高到低优先级：

```text
Root
└─ Selector [Service: Update Guard Context]
   ├─ Sequence [CanRunAi == false; Aborts Both]
   │  ├─ Stop Guard Actions
   │  └─ Wait 0.2s
   ├─ Sequence [TargetActor Is Set; Aborts Both]
   │  └─ Engage Guard Target (Blackboard Key = TargetActor)
   ├─ Sequence [SearchActive == true; Aborts Both]
   │  ├─ Stop Guard Actions
   │  ├─ Move To (LastKnownLocation)
   │  └─ Wait 60s
   ├─ Sequence [ReturningToRoute == true; Aborts Self]
   │  ├─ Set Guard Patrol Destination (PatrolLocation)
   │  ├─ Move To (PatrolLocation)
   │  └─ Complete Guard Patrol Arrival (PatrolLocation)
   ├─ Sequence
   │  ├─ Set Guard Patrol Destination (PatrolLocation)
   │  ├─ Move To (PatrolLocation)
   │  ├─ Complete Guard Patrol Arrival (PatrolLocation)
   │  └─ Wait 0.6s
   └─ Wait 0.5s
```

搜索末尾的 Wait 60 秒只是等待占位：实际搜索由 Service 的 12 秒追击窗口截止，
SearchActive 变 false 后，Aborts Both 会立即中断移动或等待。不能依靠“Move To 后再 Wait 5 秒”
作为搜索超时，否则到达耗时会被额外加上，且寻路过程中无法按时退出。
搜索移动若失败会落入低优先级分支；如需要失败后原地搜索到超时，可在搜索分支内部增加
Selector，把 `Move To → Wait 60s` 与 `Wait 60s` 作为两个选项。

最右侧 Wait 0.5 秒是缺路线/寻路失败/目标非法时的回退，避免整棵树在同一帧内反复失败重启。
返回分支到点时会清除 ReturningToRoute，从而结束返回分支；下一轮普通巡逻负责后续点。

内置 Move To 建议配置：

| 位置 | Blackboard Key | Acceptable Radius | Allow Partial Path |
|---|---|---|---|
| 搜索 | LastKnownLocation | 80 cm | 关闭 |
| 巡逻/返回 | PatrolLocation | 70 cm | 关闭 |

巡逻到点检查为 110 cm。若 Move To 开启 Reach Test Includes Agent Radius，角色胶囊半径与
Acceptable Radius 之和应落在该范围内；更大角色要同步调整 Arrival Radius。
保持 Project Goal Location 等导航设置与地图 NavMesh 一致，路线点应落在可到达的导航区域。

## 4. 接入与验证范围

现有控制器在接管角色、角色组件就绪并初始化 GuardInventory 后启动行为树，符合这些任务的装备访问顺序。
新增 C++ 类需要完成编译，并重启仍加载旧模块的编辑器，才能稳定看到全部反射类和新注释。

自动化测试过滤器：`BrokenEdge.Extraction.GuardBT`。

- PatrolArrival：远距离/不同楼层拒绝到点、成功到点与循环、重复完成、动态路线变更、空路线。
- IsolationAndInvalidTarget：共享瞬时任务下两个守卫的索引/目的地隔离、自身/空目标拒绝、停止动作。
- EngagementAbort：反应时间内保持交战但不射击、中断/结束释放 Focus、重进保持时间键。
- SearchContext：感知丢失进入追击、12 秒超时请求返回、追击距离预算、旧记忆不重启搜索、禁用角色停止行动分支。
- RandomPatrolBounds：固定圆心、胶囊边界内缩、楼层高度差、越界路径、旋转缩放的活动盒、无导航失败与随机到点。
- RandomPatrolNavigation：临时地板上构建真实 NavMesh，连续随机选点、完整路径与边界检查、返回复用目标、指定活动盒选点。
- FacingAndPatrolRotation：直接验证角色平滑转向 Move/Gameplay Focus、战斗结束恢复巡逻方向、手部瞄准目标来源。
- EnemyBlueprintMeshFacing：实际加载现有敌人蓝图，验证身体网格前向修正、位置/缩放保留，以及偏航修正可配置。
- StandaloneFireAndReload：无搜打撤 GameState 时实际 AK 射击扣弹、换弹扣备用弹药，正式搜打撤各阶段校验保持有效。
- ExtraSpread：追加散射的半角限制、射线长度、距离放大、零角关闭和服务器种子确定性。
- SightToBlackboard：真实 Sight 检测范围内玩家，Service 写入目标与反应时间，失去视野后搜索最后位置。
- ExpandedSightRange：实际敌人控制器蓝图在 45 米发现、65 米保留、80 米丢失，回到 55 米重新发现目标。

测试使用临时 World 和黑板，不修改地图/黑板资产或用户存档。
实际地图的内置 Move To 路径、Sight 发现/重新发现、武器实弹换弹与多人掉落仍需要 PIE/专服回归；
这些运行场景不由上述临时 World 测试替代。
