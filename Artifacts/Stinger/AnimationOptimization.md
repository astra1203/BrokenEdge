# 蜂医（Stinger）动画优化

2026-10-27。覆盖四块：**第一人称持枪表现**（移动衔接、起跳/落地、开镜/关镜）、**技能表现**（治疗/救援/烟幕）、
**第三人称左右瞄准偏移**，并核对了既有的**翻越/攀爬**动画集。
没有改动移动速度、伤害、弹药、技能结算与网络判定。

资产来源为工程内已导入并重映射的《三角洲行动》解包动画（`Content/Characters/Stinger/Animations`），
未使用外部目录；可用性盘点见 [AnimationInventory.md](AnimationInventory.md)。

## 优化前的问题

以 `DA_RifleLocomotion`、`ABP_StingerFirstPerson_Repaired` 与 `UStingerFirstPersonAnimInstance` 的实际读取结果为准：

| 问题 | 现象 | 原因 |
| --- | --- | --- |
| 循环动画播放速率恒为 1 | 角色速度 600/850 cm/s，动画自然速度 132/391/561，脚部明显滑步 | `ActivePlayRate` 从未按速度计算；第三人称节点有速率匹配，第一人称没有 |
| 空中无动画 | 起跳与滞空停在 Idle 持枪姿势 | `JumpAnimation` 为空，`WeaponPresentation.Jump` 也为空 |
| 落地无缓冲 | 落地瞬间直接回到行走循环 | 没有落地序列参与 |
| 开镜硬切 | 右键瞬间从腰射姿势跳到瞄准姿势 | `AimOn`/`AimOff` 已重映射但未接入，只靠 0.15 秒通用姿势混合 |
| Sprint 槽位错填 | 冲刺中被打断（换弹/开镜）会显示 RelaxIdle 放松站姿 | `ABP.SprintAnimation` 被填成 `AR_M_1P_RelaxIdle` |

## 改动

### 代码

1. `URifleLocomotionSet`（`Source/BrokenEdge/Animation/RifleLocomotionSet.h`）新增 5 个第一人称槽位
   `ViewAimOn`、`ViewAimOff`、`ViewJumpStart`、`ViewJumpLoop`、`ViewJumpLand` 与播放速率上限 `ViewPlayRateMax`（默认 2.5）。
   槽位留空时行为与旧版完全一致（回退到 0.15 秒姿势混合 / Idle），不是必填项。
2. `UStingerFirstPersonAnimInstance`（`.h`/`.cpp`）状态机改造：
   - **播放速率匹配**：循环段按 `Speed / 自然速度` 计算播放速率，钳制在 `[0.55, ViewPlayRateMax]`；
     过渡段保持源节奏（速率 1），避免过渡被拉快。自然速度取 `WalkSpeed 132 / RunSpeed 391 / SprintSpeed 561`。
   - **起跳/落地边沿触发**：`IsFalling` 上升沿播 `ViewJumpStart`（一次性），滞空播 `ViewJumpLoop`（循环），
     落地沿播 `ViewJumpLand`（一次性）。一次性动作播完自动回到循环姿势，期间不参与移动状态切换。
   - **开镜/关镜过渡**：`IsAiming` 上升沿播 `ViewAimOn`、下降沿播 `ViewAimOff`，播完后落到 `Aim` 或移动循环。
   - **优先级**：近战 > 起跳/落地 > 开镜过渡 > 空中/冲刺/瞄准循环 > 移动状态机。一次性动作不会被同帧的移动状态机覆盖。
3. 修复 Sprint 兜底：AK 的 `WeaponPresentation.Sprint` 由 `RelaxIdle` 改为 `AR_M_1P_Sprint_Loop`，
   `Jump` 指向 `AR_M_1P_AimJumpLoop` 作为无三段跳跃配置时的兜底。

### 资产绑定

