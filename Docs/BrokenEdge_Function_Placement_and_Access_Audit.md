# BrokenEdge 函数位置、访问权限与网络权限审查

> 审查对象：`Source/BrokenEdge` 当前源码
>
> 审查版本：Unreal Engine 5.5.4
>
> 审查日期：2026-08-12
>
> 审查目标：评估函数位于哪个类、哪个访问区段，以及 C++、Blueprint 和多人网络权限是否合理。

## 1. 结论

当前代码能够通过 UHT、编译和链接，但函数边界仍处于教学项目的早期搭建阶段。主要问题不是语法，而是四种权限混在了一起：

1. C++ 的 `public/protected/private`；
2. Blueprint 的可见与可写权限；
3. 多人游戏中的 Server、Owning Client、Simulated Proxy 权限；
4. 类职责边界，即一个函数究竟应该放在 Character、Component、Controller、HUD 还是 Widget。

综合评价：**4.7 / 10，能编译，但封装和网络边界需要在继续扩展战斗系统前收紧。**

| 维度 | 评分 | 结论 |
|---|---:|---|
| C++ 访问控制 | 5/10 | 多个内部函数和可变状态公开，读接口与写接口没有分离 |
| 头文件函数分组 | 5/10 | 多次穿插 `public/private/protected`，同一职责被拆散 |
| Blueprint 暴露 | 6/10 | AnimInstance 的私有只读变量处理较好，但组件指针大量使用 `EditAnywhere` |
| 网络 Authority 与复制 | 3/10 | 3 个复制属性未注册，部分状态可在客户端直接写入 |
| 类职责划分 | 5/10 | Character、Controller、HUD、Widget 之间存在穿透式访问 |
| 测试代码隔离 | 4/10 | `test` 代码编入 Runtime 模块，并已产生真实编译警告 |

## 2. 审查边界

本次检查了：

- 22 个头文件；
- 19 个 `.cpp` 文件；
- Character、AnimInstance、Buff、Combat、Weapon、Pickup、PlayerController、HUD、Widget、GameMode；
- `Source/BrokenEdge/test` 下的三个测试类；
- `UPROPERTY`、`UFUNCTION`、RPC、`OnRep`、`DOREPLIFETIME` 和主要调用点。

本文只做审查和整改建议，没有修改业务源码或 Blueprint 资产。

## 3. 先区分四种“权限”

### 3.1 C++ 访问权限

```text
public    任何持有对象指针的 C++ 调用方都能调用
protected 当前类和派生类能调用
private   仅当前类能调用；friend 仍可绕过
```

这只约束 C++ 编译期访问。把函数写成 `private`，不会自动阻止网络客户端改变本地状态。

### 3.2 Blueprint 权限

`BlueprintReadOnly`、`BlueprintReadWrite`、`BlueprintCallable` 与 `AllowPrivateAccess` 决定蓝图能否看见或调用成员，与 C++ 的访问标签不是一回事。

例如，AnimInstance 中这种写法是合理的：

```cpp
private:
    UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
    float Speed;
```

蓝图只能读取，C++ 状态仍由 AnimInstance 自己维护。

### 3.3 网络权限

多人游戏中的关键边界是：

```text
客户端输入
    -> Server RPC
        -> 服务器验证并修改权威状态
            -> 属性复制 / Multicast
                -> 客户端 OnRep 更新表现
```

`public` 不代表允许客户端修改，`private` 也不代表服务器专用。服务器权限必须由 RPC、`HasAuthority()`、复制注册和调用链共同保证。

### 3.4 类职责权限

即使调用在语法和网络上都合法，也可能放错类。例如 Widget 控件应由 Widget 自己封装，PlayerController 不应逐层访问：

```cpp
BrokenEdgeHUD->CharacterOverlay->HealthBar->SetPercent(...);
```

这种访问把 Controller、HUD 和具体控件结构绑死在一起。

## 4. P0：必须优先处理的问题

### 4.1 三个复制属性未注册

[`BrokenEdgeCharacter.h`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.h) 声明了：

```cpp
UPROPERTY(ReplicatedUsing = OnRep_OverlappingWeapon)
AWeapon* OverlappingWeapon;

UPROPERTY(ReplicatedUsing = OnRep_Shield)
float Shield;

UPROPERTY(Replicated)
bool bDisableGameplay;
```

