# BrokenEdge 武器手部 IK 问题诊断与修复指南

> 当前项目：`C:\UnrealProjects\BrokenEdge`
>
> 参考项目：`D:\SourceFiles\UnrealProjects\UE5BlasterLearning-main`
>
> 引擎版本：Unreal Engine 5.5.4
>
> 检查日期：2026-08-25
>
> 文档性质：静态源码与资产检查结果、修复建议和验收步骤

## 1. 结论先行

BrokenEdge 当前的手部 IK 问题不属于 `BrokenEdgePlayerController.cpp`。实际链路位于武器骨架、`UBrokenEdgeAnimInstance` 和 `ABP_BrokenEdgeAnimInstance`。

已确认的主要问题如下：

1. 当前狙击枪不存在 `LeftHandSocket`，但 C++ 每帧都读取这个 Socket；
2. Socket 不存在时，UE 会退回武器组件自身的变换，导致左手 FABRIK 被拉向武器原点，而不是护木握持位置；
3. 当前动画蓝图中的右手 `Transform (Modify) Bone` 节点使用 `Rotation Mode = Ignore`，因此计算出来的 `RightHandRotation` 不会真正作用到右手骨骼；
4. `bUseFABRIK`、`bUseAimOffsets` 和 `bTransformRightHand` 的更新被放在“武器有效”判断内部，武器失效或卸下后可能残留上一帧状态；
5. 参考项目通过 `bLocallyController` 限制本地右手瞄准变换，当前项目删除了该门控；如果直接启用右手旋转，远程角色可能使用未更新的默认旋转。

修复优先级建议：

```text
补齐 LeftHandSocket
    -> 校验 FABRIK 节点
    -> 启用右手 Replace Existing Rotation
    -> 恢复本地控制门控
    -> 修正 C++ 中 IK 开关的更新范围
    -> Standalone、Listen Server、Client 分别验收
```

## 2. IK 的实际执行链

当前项目的武器手部 IK 链路是：

```text
CombatComponent
  -> 把 Weapon Actor 挂到角色 RightHandSocket
  -> Character 持有 EquippedWeapon
  -> BrokenEdgeAnimInstance 每帧读取 EquippedWeapon
      -> 从武器读取 LeftHandSocket 世界变换
      -> 转换到角色 hand_r 骨骼空间
      -> 写入 LeftHandTransform
  -> ABP_BrokenEdgeAnimInstance
      -> FABRIK 驱动 upperarm_l ... hand_l
      -> Transform (Modify) Bone 驱动 hand_r
```

相关源码位置：