| 槽位 | 资产 | 时长 |
| --- | --- | --- |
| `ViewJumpStart` | `Corrected/MappedV4/52f0dbef/AR_M_1P_AimJumpStart` | 0.469 s |
| `ViewJumpLoop` | `Corrected/MappedV4/22d89289/AR_M_1P_AimJumpLoop` | 0.469 s |
| `ViewJumpLand` | `Corrected/MappedV4/5628d1b9/AR_M_1P_AimJumpLand` | 0.569 s |
| `ViewAimOn` | `Corrected/MappedV4/7372680e/AR_M_1P_AimOn` | 0.303 s |
| `ViewAimOff` | `Corrected/MappedV4/0684eb34/AR_M_1P_AimOff` | 0.635 s |

跳跃使用**开镜版**序列：解包资源里只有 `AimJump*` 被重映射到目标骨架（`Stinger_3P_Textured_Skeleton`），
腰射版 `AR_M_1P_JumpStart/Jumploop/JumpLand` 只有原始 PSA 版本。第一人称前臂由双臂 IK 锁到武器握点，
躯干姿势差异不影响持枪观感，因此两组共用同一套序列；日后补齐腰射版映射时只需替换槽位，无需改代码。

## 技能动画接入

技能系统的结算本来就在 `UExtractionAbilities`（Revive / Heal / Smoke，服务器计时 + 复制），缺的只是表现层。
本轮补上，并且不碰任何结算逻辑：

- `Action` 改为 `ReplicatedUsing=OnRep_Action`；服务器在 `ServerBegin` 设定 `ActionEnd` 后、以及 `Cancel()` 里
  直接调用表现层，客户端由 RepNotify 触发，保证服务器、拥有者、远端看到同一套动作。
- `ABrokenEdgeCharacter` 新增 6 个蒙太奇槽位与 `PlayAbilityMontage(uint8, float)` / `StopAbilityMontage()`：
  `ActionKind` 与 `EExtractionAction` 取值一致（1 Revive / 2 Heal / 3 Smoke），避免角色头文件反向依赖撤离模块。
- **时长对齐**：短动作（治疗、烟雾）按 `蒙太奇长度 / 服务器时长` 设播放速率并钳制 `[0.5, 2.5]`；
  持续动作（救援，`ReviveSeconds = 5`）用 `Montage_SetNextSection(Section, Section)` 原地循环，结束由 `Action` 复位驱动停止。
- **医疗物品**（`UExtractionInteractionComponent::StartMedical` / `CancelMedical`）走同一套表现：
  `MedicalEnd` 改成 `ReplicatedUsing=OnRep_MedicalEnd`，拥有者客户端据此启停，`MedicalDuration` 来自物品的 `UseSeconds`。
- 1P 蒙太奇通过既有 `PlayFirstPersonMontage(View, Body)` + `SyncFirstPersonAction()` 逐帧对齐到 3P 蒙太奇位置，
  换弹/换枪用的是同一条通路，因此第一人称手臂 IK、武器跟随都不需要额外处理。

| 技能 | 3P 蒙太奇 | 1P 蒙太奇 | 长度 |
| --- | --- | --- | --- |
| Heal（治疗，`HealSeconds = 2`） | `AM_Ability_Heal_3P` | `AM_Ability_Heal_1P` | 1.401 s / 1.334 s |
| Revive（救援，`ReviveSeconds = 5`，循环） | `AM_Ability_Revive_3P` | `AM_Ability_Revive_1P` | 1.001 s / 1.001 s |
| Smoke（烟幕，`SmokeCastSeconds = 0.5`） | `AM_Ability_Smoke_3P` | `AM_Ability_Smoke_1P` | 见资产 |

蒙太奇资产在 `/Game/Characters/Stinger/Animations/Abilities/`，绑定写在两个角色蓝图上
（`BP_StingerCharacter`、`BP_StingerCharacter_Repaired`），策划可在编辑器里直接换。

## 第三人称瞄准左右偏移

`AO_Rifle` / `AO_RifleCrouching` 是 `AimOffsetBlendSpace1D`，只有俯仰轴，所以瞄准转向时上半身完全不动。
新建 2D 瞄准偏移需要重新采样 yaw 姿势，源 PSA 已不在本机，因此改为**程序化脊椎偏航**：