但 [`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp) 的 `GetLifetimeReplicatedProps()` 只注册了 `Health`：

```cpp
DOREPLIFETIME(ABrokenEdgeCharacter, Health);
```

后果：

- `OnRep_OverlappingWeapon()` 不会按预期由属性复制触发；
- `OnRep_Shield()` 不会按预期触发；
- 客户端无法从服务器获得 `bDisableGameplay` 的权威值；
- UE 在类初始化时会报告这些属性没有注册。

推荐方向：

```cpp
DOREPLIFETIME_CONDITION(
    ABrokenEdgeCharacter,
    OverlappingWeapon,
    COND_OwnerOnly);

DOREPLIFETIME(ABrokenEdgeCharacter, Shield);
DOREPLIFETIME(ABrokenEdgeCharacter, bDisableGameplay);
```

`OverlappingWeapon` 通常只需要复制给拥有该 Character 的客户端，其他客户端不需要显示它的拾取提示。

### 4.2 武器重叠状态没有服务器写权限边界

[`Weapon.cpp`](../Source/BrokenEdge/Weapon/Weapon.cpp) 在所有实例的 `BeginPlay()` 中启用 `AreaSphere` 并绑定 Begin/End Overlap，回调直接调用：

```cpp
BrokenEdgeCharacter->SetOverlappingWeapon(this);
BrokenEdgeCharacter->SetOverlappingWeapon(nullptr);
```

[`SetOverlappingWeapon()`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp) 又是公开函数，并直接写复制属性。

这意味着服务器和客户端副本都可能各自修改本地的 `OverlappingWeapon`。当前界面有时能显示，不代表权威链正确。

推荐职责：

```text
AWeapon 服务器重叠回调
    -> ABrokenEdgeCharacter 服务器写 OverlappingWeapon
        -> COND_OwnerOnly
            -> Owning Client 的 OnRep 显示或隐藏 Widget
```

推荐措施：

- 只在 `HasAuthority()` 的 Weapon 上启用并绑定 `AreaSphere`；
- `SetOverlappingWeapon()` 改为受控的服务器内部接口；
- `OnRep_OverlappingWeapon(LastWeapon)` 负责隐藏旧 Widget、显示新 Widget；
- Listen Server 的本地玩家需要在服务器写值后主动刷新，因为 Authority 不会因自己的写入调用 `OnRep`。

### 4.3 `SetHealth()` 和 `SetShield()` 公开且没有 Authority 契约

当前两个 setter 位于 Character 的 `public` 区域：

```cpp
void SetHealth(float Amount);
void SetShield(float Amount);
```

任何 C++ 调用方只要拿到 Character 指针，就可以直接修改状态。函数只做 Clamp，没有：

- `HasAuthority()` 检查；
- 伤害、治疗和护盾变化的语义区分；
- HUD 刷新；
- 淘汰判断；
- 事件广播；
- 必要时的 `ForceNetUpdate()`。

这使调用方必须记住额外步骤。`UBuffComponent` 当前正是先调用 setter，再单独调用 `UpdateHUD...()`。

更稳妥的接口是公开“意图”，隐藏“直接写变量”：

```cpp
public:
    void ApplyHealing(float Amount);       // Server only
    void AddShield(float Amount);          // Server only
    float GetHealth() const;
    float GetShield() const;

private:
    void SetHealthInternal(float NewValue);
    void SetShieldInternal(float NewValue);
```

公开读、收紧写，并在一个位置完成 Clamp、状态变化、服务器校验和本地 HUD 刷新。

### 4.4 Buff 修改移动参数但没有同步策略

`BuffJump()` 和 `BuffSpeed()` 是公开函数，既没有 Authority 检查，也没有 RPC/Multicast。它们直接修改：

```cpp
JumpZVelocity
MaxWalkSpeed
MaxWalkSpeedCrouched
```

`UBuffComponent` 被设置为 replicated，并不表示上述 `CharacterMovement` 配置会自动按期望同步到所有客户端。

推荐：

- Buff 入口只允许服务器执行；
- 服务器保存权威 Buff 状态和结束时间；
- 根据玩法选择复制状态后在 `OnRep` 应用，或通过可靠 Multicast 应用表现参数；
- Reset 也必须走同一同步路径；
- 为 `HealingTime == 0`、`ReplenishTime == 0` 增加参数保护。

## 5. P1：高优先级封装问题

### 5.1 Character 命中盒暴露过宽

当前：

- `HitCollisionBoxes` 是 `public` 可变 `TMap`；
- 18 个 `UBoxComponent*` 位于 `protected`；
- 所有命中盒指针使用 `EditAnywhere`。

风险：

- 任意调用方可以添加、删除或替换 Map 元素；
- 任意派生类可以替换组件指针；
- Blueprint 实例可以替换默认子对象引用，而不是只调整组件参数；
- Server-Side Rewind 等系统无法依赖稳定的命中盒集合。

建议：

```cpp
private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Head;

    UPROPERTY()
    TMap<FName, TObjectPtr<UBoxComponent>> HitCollisionBoxes;

public:
    const TMap<FName, TObjectPtr<UBoxComponent>>&
        GetHitCollisionBoxes() const;
```

若 Blueprint 需要查看组件，可使用 `VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true")`，不要用 `EditAnywhere` 替换组件指针。

### 5.2 Character 的访问区段被切成五段

`BrokenEdgeCharacter.h` 当前结构大致是：

```text
public 生命周期和 HUD
protected 命中盒
public 输入函数
private 状态和回调
protected PollInit
public 状态接口
```

这不违反 C++，但会让读者无法快速看出类的公开契约。

建议固定顺序：

```text
public
    构造函数
    必须公开的 UE override
    只读查询和少量对外命令

protected
    BeginPlay / PostInitializeComponents
    可供派生类定制的钩子

private
    输入处理
    RPC
    OnRep
    HUD 辅助函数
    内部 setter
    组件和状态
```

不是所有 override 都必须公开：`BeginPlay()` 和 `PostInitializeComponents()` 可以放在 `protected`。`GetLifetimeReplicatedProps()` 保持 `public` 符合 UE 常见写法。

### 5.3 输入处理函数不需要全部 public

`Move()`、`Look()`、`CrouchButtonPressed()`、`EquipButtonPressed()` 只由本类的 `SetupPlayerInputComponent()` 绑定，没有外部调用点。

建议放入 `private`。`Jump()` 是对基类公开虚函数的行为覆盖，保留 `public` 是合理选择。

### 5.4 `GetBuff()` 暴露完整可变组件

Pickup 通过：

```cpp
Character->GetBuff()->BuffHeal(...);
```

获得完整的 `UBuffComponent` 可变接口。随着组件功能增加，任何调用方都会获得过宽权限。

两种可选方案：

1. 保留 getter，但返回明确的窄接口；
2. 更推荐让 Character 暴露 `ApplyHealingBuff()`、`ApplySpeedBuff()` 等游戏语义函数，再转发给私有组件。

第二种方案让 Character 成为外部交互边界，BuffComponent 继续负责具体计时和恢复逻辑。

### 5.5 `friend` 形成双向穿透

`UCombatComponent` 和 `UBuffComponent` 都声明 `friend class ABrokenEdgeCharacter`。Character 直接写 Buff 的：

```cpp
Character
InitialJumpZVelocity
InitialBaseSpeed
InitialCrouchSpeed
```

这能工作，但会让 Component 的私有实现失去意义。

建议由 Component 提供一次初始化入口：

```cpp
void InitializeWithCharacter(ABrokenEdgeCharacter* InCharacter);
```

Component 自己从 CharacterMovement 读取初始值，之后可以删除对应的 `friend`。CombatComponent 也应通过查询和命令接口工作，而不是依赖 Character 直接访问私有字段。

## 6. UI 函数职责与访问权限

### 6.1 当前穿透式访问

当前链路是：

```text
Character
    -> PlayerController::SetHUDHealth
        -> HUD->CharacterOverlay
            -> CharacterOverlay->HealthBar
```

为支持这条链：

- HUD 的 `CharacterOverlay` 被设为 public；
- Widget 的四个 `BindWidget` 控件也被设为 public；
- PlayerController 了解 Widget 内部控件名称。

### 6.2 推荐边界

```text
Character
    只持有游戏状态并发出本地刷新请求

PlayerController
    对外提供 SetHUDHealth / SetHUDShield

HUD
    创建并持有 CharacterOverlay
    只提供窄 getter

CharacterOverlay
    私有持有 BindWidget 控件
    提供 SetHealth / SetShield
```

示意接口：

```cpp
class UCharacterOverlay : public UUserWidget
{
public:
    void SetHealth(float Health, float MaxHealth);
    void SetShield(float Shield, float MaxShield);

private:
    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UProgressBar> HealthBar;
};
```

这样修改 Widget 控件结构时，不需要同步修改 PlayerController。

### 6.3 HUD 成员权限

`CharacterOverlayClass` 更适合：

```cpp
UPROPERTY(EditDefaultsOnly, Category="HUD")
TSubclassOf<UCharacterOverlay> CharacterOverlayClass;
```

`CharacterOverlay` 应为 private，并提供只读 getter。`AddCharacterOverlay()` 当前只有 `BeginPlay()` 调用，可放 `private` 或 `protected`，不需要 public。

### 6.4 PlayerController 的 `PollInit()`

没有派生类调用或覆盖它，因此应放 `private`，不是 `protected`。

另有一个相邻逻辑问题：HUD 尚未准备好时，代码把 `bInitializeHealth` 设为 `true`；但 `PollInit()` 使用：

```cpp
if (!bInitializeHealth) SetHUDHealth(...);
```

条件方向与变量含义相反。Shield 同样如此。这里应在整改时一起修复，否则缓存值不会在 Widget 创建后刷新。

## 7. Weapon 与 Pickup

### 7.1 Weapon 做得较好的部分

- `BeginPlay()` 位于 protected；
- `WeaponMesh`、`AreaSphere`、`PickUpWidget` 是 private；
- Widget 显隐通过 `ShowPickupWidget()` 封装，没有直接把 WidgetComponent 暴露出去。

### 7.2 Weapon 需要调整的部分

`OnSphereOverlap()` 和 `OnSphereEndOverlap()` 只用于内部委托绑定，应放 `protected` 或 `private`，不需要 public。

`UFUNCTION()` 被放在 `BeginPlay()` 前，使 `BeginPlay` 本身成为反射函数。它不是重叠回调，这个宏没有必要，应移到真正需要动态委托绑定的两个 overlap 函数上。

`Tick()` 为空，但构造函数启用了 Tick。若暂时没有逐帧逻辑，应设置：

```cpp
PrimaryActorTick.bCanEverTick = false;
```

### 7.3 Pickup 做得较好的部分

- 基类 `OnSphereOverlap()` 是 protected virtual；
- 派生 Pickup 的 override 也放在 protected；
- Timer handle 和绑定辅助函数是 private；
- 拾取逻辑只在 Authority 上绑定回调，网络边界优于 Weapon。

### 7.4 Pickup 需要调整的部分

`OverlapSphere` 和 `PickupMesh` 是构造创建的默认子对象，建议从 `EditAnywhere` 改为 `VisibleAnywhere`。需要修改半径、网格或碰撞时，编辑组件内部属性，而不是替换组件指针。

`BaseTurnRate` 和各 Buff 参数如果只应在类默认值中配置，使用 `EditDefaultsOnly` 比 `EditAnywhere` 更能表达意图。时间、速度和数值参数还应增加 `Category` 与 `ClampMin`。

## 8. Component 的函数组织

### 8.1 BuffComponent

当前头文件顺序是：

```text
public 构造和 friend
protected BeginPlay / RampUp
private 部分状态
public Tick / Buff API
private Timer / Reset / 初始值
```

建议整理为：

```text
public
    构造
    InitializeWithCharacter
    BuffJump / BuffSpeed / BuffHeal / ReplenishShield

protected
    BeginPlay
    TickComponent

private
    HealRampUp / ShieldRampUp
    ResetJump / ResetSpeeds
    所有运行状态和 Timer
```

如果没有派生 BuffComponent 的计划，RampUp 函数也应为 private。

### 8.2 CombatComponent

做得较好的地方：两个复制状态和两个 `OnRep` 都是 private，复制注册也完整。

需要调整：

- `BeginPlay()` 应从 public 移到 protected；
- 两个连续的 `public:` 应合并；
- 当前 `TickComponent()` 为空，却启用了 Tick；
- 用窄查询/命令接口替代 `friend class ABrokenEdgeCharacter`；
- `CombatState` 和 `bHoldingTheFlag` 应通过 `IsHoldingFlag()`、`GetCombatState()` 等只读接口提供给 AnimInstance/Character。

## 9. GameMode、AnimInstance 与测试代码

### 9.1 GameMode

`ABrokenEdgeGameMode::BeginPlay()` 应放 protected。更重要的是，生产 GameMode 的 `.cpp` 包含 `test/CombatObserver.h` 和 `test/MyCharacter.h`，并保留一大段注释掉的测试代码。

建议：

- 从 GameMode 移除测试 include 和注释测试逻辑；
- 将实验 Actor 放进独立测试地图、Editor 模块或自动化测试；
- 默认 Pawn 配置可继续放在 GameMode 构造函数或 Blueprint GameMode 中。

### 9.2 AnimInstance

这是当前权限设计较好的一组类：

- 运行变量全部是 private；
- Blueprint 只有 ReadOnly 权限；
- 外部通过 Character 的查询函数读取状态。

建议微调：

- `IsWeaponEquipped()`、`IsAiming()`、`ShouldRotateRootBone()`、`IsElimed()` 标为 `const` 和 `[[nodiscard]]`；
- 修正 `IsElimed` 命名为 `IsEliminated`；
- 当前前两个查询固定返回 `true`，应转发到 CombatComponent 的真实状态。

### 9.3 `AMyCharacter::TakeDamage(float)`

本次强制构建已由 MSVC 确认：

```text
warning C4263: AMyCharacter::TakeDamage(float) 不重写基类虚函数
warning C4264: APawn::TakeDamage(...) 被隐藏
```

原因是测试函数与 `APawn::TakeDamage` 同名但签名不同。

两种修复选择：

1. 只是测试函数：改名为 `ApplyTestDamage(float)`；
2. 要接入 UE 伤害系统：使用完整 override 签名并返回实际伤害值。

```cpp
virtual float TakeDamage(
    float DamageAmount,
    const FDamageEvent& DamageEvent,
    AController* EventInstigator,
    AActor* DamageCauser) override;
```

### 9.4 测试委托权限

`FOnHealthChanged OnHealthChanged` 是 public multicast delegate。外部不仅能订阅，也能 `Broadcast()`、`Clear()` 或移除其他监听者。

若要保证只有 Character 能广播，可使用 `DECLARE_EVENT_TwoParams`，再向外只暴露订阅接口。

`UCombatObserver` 的 handler、弱引用和 delegate handle 都是 private，这部分权限合理。

### 9.5 TestActorMove

`BeginPlay()` protected、配置和运行状态 private，组织合理。`Tick()` 保持 public 与 UE Actor 常见写法一致，不属于问题。

## 10. 各类整改表

| 类 | 当前主要问题 | 推荐调整 | 优先级 |
|---|---|---|---:|
| `ABrokenEdgeCharacter` | 公开写状态、命中盒可变公开、访问区段碎片化、3 个复制属性未注册 | 公开读/收紧写；注册复制；输入和内部辅助移 private | P0 |
| `UBuffComponent` | public API 无 Authority；双向 friend；速度/跳跃未同步 | 服务器入口、统一同步、初始化函数替代 friend | P0 |
| `UCombatComponent` | 空 Tick、BeginPlay public、friend | 关闭 Tick；生命周期 protected；窄接口 | P1 |
| `AWeapon` | overlap 在所有网络实例写状态；回调 public | Authority-only overlap；回调 private/protected | P0 |
| `APickup` | 默认子对象指针 `EditAnywhere` | `VisibleAnywhere`；参数 `EditDefaultsOnly` | P2 |
| `ABrokenEdgePlayerController` | 穿透 HUD/Widget；缓存条件反向 | 调用 Widget 窄接口；修正初始化条件 | P1 |
| `ABrokenEdgeHUD` | Widget 类和实例 public；Add 函数 public | 类 `EditDefaultsOnly`；实例 private；只读 getter | P1 |
| `UCharacterOverlay` | BindWidget 控件 public | 控件 private；提供 SetHealth/SetShield | P1 |
| `UBrokenEdgeAnimInstance` | 查询函数非 const；读取到硬编码状态 | Character 查询改 const，连接 Combat 真实状态 | P2 |
| `ABrokenEdgeGameMode` | BeginPlay public；生产类引用测试代码 | BeginPlay protected；移除测试依赖 | P1 |
| `AMyCharacter` | 隐藏 APawn::TakeDamage；委托可被外部广播 | 重命名/正确 override；使用 owner-only event | P1 |
| `UCombatObserver` | 核心权限合理 | 补生命周期解除订阅策略 | P2 |
| `ATestActorMove` | 未发现明显权限问题 | 保持现状 | - |
| `AHitScanWeapon` | 空类和空 public 区段 | 实现 Fire 后再保留，或暂时删除噪声 | P3 |

## 11. 推荐的 Character 头文件骨架

下面只表达“放在哪里”，不是要求直接复制：

```cpp
UCLASS()
class BROKENEDGE_API ABrokenEdgeCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ABrokenEdgeCharacter();

    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent*) override;
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void Jump() override;

    [[nodiscard]] float GetHealth() const;
    [[nodiscard]] float GetMaxHealth() const;
    [[nodiscard]] float GetShield() const;
    [[nodiscard]] bool IsWeaponEquipped() const;
    [[nodiscard]] bool IsAiming() const;

    // 对外暴露游戏语义，不暴露任意写变量。
    void ApplyHealingBuff(float Amount, float Duration);
    void ApplyShieldBuff(float Amount, float Duration);

protected:
    virtual void BeginPlay() override;
    virtual void PostInitializeComponents() override;
    virtual void NotifyControllerChanged() override;

private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void CrouchButtonPressed();
    void EquipButtonPressed();

    UFUNCTION(Server, Reliable)
    void ServerEquipButtonPressed();

    UFUNCTION()
    void OnRep_OverlappingWeapon(AWeapon* LastWeapon);

    UFUNCTION()
    void OnRep_Health();

    UFUNCTION()
    void OnRep_Shield(float LastShield);

    void SetOverlappingWeaponFromServer(AWeapon* Weapon);
    void SetHealthInternal(float NewValue);
    void SetShieldInternal(float NewValue);
    void RefreshLocalHUD();
    void PollInit();

    // 组件、命中盒、复制状态和缓存统一放在这里。
};
```

## 12. 推荐实施顺序

### 第一批：修正网络正确性

1. 注册 `OverlappingWeapon`、`Shield`、`bDisableGameplay`；
2. Weapon overlap 改为 Authority-only；
3. 实现 `OnRep_OverlappingWeapon()`；
4. 为 Shield 恢复 HUD 更新；
5. 明确 BuffJump/BuffSpeed 的复制方案。

### 第二批：收紧写权限

1. 隐藏 `SetHealth/SetShield`；
2. 隐藏可变 HitCollisionBoxes；
3. 用 Component 初始化函数替代 friend；
4. 将输入函数、PollInit、overlap callback 移入 private/protected。

### 第三批：整理 UI 职责

1. Widget 控件私有化；
2. Widget 提供 `SetHealth/SetShield`；
3. HUD 只负责创建和持有 Widget；
4. Controller 不再访问 Widget 内部控件；
5. 修正 `bInitializeHealth/bInitializeShield` 条件。

### 第四批：隔离测试和清理结构

1. 修复 `TakeDamage` 隐藏警告；
2. 从 GameMode 移除 test include；
3. 关闭 Weapon、Combat 等空 Tick；
4. 统一头文件区段和函数定义顺序；
5. 把 `EditAnywhere` 精确替换为 `EditDefaultsOnly`、`EditInstanceOnly` 或 `VisibleAnywhere`。

## 13. 验证结果与限制

本次执行了强制构建：

```powershell
Build.bat BrokenEdgeEditor Win64 Development `
  -Project=C:\UnrealProjects\BrokenEdge\BrokenEdge.uproject `
  -WaitMutex -NoHotReloadFromIDE -NoUBTMakefiles
```

结果：

- UHT 成功；
- `Module.BrokenEdge.cpp` 编译成功；
- `UnrealEditor-BrokenEdge.dll` 链接成功；
- 构建退出码为 0；
- 保留两个 `AMyCharacter::TakeDamage(float)` 隐藏基类函数警告。

构建成功只证明当前代码可编译，不证明多人运行时权限正确。本次没有修改源码，也没有进行双客户端 PIE、Dedicated Server 或实际拾取/Buff 回归测试。

## 14. 最终判断

当前工程中，以下做法值得保留：

- OnRep 通常位于 private；
- Combat 的复制字段是 private；
- AnimInstance 使用 private + BlueprintReadOnly；
- Pickup 派生回调使用 protected virtual；
- Observer 的内部 handler 和 delegate handle 是 private。

下一阶段最重要的原则是：

```text
公开查询，隐藏直接写入；
公开游戏意图，隐藏实现细节；
C++ private 不代替 Server Authority；
Widget 自己管理控件，Controller 不穿透 UI；
默认子对象可见但不可替换；
测试代码不进入生产 Runtime 职责链。
```

优先修复 P0 网络问题，再调整访问标签。只移动 `public/private` 而不修 Authority 和复制注册，无法解决当前最关键的风险。