- [`CombatComponent.cpp`](../Source/BrokenEdge/Component/CombatComponent.cpp#L293)：把武器挂到角色右手；
- [`BrokenEdgeAnimInstance.cpp`](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.cpp#L78)：计算左右手 IK 数据；
- [`BrokenEdgeAnimInstance.h`](../Source/BrokenEdge/Character/BrokenEdgeAnimInstance.h#L100)：向动画蓝图暴露 IK 变量；
- `Content/Blueprints/Character/Animation/ABP_BrokenEdgeAnimInstance.uasset`：消费这些变量并修改骨骼姿势。

[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp) 中没有 IK、FABRIK、左右手 Socket 或 AnimInstance 相关调用，因此不应该在 PlayerController 中修复该问题。

## 3. 当前项目与参考项目对照

### 3.1 已检查结果

| 检查项 | BrokenEdge | 参考项目 | 判断 |
| --- | --- | --- | --- |
| 左手变换计算 | 读取 `LeftHandSocket` | 同样读取 `LeftHandSocket` | C++ 主算法一致 |
| 狙击枪 `LeftHandSocket` | 不存在 | 存在 | 当前项目左手 IK 的直接问题 |
| 角色持枪 Socket | `RightHandSocket` | `RightHandSocket` | 挂枪入口一致 |
| FABRIK 节点数量 | 1 个 | 资产中存在 FABRIK 节点 | 结构存在 |
| FABRIK Effector Space | Bone Space | 参考资产包含对应 Bone Space 配置 | 当前空间设置没有明显问题 |
| FABRIK Precision | `1.0` | 未完成逐属性运行时读取 | 当前值合理 |
| FABRIK Max Iterations | `10` | 未完成逐属性运行时读取 | 当前值合理 |
| 右手 Modify Bone Rotation Mode | `Ignore` | 参考资产包含 `BMM_Replace` | 当前右手旋转节点没有生效 |
| `bLocallyController` | C++ 声明和赋值被注释，ABP 不再引用 | C++ 设置为 `true`，ABP 引用 | 当前缺少本地玩家门控 |
| IK 开关更新位置 | 武器有效判断内部 | 武器有效判断外部 | 当前可能残留旧状态 |

### 3.2 当前狙击枪实际 Socket 列表

当前 `BP_SniperRifle` 使用：

```text
/Game/MilitaryWeapSilver/Weapons/Sniper_Rifle_A
```

`WeaponMesh` 的相对变换为：

```text
Location = (0, 0, 0)
Rotation = (Pitch=0, Yaw=0, Roll=0)
Scale    = (1, 1, 1)
```

当前网格体能够查询到的 Socket 和骨骼名称包括：

```text
AmmoEject
MuzzleFlash
AmmoSocket
Root_Bone1
Grip_Bone
Clip_Bone
Slide_Bone
Trigger_Bone
Ammo
```

以下两个名称不存在：

```text
LeftHandSocket = false
Hand_R         = false
```

`BP_SniperRifle2` 当前会解析到 `BP_SniperRifle`，不是另一套独立的狙击枪 Blueprint 配置。

### 3.3 为什么缺少 Socket 会把左手拉错

当前代码：

```cpp
LeftHandTransform = EquippedWeapon->GetWeaponMesh()->GetSocketTransform(
    FName("LeftHandSocket"),
    ERelativeTransformSpace::RTS_World);
```

UE 5.5.4 的 `USceneComponent::GetSocketTransform()` 约定是：

```text
找到 Socket 或 Bone
    -> 返回对应变换

找不到 Socket 或 Bone
    -> 返回组件自身的变换
```

因此当前数据流实际变成：

```text
不存在的 LeftHandSocket
    -> WeaponMesh 世界变换
    -> 转换到 hand_r 骨骼空间
    -> FABRIK 把 hand_l 拉向武器原点
```

这不是 FABRIK 求解器算错，而是传给求解器的 Effector 目标点错误。

## 4. 修复左手 FABRIK

### 4.1 给狙击枪添加 LeftHandSocket

1. 在内容浏览器打开 `Sniper_Rifle_A`；
2. 显示 Skeleton Tree；
3. 找到 `Root_Bone1`；
4. 右键该骨骼，选择“添加 Socket”；
5. 将 Socket 严格命名为 `LeftHandSocket`，注意大小写；
6. 把 Socket 移到狙击枪护木上左手应该握住的位置；
7. 调整 Socket 旋转，让左手掌心方向与枪身贴合；
8. 保存对应的 `Sniper_Rifle_A_Skeleton`。

推荐把 Socket 挂在武器根骨骼上，然后使用相对位置和相对旋转摆放。若挂在会播放局部动画的弹匣、枪栓或扳机骨骼上，左手会随着这些局部骨骼错误移动。

### 4.2 不要通过角色 RightHandSocket 修正左手

角色骨架中的 `RightHandSocket` 负责整把武器相对于右手的挂接姿势。旋转它会同时影响：

- 枪身位置；
- 枪口位置；
- 左手 IK 目标；
- 其他使用同一挂点的武器。

所以应当分工处理：

```text
RightHandSocket     调整整把武器怎样被右手握住
LeftHandSocket      调整左手握住武器的具体位置
MuzzleFlash         提供枪口位置
AmmoEject           提供抛壳位置
```

不要用旋转角色 `RightHandSocket` 的方式代替 `LeftHandSocket`。

### 4.3 多武器项目必须逐把配置

`LeftHandSocket` 属于武器骨架资产，不会自动从一把枪复制到另一把枪。应逐个检查：

- Assault Rifle；
- Sniper Rifle；
- Shotgun；
- Rocket Launcher；
- Grenade Launcher；
- Pistol 或双持武器。

参考项目的多种武器骨架都包含 `LeftHandSocket`；当前项目扫描到的武器资产没有该名称，因此不应只验证狙击枪。

## 5. 校验动画蓝图 FABRIK 节点

打开：

```text
/Game/Blueprints/Character/Animation/ABP_BrokenEdgeAnimInstance
```

在 AnimGraph 中找到 `Transform Hands` 状态和 FABRIK 节点，建议使用以下配置：

| 属性 | 推荐值 |
| --- | --- |
| Root Bone | `upperarm_l` |
| Tip Bone | `hand_l` |
| Effector Transform | `LeftHandTransform` |
| Effector Transform Space | Bone Space |
| Effector Transform Bone | `hand_r` |
| Precision | `1.0` |
| Max Iterations | `10` |
| Alpha 或启用条件 | `bUseFABRIK` |

为什么使用 `hand_r` Bone Space：

1. 武器挂在右手；
2. C++ 把武器上的左手 Socket 世界坐标转换到了 `hand_r` 骨骼空间；
3. FABRIK 必须用相同的参考空间解释 `LeftHandTransform`；
4. 如果动画蓝图改成 Component Space 或 World Space，数值语义就不再一致。

对应 C++ 转换代码：

```cpp
BrokenEdgeCharacter->GetMesh()->TransformToBoneSpace(
    FName("hand_r"),
    LeftHandTransform.GetLocation(),
    FRotator::ZeroRotator,
    OutPosition,
    OutRotation);
```

## 6. 修复右手 Transform Bone

### 6.1 当前问题

当前动画蓝图中的 `Transform (Modify) Bone` 节点已确认使用：

```text
Translation Mode = Ignore
Rotation Mode    = Ignore
Scale Mode       = Ignore
```

`Rotation Mode = Ignore` 表示节点不会采用 `RightHandRotation`，即使 C++ 每帧已经正确计算该变量。

### 6.2 推荐配置

在动画蓝图中选择右手 `Transform (Modify) Bone` 节点，设置：

| 属性 | 推荐值 |
| --- | --- |
| Bone to Modify | `hand_r` |
| Translation Mode | Ignore |
| Rotation Mode | Replace Existing |
| Rotation Space | Bone Space |
| Rotation | `RightHandRotation` |
| Scale Mode | Ignore |
| Alpha 或启用条件 | `bTransformRightHand` |

建议先只启用 Rotation，不修改 Translation 和 Scale，避免把右手从动画原本的位置拉走。

### 6.3 Hand_R 名称风险

当前 C++ 从武器网格读取：

```cpp
FTransform RightHandTransform =
    EquippedWeapon->GetWeaponMesh()->GetSocketTransform(
        FName("Hand_R"),
        ERelativeTransformSpace::RTS_World);
```

但当前狙击枪没有 `Hand_R` Socket 或同名骨骼。UE 会再次退回武器组件变换。

这里有两种处理方式：

#### 方案 A：从角色的 hand_r 骨骼读取，推荐

```cpp
const FTransform RightHandTransform =
    BrokenEdgeCharacter->GetMesh()->GetSocketTransform(
        FName("hand_r"),
        ERelativeTransformSpace::RTS_World);
```

右手本身属于角色骨架，从角色 Mesh 读取语义更直接，也不要求每把武器都额外创建 `Hand_R`。

#### 方案 B：给每把武器添加 Hand_R Socket

可以在武器骨架根部添加 `Hand_R`，但必须保证所有武器都具有完全相同的命名和正确位置，否则换枪后右手瞄准旋转会不一致。

参考项目源码也从武器读取 `Hand_R`，所以这属于参考实现本身也值得复核的风险，不应仅因为参考项目这样写就认为一定正确。

## 7. 修正 IK 开关的生命周期

### 7.1 当前问题

当前代码把这些状态更新放在武器有效判断内部：

```cpp
bUseFABRIK
bUseAimOffsets
bTransformRightHand
```

当角色上一帧持有武器、下一帧丢弃武器或武器引用短暂为空时，这些变量不会重新计算，可能继续保留 `true`。

可能出现的表现包括：

- 卸下武器后左臂仍保持持枪 IK；
- 换枪期间姿势短暂跳变；
- 淘汰或重生阶段继续修改手部骨骼；
- 网络客户端因复制到达顺序不同，出现偶发姿势差异。

### 7.2 推荐写法

先统一得到“当前是否存在有效武器”：

```cpp
const bool bHasValidWeapon =
    bWeaponEquipped &&
    EquippedWeapon &&
    EquippedWeapon->GetWeaponMesh();
```

只有变换计算需要放在 `if` 内：

```cpp
if (bHasValidWeapon)
{
    // 计算 LeftHandTransform 和 RightHandRotation
}
```

开关应当每帧明确赋值：

```cpp
const bool bCombatAllowsIK =
    BrokenEdgeCharacter->GetCombatState() == ECombatState::ECS_Unoccuiped;

bUseFABRIK = bHasValidWeapon && bCombatAllowsIK;

if (BrokenEdgeCharacter->IsLocallyControlled() &&
    BrokenEdgeCharacter->GetCombatState() != ECombatState::ECS_ThrowingGrenade &&
    BrokenEdgeCharacter->bFinishedSwapping)
{
    bUseFABRIK = bHasValidWeapon &&
                 !BrokenEdgeCharacter->IsLocallyReloading();
}

bUseAimOffsets =
    bHasValidWeapon &&
    bCombatAllowsIK &&
    !BrokenEdgeCharacter->GetDisableGameplay();

bTransformRightHand =
    bHasValidWeapon &&
    BrokenEdgeCharacter->IsLocallyControlled() &&
    bCombatAllowsIK &&
    !BrokenEdgeCharacter->GetDisableGameplay();
```

这里把 `IsLocallyControlled()` 直接加入 `bTransformRightHand`，可以代替参考项目中的 `bLocallyController` 动画蓝图门控。两种方案选择一种即可，不要同时维护两套含义相同的状态。

## 8. 与枪模型朝向的关系

枪模型朝向和左手 IK 是相关但不同的两层：

```text
模型轴向错误
    -> 整把枪、枪口和所有 Socket 的空间方向可能不正确

LeftHandSocket 缺失或位置错误
    -> 主要影响左手握持位置

FABRIK 空间设置错误
    -> 正确的 Socket 坐标会被按照错误空间解释
```

如果修改了 `Sniper_Rifle_A` 的导入旋转或烘焙了模型变换，必须重新检查：

- `LeftHandSocket`；
- `MuzzleFlash`；
- `AmmoEject`；
- 武器相对于角色 `RightHandSocket` 的姿势。

不要只看 Skeletal Mesh 编辑器里的枪身方向。最终验收必须放到角色持枪动画中完成。

## 9. 推荐修复顺序

### 第一步：只修左手目标点

1. 添加 `LeftHandSocket`；
2. 保持现有 FABRIK Bone Space；
3. 在角色持枪预览中摆放 Socket；
4. 验证左手是否稳定握住护木。

这一步完成前不要通过 FABRIK Alpha、Precision 或 Max Iterations 掩盖问题。目标点错误时，提高迭代次数不会得到正确握持位置。

### 第二步：修右手旋转

1. 将 Modify Bone 的 Rotation Mode 改为 Replace Existing；
2. Bone to Modify 设置为 `hand_r`；
3. Rotation Space 设置为 Bone Space；
4. 添加本地控制门控；
5. 检查向上、向下和左右瞄准时手腕是否翻转。

### 第三步：修 C++ 状态残留

1. 把 IK 开关移出武器有效判断；
2. 每帧明确写入 `true` 或 `false`；
3. 分别验证换弹、换枪、扔雷、丢枪和淘汰状态。

## 10. 调试方法

### 10.1 动画蓝图预览

在 `ABP_BrokenEdgeAnimInstance` 中观察：

```text
bWeaponEquipped
bUseFABRIK
bTransformRightHand
LeftHandTransform
RightHandRotation
```

判断方法：

| 现象 | 优先检查 |
| --- | --- |
| 左手被拉到右手或枪托附近 | `LeftHandSocket` 是否存在 |
| 左手位置正确但角度不自然 | Socket Rotation、FABRIK Rotation Source |
| 左手完全不动 | `bUseFABRIK`、FABRIK Alpha、状态机路径 |
| 右手不跟随准星 | Modify Bone Rotation Mode 是否仍为 Ignore |
| 本地正常、远端手腕异常 | 本地控制门控、远程角色的 RightHandRotation |
| 换枪瞬间手臂跳变 | CombatState、bFinishedSwapping、Socket 配置差异 |

### 10.2 临时存在性检查

调试阶段可以加入：

```cpp
const bool bHasLeftHandSocket =
    EquippedWeapon->GetWeaponMesh()->DoesSocketExist(FName("LeftHandSocket"));

ensureMsgf(
    bHasLeftHandSocket,
    TEXT("Weapon %s does not contain LeftHandSocket"),
    *GetNameSafe(EquippedWeapon));
```

该检查适合开发阶段定位漏配武器。正式版本可以保留非致命日志，也可以在完成全部武器资产验收后移除。

## 11. 验收清单

### 11.1 单机表现

- [ ] 未装备武器时双手保持普通移动动画；
- [ ] 装备狙击枪后左手握住护木；
- [ ] Idle、Run、Crouch、Aim 状态下左手不漂移；
- [ ] 上下瞄准时右手和枪口朝向合理；
- [ ] 换弹期间 FABRIK 按设计关闭；
- [ ] 换弹完成后 FABRIK 恢复；
- [ ] 换枪过程中左右手没有明显跳变；
- [ ] 丢弃武器后 IK 状态被清零；
- [ ] 淘汰后不继续应用持枪 IK。

### 11.2 多武器表现

- [ ] Assault Rifle 的 `LeftHandSocket` 已配置；
- [ ] Sniper Rifle 的 `LeftHandSocket` 已配置；
- [ ] Shotgun 的 `LeftHandSocket` 已配置；
- [ ] Rocket Launcher 的 `LeftHandSocket` 已配置；
- [ ] Grenade Launcher 的 `LeftHandSocket` 已配置；
- [ ] 手枪或双持武器使用独立策略，不强行套用步枪握持点。

### 11.3 联机表现

至少使用 Listen Server 加一个 Client，分别观察：

| 观察对象 | 需要确认 |
| --- | --- |
| Listen Server 本地角色 | 左手 FABRIK 和右手瞄准正常 |
| Client 自己的角色 | 本地右手瞄准正常 |
| Client 看到的服务器角色 | 没有默认零旋转导致的手腕翻转 |
| Server 看到的 Client 角色 | 武器挂接和左手握持稳定 |
| 第二个 Client 看到的第一个 Client | Simulated Proxy 表现正常 |

仅在 AnimBlueprint 预览窗口中正确，不等于多人运行时正确。

## 12. 检查边界与当前状态

本次已完成：

- 当前项目与参考项目相关 C++ 调用链对比；
- 当前 `BP_SniperRifle` 的 `WeaponMesh`、网格体引用和相对变换检查；
- 当前狙击枪 Socket 存在性检查；
- 当前动画蓝图 FABRIK 和 Modify Bone 核心属性检查；
- 参考项目武器骨架中的 `LeftHandSocket` 资产证据检查；
- `BrokenEdgePlayerController.cpp` 与 IK 无关的确认。

尚未完成：

- 没有修改 C++ 或 Blueprint 业务逻辑；
- 没有替用户添加或摆放 `LeftHandSocket`；
- 没有运行 Standalone 或 PIE；
- 没有执行 Listen Server、Client 或 Dedicated Server 动画验收；
- 参考项目因当前引擎缺少可加载的 `SteamShared` 二进制，未完成其动画蓝图全部节点属性的命令行逐项读取；参考侧结论来自源码和资产中可读取的类名、变量名、Socket 名及枚举配置证据。

因此本文记录的是已经定位的静态问题和推荐修复方案，不应被表述为“IK 已修复”或“多人表现已验证”。