- `PreUpdate` 计算 `AimYaw = clamp(瞄准朝向 − 身体朝向, ±AimYawClamp) × AimYawScale`（默认 70° / 0.6）。
- `Update_AnyThread` 用 12 的插值速度平滑，避免视角微抖直接传到骨骼。
- `Evaluate_AnyThread` 在俯仰 additive 之后，把偏航平均摊到 `Spine`/`Spine1`/`Spine2` 三节，
  上半身转向瞄准方向、双腿保持原朝向。只在 `bAim` 且启用原版移动时生效，`Weight` 淡出时同步消失。

`AimYawScale = 0` 即可完全关闭，无需改代码。

## 第三视角冲刺姿势来回切换（bug 修复）

用户反馈：第三人称按住 Shift 奔跑时，动作在「持枪冲刺」和「普通持枪移动」之间反复跳。

先写了一个 PIE 采样测试复现（`BrokenEdge.Extraction.WeaponPIE.RifleLocomotionStability`），
逐帧读 `UBrokenEdgeAnimInstance::RifleLocomotionCoordinates`（Y 是速度轴，850 = 冲刺采样），实测：

```
修复前: frames=181 sprintPose=42  nonSprintPose=139 poseSwaps=3  Y=[0..850] v=[0..850]
        swap [t=0.34 Y=0 v=0] → [t=0.35 Y=850 v=17] → [t=0.37 Y=0 v=0]
修复后: frames=181 sprintPose=181 nonSprintPose=0   poseSwaps=0  Y=[850..850]
```

`enableSwaps=0`、`notSprinting=0` 说明节点没被禁用、`IsSprinting()` 也一直是 true，
所以问题不在状态判定，而在**坐标计算**：

| 缺陷 | 后果 |
| --- | --- |
| `bSprint` 要求 `Speed > 5`，且一旦不满足就把坐标从 850 直接写成 `min(Speed, 391)` | 撞墙、上台阶、被推挤、网络修正让速度瞬间掉到 0 时，姿势在冲刺与跑/待机之间硬切 |
| 冲刺采样只存在于 ±45°，超出后 850 在角度轴上外插 | 侧向或转向中冲刺会在 45° 边界反复跳回跑姿 |
| 坐标每帧硬赋值，没有过渡 | 即使只有一次判定变化，观感也是"跳" |

改法（`FAnimNode_RifleLocomotion`，只影响显示）：

- **状态驱动 + 保持时间**：`IsSprinting()` 成立期间锁定冲刺姿势，掉线后仍保留 `SprintPoseHoldSeconds`（默认 0.18 s），
  瞬时速度跌破阈值不再影响姿势；真正停步会先让 `IsSprinting()` 失效，再平滑退回。
- **方向钳制**：冲刺时把方向夹到 `±SprintDirectionLimit`（默认 45°），不再靠外插取姿势。
- **坐标平滑**：`SmoothedCoordinate` 用 `CoordinateInterpSpeed`（默认 12）插值，冲刺↔跑是过渡而不是硬切。

三个参数都在 `DA_RifleLocomotion` 上，可运行时调。测试里冲刺段 173 帧全为冲刺姿势、0 次切换；
松开 Shift 后过渡窗口外的切换次数为 0，且确实离开冲刺姿势。


## 翻越 / 攀爬

上一轮已经完成，本轮只做核对：`DA_TraversalAnimations` 的 13 条记录（Vault 60/90/120、Mantle 60/90/120/180、
Ladder 上/下、LadderEnter/LadderExit 的普通与顶部变体）全部指向已重映射的原版序列，1P/3P 均无空槽位。
`UExtractionTraversalComponent` 用 `PlaySlotAnimationAsDynamicMontage` 播 `DefaultSlot`，
与第一人称原生图、第三人称 AnimBP 的 Slot 都一致。

## 治疗可移动施放（移速降低）

需求：按 Z 治疗时不再强制站定，但移动要明显变慢。

技能结算在 `UExtractionAbilities`，之前 `TickComponent` 用一条通用规则判定：
速度 > 5 cm/s 或离起手点 > 2 cm 就 `Cancel()`。现在按动作区分：

| 动作 | 移动是否打断 | 原因 |
| --- | --- | --- |
| Heal（Z） | **不打断**，改为降速 | 治疗只作用于自己或范围内的队友，移动不影响生效 |
| Revive（E 长按） | 打断 | 离开 `ReviveRange` 就该失效，靠 `ValidTarget()` 每次重新校验 |
| Smoke（X） | 打断 | `SmokePoint` 在起手时按视线算好，移动会让落点失去意义 |
| 无动作 | 不打断 | 没有可取消的东西 |

判定抽成 `UExtractionAbilities::CancelledByMovement(Kind, bMoved)`，非 PIE 测试可直接断言。

- **降速**：`UExtractionRules::HealMoveSpeedScale`（默认 0.4，Config 可调）在
  `ABrokenEdgeCharacter::UpdateSprint()` 里乘到 `MaxWalkSpeed` 上。基础 600 cm/s → 240 cm/s，
  瞄准中 450 → 180。骨折减速仍在其后叠加。
- **治疗期间禁止冲刺**：`bSprinting` 条件里加了 `!bHealing`，避免"边治疗边 850 冲刺"把减速抹掉。
  松开治疗键后若仍按着 Shift，下一帧自动恢复冲刺。
- **动画**：第三人称 ABP 里已经有 `LayeredBoneBlend`（上下半身分层），蒙太奇走的是上半身层，
  所以边走边治疗时腿部继续走正常移动、上半身播放医疗枪动作，不需要改动画图。
  第一人称只有前臂可见，蒙太奇走 `DefaultSlot`、手臂由 IK 跟随时序联动，同样无副作用。
- **HUD**：热键提示改成 `Z 治疗(可移动)`，施放中显示 `正在治疗 · 可移动，移速降低 · X.X 秒`。

## 攀爬/翻越朝向对齐障碍表面（bug 修复）

用户反馈：翻越箱体时人物朝向与障碍不符，身体斜着切过箱体边缘。

`UTraversalMovementComponent::TryObstacle()` 原本用**相机朝向**当攀爬朝向：

```cpp
const FVector Facing = FRotator(0, CharacterOwner->GetControlRotation().Yaw, 0).Vector();
...
if (FVector::DotProduct(Front.ImpactNormal, -Facing) < .5f) return false;   // 允许偏差 60°
```

也就是说，只要视角与障碍面法线夹角在 60° 以内就能触发，而触发后角色仍按相机朝向去翻越。
斜着撞箱体时，角色会沿视线斜切过边缘，动画本身是"正对障碍翻越"，于是朝向与障碍明显脱节。

改法：命中后把朝向对齐障碍表面的**水平法线**，相机朝向只用来决定"往哪边探"。

```cpp
const FVector Facing = SnapFacingToNormal(Camera, Front.ImpactNormal);
```

`SnapFacingToNormal` 是新增的静态函数：取 `-ImpactNormal` 的水平分量并归一化；
法线接近竖直（顶面/底面命中）时退回首选朝向，避免零向量。之后的高度探测、翻越远端探针、
落点都统一用这个朝向，角色一定正对障碍面攀爬。

验证：`BrokenEdge.Traversal.Facing` 覆盖正对/斜撞/未归一化法线/竖直法线四种情况；
`BrokenEdge.Traversal.EntryCapsule` 回归通过。

> 未验证的部分：动画序列本身的根骨是否带旋转。`DA_TraversalAnimations` 里 13 条记录的
> `enable_root_motion` 全为 0，根骨旋转会直接体现在姿势上，但 Python 侧的 `AnimSequence`
> 没有暴露取骨接口（`get_bone_transform` 不存在），本轮无法采样确认。若对齐法线后仍有朝向偏差，
> 下一步应在 C++ 里加一个取根骨 yaw 的调试接口再判断。

## 全量重置动画资产（2026-10-27 第二轮）

要求：用解包源资产完全重置一遍蜂医动画，只重置资产层、保留已调好的接入配置。

### 源与前置

源在共享盘 `\\Desktop-m9pa1sf\三角洲解包资产`（主机名解析到 `192.168.1.111`），
`蜂医的人物模型和相关动画资产` 下四个分组：`01_完整人物模型`(5 GLB)、`02_蜂医角色动画`(100 PSA)、
`03_蜂医技能动画`(157 PSA)、`04_通用男性动作`(186 PSA)，合计 **443 PSA**，与历史记录完全一致。

抽样 12 个 PSA 做 sha256 比对历史清单（`Artifacts/Stinger/RepairImportManifest.json`）：**12/12 一致**，
说明这就是当初导入的同一份源，不是换了游戏版本。

脚本原先写死 `D:\司晨\源代码和素材`（已删除），本轮把它们改成可配置，便于以后盘符再变：

| 脚本 | 变量 |
| --- | --- |
| `Scripts/Stinger/import_assets.py` | `STINGER_SOURCE` |
| `Scripts/Stinger/repair_import.py` | `STINGER_REPAIR_SOURCE`（原有） |
| `Scripts/SpaceCenter/source_packages.py` | `DELTA_SOURCE_ZIPS` |
| `Scripts/SpaceCenter/sample_config.json` | `source` 直接改到共享盘 |

### 回滚点

`Content/Characters/Stinger/Animations` 复制到 `D:\BrokenEdge_AnimBackup_20261027`
（1703 文件 / 3097.7 MB，与源目录计数一致）。

### 重建

重置的实质是重跑 `repair_import.py`——它才是运行时资产的生成器（`Corrected/Source` + `Corrected/MappedV4`，
即 `DA_RifleLocomotion`、`DA_TraversalAnimations`、技能蒙太奇引用的那一套）。
`Imported/` 是 v2 旧解码产物，运行时已不引用，本轮不重建。

```powershell
$env:STINGER_REPAIR_SOURCE='\\Desktop-m9pa1sf\三角洲解包资产\蜂医的人物模型和相关动画资产'
$env:STINGER_REPAIR_NAMES='*'
$env:STINGER_REPAIR_REPORT='D:/project_Lu/Server/BrokenEdge/Saved/Stinger/AnimationOptimization/repair_full.json'
& "D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/project_Lu/Server/BrokenEdge/BrokenEdge.uproject" `
    -run=pythonscript -script="<绝对路径>/Scripts/Stinger/repair_import.py" -NullRHI -unattended -nosound
```

生成参数沿用原版：`mapping_revision=4`、`fixed_limb_translations=0`（即目录名 `MappedV4`），不设 `STINGER_REPAIR_FIXED_LIMBS`。
换新的 `STINGER_REPAIR_REPORT` 是必须的——脚本会按旧报告里的 sha256 跳过，否则一次都不会重建。

### 结果

| 检查 | 结果 |
| --- | --- |
| 重建 | **443/443 written，0 失败** |
| 新旧清单对比 `compare_repair_manifests.py` | `ok=True`：443/443 条目一一对应，序列集合/帧数/源帧率/资产路径全一致，0 偏差 |
| 资产落盘变化 | 311 个 uasset 被原地重写，目录体积 3102 MB（备份 3097.7 MB，无增删） |
| `verify_animation_optimization.py` | 54/54 |
| `verify_view_transitions.py` | 41/41 |
| `BrokenEdge.Extraction.WeaponPIE`（3 个用例） | 全部 Success；冲刺姿势 181/181、0 次切换 |

清单对比能跑到 `ok=True` 是个强证明：资产路径由相对路径哈希决定，路径不变说明 443 条序列的分组/命名
一个都没变，接入层（14 个移动槽位、13 条翻越、6 个蒙太奇）全部自动重连，不需要手工重绑。

### 没重置的部分：移动/瞄准那套

`DeltaForceLocomotion` 下的 1P/3P 移动序列、`BS_RifleStanding/Crouching`、`AO_Rifle*`、`DA_RifleLocomotion`
来自另一条管线：`convert_locomotion.ps1` 读 `原始资源ZIP/02_武器_011.zip`，经 CUE4Parse 导出 PSA 再导入。
**本轮没跑，因为工具链缺件**：

- `Saved/SpaceCenter/Tools/CUE4Parse/CUE4Parse-Natives` 未构建
- `Scripts/SpaceCenter/Converter/bin/Release/net10.0/` 里没有 `CUE4Parse-Natives.dll`
- 本机没有 `cmake`（`convert_locomotion.ps1` 第一步就要用它编译原生 ACL 依赖）

武器包是 Oodle 压缩的，去掉原生库无法解压。补这一步需要先装 cmake，再跑
`Scripts/SpaceCenter/bootstrap.ps1` + `Scripts/Stinger/convert_locomotion.ps1`。
这一步不影响上面 443 条序列的重置结果。

## 验证

| 检查 | 结果 |
| --- | --- |
| `BrokenEdgeEditor Win64 Development` 编译 | Succeeded，0 error |
| 全量资产校验 `verify_animation_optimization.py` | **54/54**：5 个 1P 跳跃/开镜槽位、瞄准偏航参数、两个蓝图各 6 个技能蒙太奇、翻越动画集 13 条全部绑定且骨骼一致 |
| 冷启动绑定校验 `verify_view_transitions.py` | 41/41 |
| 绑定幂等 `configure_view_transitions.py` 二次运行 | `changed=0 unpersisted=0` |
| 自动化 `BrokenEdge.Extraction.Abilities.MovementRules`（新增） | Success：治疗移动不取消、救援/烟雾移动仍取消、`HealMoveSpeedScale ∈ (0,1)` |
| 自动化 `BrokenEdge.Traversal.Facing`（新增） | Success：正对/斜撞/未归一化法线/竖直法线四种情形 |
| 自动化 `BrokenEdge.Traversal.EntryCapsule` | Success（回归） |
| 自动化 `BrokenEdge.Extraction.WeaponPIE.InputAndAssembly` | Success |
| 自动化 `BrokenEdge.Extraction.WeaponPIE.SprintTransitions` | Success |
| 自动化 `BrokenEdge.Extraction.WeaponPIE.RifleLocomotionStability`（新增） | Success：冲刺 173/173 帧为冲刺姿势、0 次切换；松开后过渡窗口外 0 次切换 |

其中 `InputAndAssembly` 第 1 步在 Shift+W 冲刺 0.8 秒后断言
`CurrentVisualState == "Sprint" && bLocomotionMotion && !bMissingStateAnimation`，
正是本轮改动直接影响的状态字段；新旧行为下该断言均需成立，因此它同时起到回归作用。

治疗移动规则没有做成 PIE 测试：`UExtractionCombatComponent::CanAct()` 在没有 `AExtractionGameState`
的独立测试地图上对普通玩家直接返回 false，技能根本起不来。所以这条规则用非 PIE 断言覆盖，
移速改动的回归则交给既有的 `WeaponPIE.SprintTransitions`（它全程校验冲刺与移速）。

> 本轮最后一次编译时编辑器是关闭的，产物直接写回 `Binaries/Win64/UnrealEditor-BrokenEdge.dll`，
> 因此重启编辑器即可加载全部改动，不存在残留的 `-000N` 热重载模块。

证据：`Saved/Stinger/AnimationOptimization/ConfigureViewTransitions.json`、`VerifyViewTransitions.json`、
`VerifyAll.json`、`ConfigureAbilityMontages.json`、`WeaponPIE4.log`、`AbilityRules2.log`、
`LocomotionStability3.log`。

## 复现

```powershell
./Scripts/Stinger/audit_animation_matrix.py            # 资产盘点（纯本地，产出 Artifacts/Stinger/AnimationInventory.*）
& "D:/UE_5.8/Engine/Build/BatchFiles/Build.bat" BrokenEdgeEditor Win64 Development `
    "D:/project_Lu/Server/BrokenEdge/BrokenEdge.uproject" -WaitMutex -NoUBA
# 以下三条会写资产，必须关闭编辑器后执行
& "D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/project_Lu/Server/BrokenEdge/BrokenEdge.uproject" `
    -run=pythonscript -script="<绝对路径>/Scripts/Stinger/configure_view_transitions.py" -NullRHI -unattended -nosound
& "D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/project_Lu/Server/BrokenEdge/BrokenEdge.uproject" `
    -run=pythonscript -script="<绝对路径>/Scripts/Stinger/configure_ability_montages.py" -NullRHI -unattended -nosound
# 校验只读，可以和编辑器并存
& "D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/project_Lu/Server/BrokenEdge/BrokenEdge.uproject" `
    -run=pythonscript -script="<绝对路径>/Scripts/Stinger/verify_animation_optimization.py" -NullRHI -unattended -nosound
```

写资产的脚本在编辑器打开时**不会**报错退出，而是保存失败（Windows 共享冲突 `Error Code 32`，
日志里是 `MoveFile was unable to move ...uasset`）。本项目改蓝图 CDO 后必须用
`save_loaded_asset(blueprint, only_if_is_dirty=False)`，默认的 `only_if_is_dirty=True` 会因为
CDO 修改不标记包脏而直接返回 False。

## 回退

- 第一人称跳跃/开镜：清空 `DA_RifleLocomotion` 的 5 个 `View*` 槽位即可回退，代码无需改动。
- 技能表现：清空两个角色蓝图的 6 个 `*Montage` 槽位，`PlayAbilityMontage` 会直接返回，结算不受影响。
- 瞄准偏航：`DA_RifleLocomotion.AimYawScale = 0`。
- 冲刺姿势：`SprintPoseHoldSeconds = 0` 且 `CoordinateInterpSpeed = 0` 会回到修复前的硬切行为
  （保留是为了对照，不建议这么做）；`SprintDirectionLimit` 控制冲刺方向钳制角度。
- 治疗移动：`UExtractionRules::HealMoveSpeedScale = 1` 即取消降速（仍可边走边治疗）；
  要恢复"必须站定"，把 `CancelledByMovement` 里的 Heal 例外去掉即可。
- 攀爬朝向：把 `SnapFacingToNormal` 换回 `return Preferred;` 即恢复按相机朝向攀爬。
- 翻越：`DA_TraversalAnimations` 未改动，无需回退。

## 未做与阻塞项

- **趴下**：**缺资产，无法完成**。工程里只有 1P 趴下全套（`Idle2Prone`、`Prone2Idle`、`Prone2Crouch`、
  `ProneMove_Start`、`Prone_Movef/b/L/R`，均已重映射），**没有步枪 3P 趴下序列**（`AR_M_3P_Prone*` 不存在，
  现有 3P 趴下全是蜂巢烟雾/烟幕弹/医疗枪的武器专用姿势）。源 PSA 目录 `D:\司晨\源代码和素材` 已不在本机，
  无法重新映射。另外项目也没有趴下移动状态（`EExtractionTraversal` 只有 Vault/Mantle/Ladder*）。
  要补需要先拿回原始资源包。
- **蜂巢烟雾（HoneycombSmoke）**：没有任何玩法载体——`EExtractionAbilities` 只有 Revive/Heal/Smoke 三种动作，
  Smoke 用的是烟幕弹（`SmokeScreen_M_*`）。序列已重映射可直接用，但要新增一个设备技能才能接。
- **蹲伏 1P**：第一人称只有前臂，蹲下不改变可见内容；3P 蹲伏由 `BS_RifleCrouching` 覆盖，故不处理。
  `Idle2Crouch` / `Crouch2Idle` 仍只有原始 PSA 版本。
- `Scripts/Stinger/audit_locomotion.py` 依赖 `Saved/Stinger/Locomotion/Import.json`，该文件当前不存在
  （Saved 被清理过），冷启动校验改用本轮的独立脚本完成。
