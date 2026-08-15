# UE5 为什么通过 PlayerController 更新生命值与护盾 UI

## 1. 先给结论

生命值和护盾值不应该存放在 `PlayerController` 中；它们在当前 BrokenEdge 项目里属于 `ABrokenEdgeCharacter`。

之所以让 Character 调用 PlayerController 的 `SetHUDHealth()` / `SetHUDShield()`，是因为 PlayerController 是“当前本地玩家”与其 HUD、Viewport、输入和受控 Pawn 之间的稳定桥梁：

```text
服务器权威数据              本地玩家桥梁               本地表现对象
Character Health/Shield ──▶ PlayerController ──▶ HUD ──▶ CharacterOverlay
```

各层职责是：

| 层 | 职责 |
| --- | --- |
| Character / Attribute Component | 保存并复制生命值、护盾值 |
| PlayerController | 确定“这是哪个本地玩家的 UI”，协调 Pawn 与 HUD |
| HUD | 创建和持有屏幕级 Widget |
| CharacterOverlay | 修改进度条和文本 |

因此，“使用 Controller”是一种 UI 协调方案，不是 Unreal 强制规定，也不是说属性属于 Controller。

## 2. 当前项目的真实调用链

### 2.1 生命拾取物只启动治疗

[`HealthPickup.cpp`](../Source/BrokenEdge/Pickups/HealthPickup.cpp) 中：

```cpp
BuffComponent->BuffHeal(HealthAmount, HealthingTime);
Destroy();
```

`AHealthPickup` 没有直接访问 HUD。它只把“恢复多少生命、持续多久”交给 `UBuffComponent`。

这是合理的职责划分：拾取物负责触发效果，不负责决定 UI 怎样显示。

### 2.2 Buff 每帧修改 Character

[`BuffComponent.cpp`](../Source/BrokenEdge/Component/BuffComponent.cpp) 中：

```cpp
void UBuffComponent::HealRampUp(float DeltaTime)
{
    if (!bHealing || Character == nullptr || Character->IsElimed()) return;

    const float HealThisFrame = HealingRate * DeltaTime;
    Character->SetHealth(Character->GetHealth() + HealThisFrame);
    Character->UpdateHUDHealth();
    AmountToHeal -= HealThisFrame;
}
```

护盾的流程相同：

```cpp
Character->SetShield(Character->GetShield() + ReplenishThisFrame);
Character->UpdateHUDShield();
```

### 2.3 Character 找到自己的 Controller

[`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp) 中：

```cpp
void ABrokenEdgeCharacter::UpdateHUDHealth()
{
    BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
        ? Cast<ABrokenEdgePlayerController>(GetController())
        : BrokenEdgePlayerController;

    if (BrokenEdgePlayerController)
    {
        BrokenEdgePlayerController->SetHUDHealth(Health, MaxHealth);
    }
}
```

Character 只传递数值，没有直接包含 `UProgressBar`、`UTextBlock` 或 Widget 蓝图类型。

### 2.4 Controller 找到本地 HUD

[`BrokenEdgePlayerController.cpp`](../Source/BrokenEdge/PlayerController/BrokenEdgePlayerController.cpp) 中：

```cpp
BrokenEdgeHUD = BrokenEdgeHUD == nullptr
    ? Cast<ABrokenEdgeHUD>(GetHUD())
    : BrokenEdgeHUD;
```

当 HUD 和 Widget 已准备好时：

```cpp
const float HealthPercent = Health / MaxHealth;
BrokenEdgeHUD->CharacterOverlay->HealthBar->SetPercent(HealthPercent);

const FString HealthText = FString::Printf(
    TEXT("%d/%d"),
    FMath::CeilToInt(Health),
    FMath::CeilToInt(MaxHealth));

BrokenEdgeHUD->CharacterOverlay->HealthText->SetText(
    FText::FromString(HealthText));
```

### 2.5 HUD 创建 Overlay

[`BrokenEdgeHUD.cpp`](../Source/BrokenEdge/UI/BrokenEdgeHUD.cpp) 中：

```cpp
void ABrokenEdgeHUD::AddCharacterOverlay()
{
    TObjectPtr<APlayerController> PlayerController = GetOwningPlayerController();

    if (PlayerController && CharacterOverlayClass)
    {
        CharacterOverlay = CreateWidget<UCharacterOverlay>(
            PlayerController,
            CharacterOverlayClass);

        CharacterOverlay->AddToViewport();
    }
}
```

最终真正修改控件的是 [`CharacterOverlay.h`](../Source/BrokenEdge/UI/CharacterOverlay.h) 中绑定的：

```cpp
UProgressBar* HealthBar;
UTextBlock* HealthText;
UProgressBar* ShieldBar;
UTextBlock* ShieldText;
```

## 3. 为什么 PlayerController 适合作为桥梁

### 3.1 PlayerController 表示一个玩家连接

Character/Pawn 表示当前被控制的游戏实体。PlayerController 表示玩家本身的控制上下文。

玩家死亡、重生或切换 Pawn 时：

```text
同一个 PlayerController
        │
        ├── Possess Pawn A
        ├── Pawn A 被销毁
        └── Possess Pawn B
```

如果屏幕 UI 完全归 Character 管理，每次 Pawn 更换都要重新解决 UI 引用和生命周期问题。Controller 更适合协调“新 Pawn 的数据应该显示到同一个玩家界面”。

### 3.2 PlayerController 原生持有 HUD

UE 5.5.4 引擎源码 `PlayerController.h` 中，`APlayerController` 直接保存：

```cpp
/** Heads up display associated with this PlayerController. */
TObjectPtr<AHUD> MyHUD;
```

`GetHUD()` 返回的就是这个 `MyHUD`。项目从 Controller 进入 HUD，不是随意绕路，而是沿用引擎自身的所有权关系。

### 3.3 HUD 只为本地玩家创建

UE 5.5.4 引擎源码 `APlayerController::SpawnDefaultHUD()` 首先检查：

```cpp
if (Cast<ULocalPlayer>(Player) == nullptr)
{
    return;
}
```

这意味着默认 HUD 面向本地玩家，而不是每个服务器 Controller 或每个远程角色代理都创建一份。

这正符合屏幕 UI 的本质：

- 服务器负责真实生命值；
- 每个客户端只创建自己的屏幕 UI；
- Dedicated Server 不需要血条 Widget；
- 其他玩家的 Character 副本不应更新我的主 HUD。

### 3.4 Controller 能区分本地玩家

Controller 提供 `IsLocalController()`，Character 提供 `IsLocallyControlled()`。更新屏幕 UI 前可以明确过滤：

```cpp
if (!IsLocallyControlled()) return;
```

这比让每个复制到客户端的 Character 都直接寻找 Viewport 更安全。

### 3.5 Controller 可以处理初始化时序

网络复制、Pawn Possess、HUD 创建和 Widget 构造不保证在同一帧完成：

```text
Health 首次复制到客户端
        │
        ├── HUD 可能已经创建：立即显示
        │
        └── HUD 尚未创建：暂存数值，等 Overlay Ready 后显示
```

Controller 同时能访问 Pawn 和 HUD，适合作为这类时序协调点。

## 4. 多人网络中的实例边界

### 4.1 各类对象存在于哪里

| 对象 | Dedicated Server | 拥有者客户端 | 其他客户端 |
| --- | --- | --- | --- |
| GameMode | 有 | 无 | 无 |
| 该玩家的 PlayerController | 有 | 有 | 通常无 |
| Character | 有 | 有 | 有复制代理 |
| PlayerState | 有 | 有 | 有 |
| HUD / Viewport Widget | 无 | 有 | 只存在各自本地 UI |

这里最关键的一行是 PlayerController：服务器具有每个连接的 Controller，但普通客户端只具有自己的 PlayerController，不会获得所有其他玩家的 PlayerController。

因此，在客户端的远程 Character 代理上：

```cpp
GetController()
```

通常不会得到那个远程玩家的 PlayerController。当前 `UpdateHUDHealth()` 的 Cast 会失败，不会错误更新本地 HUD。

不过，显式检查 `IsLocallyControlled()` 会让意图更清晰。

### 4.2 生命值的当前复制流程

当前 `Health` 声明为：

```cpp
UPROPERTY(ReplicatedUsing = OnRep_Health)
float Health;
```

并在 `GetLifetimeReplicatedProps()` 中注册：

```cpp
DOREPLIFETIME(ABrokenEdgeCharacter, Health);
```

服务器修改后，客户端收到新值并执行：

```cpp
void ABrokenEdgeCharacter::OnRep_Health()
{
    UpdateHUDHealth();
}
```

完整过程是：

```text
服务器 HealthPickup 重叠
          │
          ▼
服务器 BuffComponent::HealRampUp
          │
          ▼
服务器 Character::Health 改变
          │
          ├── Listen Server 本地玩家：直接刷新 HUD
          │
          └── 属性复制到客户端
                         │
                         ▼
                 客户端 OnRep_Health
                         │
                         ▼
                 本地 Controller → HUD → Widget
```

### 4.3 为什么服务器也要调用共享刷新路径

RepNotify 主要响应“客户端收到服务器复制值”。服务器自己修改属性时，不应依赖 `OnRep_Health()` 自动替它刷新 Listen Server 本地 UI。

所以常见写法是：

- 服务器修改后调用一次共享处理函数；
- 客户端 `OnRep` 也调用同一个处理函数；
- Dedicated Server 因为不是本地控制器，UI 分支自然不执行。

## 5. 当前 BrokenEdge 实现中尚未闭合的地方

### 5.1 Shield 没有注册复制

头文件虽然写了：

```cpp
UPROPERTY(ReplicatedUsing = OnRep_Shield)
float Shield;
```

但当前 `GetLifetimeReplicatedProps()` 只有：

```cpp
DOREPLIFETIME(ABrokenEdgeCharacter, Health);
```

缺少：

```cpp
DOREPLIFETIME(ABrokenEdgeCharacter, Shield);
```

`UPROPERTY(ReplicatedUsing=...)` 和 `DOREPLIFETIME` 缺一不可。当前远端客户端不会依靠属性复制获得新的护盾值。

### 5.2 `OnRep_Shield()` 没有刷新 UI

当前代码是：

```cpp
void ABrokenEdgeCharacter::OnRep_Shield(float LastShield)
{
    //@TODO
    // UpdateHUDShield();
}
```

即使补上 `DOREPLIFETIME`，客户端收到护盾后也不会更新 HUD，除非恢复 `UpdateHUDShield()` 调用。

### 5.3 HUD 初始化缓存没有被消费

`SetHUDHealth()` 在 Overlay 尚未准备好时缓存：

```cpp
bInitializeHealth = true;
HUDHealth = Health;
HUDMaxHealth = MaxHealth;
```

护盾也有对应缓存。然而当前整个工程中没有其他代码读取：

```text
bInitializeHealth
bInitializeShield
HUDHealth
HUDMaxHealth
HUDShield
HUDMaxShield
```

因此当前行为是：

```text
第一次更新早于 Widget 创建
        │
        ▼
Controller 成功缓存数值
        │
        ▼
Overlay 创建
        │
        └── 没有消费缓存，初始 UI 仍可能错误
```

设计意图正确，但缺少“Overlay Ready 后重新应用缓存”的最后一步。

### 5.4 当前 Buff 每帧主动更新 HUD

`HealRampUp()` 和 `ShieldRampUp()` 每 Tick 都调用 UI 刷新。功能规模较小时可以工作，但数据层与 UI 层耦合较强。

更易维护的边界是：

```text
Buff 只修改属性
Character 在属性变化时发布事件
本地 UI 层订阅事件并刷新
```

## 6. Controller 并不是唯一方案

### 6.1 Character 直接改 Widget

```text
Character → Widget
```

优点：实现最短。

缺点：

- Character 与具体 UI 控件强耦合；
- Dedicated Server 和远程代理必须不断判空；
- Pawn 重生时引用容易失效；
- 玩法类需要包含 UMG 细节。

适合单机原型，不适合逐渐扩大的多人项目。

### 6.2 当前方案：Character → Controller → HUD

优点：

- 沿用 PlayerController 对 HUD 的所有权；
- 能明确限定本地玩家；
- Pawn 更换后仍有稳定协调点；
- Controller 可以缓存 HUD 创建前的数据。

缺点：

- Character 仍然知道具体 Controller 类型；
- Setter 变多后 PlayerController 容易膨胀；
- 必须认真处理 HUD 初始化顺序。

这是当前项目最容易继续完善的方案。

### 6.3 事件/委托方案

```text
Character 属性变化
        │ Broadcast
        ▼
Controller / Widget 订阅
        │
        ▼
刷新 UI
```

优点：

- Character 不需要知道具体 Widget；
- 多个表现系统可以同时监听；
- 便于扩展受伤音效、屏幕闪烁和低血量警告。

注意：Pawn 更换或 Widget 销毁时必须正确解绑和重新绑定。

### 6.4 MVVM / ViewModel 方案

大型 UI 可以增加 ViewModel：

```text
Character / PlayerState → ViewModel → Widget
```

它能进一步降低 Controller 中大量 `SetHUDXxx()` 的样板代码，但对于当前小规模项目可能过重。

## 7. 数据应该放在哪个类

### 7.1 Character

适合：

- 当前 Pawn 的生命值、护盾、弹药；
- 死亡后随 Pawn 销毁和重置的数据；
- 与碰撞、伤害和能力直接相关的数据。

当前 BrokenEdge 把 Health/Shield 放在 Character 是合理的。

### 7.2 PlayerState

适合：

- 分数、击杀、队伍；
- 玩家名；
- Pawn 死亡重生后仍需保留的数据；
- 需要让所有客户端查看的玩家级状态。

如果生命值属于“当前角色身体”，通常仍应放 Character；如果设计要求换 Pawn 后保留某类玩家属性，再考虑 PlayerState 或独立属性系统。

### 7.3 PlayerController

适合：

- 当前拥有玩家的输入和操作请求；
- 本地 UI 协调；
- Pawn Possess 切换；
- 发往服务器的玩家拥有 RPC 入口。

不适合把需要复制给所有人观察的生命值本体放在这里。

### 7.4 GameMode

GameMode 只存在服务器，不适合直接操作客户端 UI。它可以决定规则，再让复制的 Character、PlayerState 或 GameState 把结果传到客户端。

## 8. 推荐的闭环结构

下面是适合当前项目的改进方向，本文只说明设计，没有修改源码。

### 8.1 复制两个属性

```cpp
void ABrokenEdgeCharacter::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ABrokenEdgeCharacter, Health);
    DOREPLIFETIME(ABrokenEdgeCharacter, Shield);
}
```

### 8.2 使用共同的本地刷新函数

```cpp
void ABrokenEdgeCharacter::RefreshOwnerHUD()
{
    if (!IsLocallyControlled()) return;

    ABrokenEdgePlayerController* PC =
        Cast<ABrokenEdgePlayerController>(GetController());

    if (PC)
    {
        PC->SetHUDHealth(Health, MaxHealth);
        PC->SetHUDShield(Shield, MaxShield);
    }
}

void ABrokenEdgeCharacter::OnRep_Health()
{
    RefreshOwnerHUD();
}

void ABrokenEdgeCharacter::OnRep_Shield(float LastShield)
{
    RefreshOwnerHUD();
}
```

服务器修改后也调用 `RefreshOwnerHUD()`，Listen Server 主机便能立即更新；Dedicated Server 会因为没有本地控制器而跳过。

### 8.3 Overlay 创建后主动同步一次

仅依赖 RepNotify 不足以保证初始 UI，因为复制可能早于 Widget 创建。Overlay Ready 后，Controller 应从当前 Pawn 读取完整快照：

```cpp
void ABrokenEdgePlayerController::HandleCharacterOverlayReady()
{
    if (const ABrokenEdgeCharacter* Character =
        Cast<ABrokenEdgeCharacter>(GetPawn()))
    {
        SetHUDHealth(Character->GetHealth(), Character->GetMaxHealth());
        SetHUDShield(Character->GetShield(), Character->GetMaxShield());
    }
}
```

触发点可以是：

- HUD 完成 `AddCharacterOverlay()` 后通知 Controller；
- Controller 的初始化轮询发现 Overlay 第一次有效；
- Widget 初始化完成后向拥有者请求一次数据快照。

事件通知通常比永久 Tick 轮询更清晰。

### 8.4 Pawn 切换后重新绑定

如果未来支持死亡重生或切换角色，Controller 需要在 Pawn 改变时：

1. 从旧 Pawn 的属性事件解绑；
2. 绑定新 Pawn；
3. 读取新 Pawn 的当前生命和护盾；
4. 刷新现有 HUD。

服务端 Possess 和拥有者客户端收到 Pawn 的时间不同，因此要分别考虑服务器 Possess 与客户端 Pawn 同步完成的生命周期。

## 9. 四种运行模式手动推演

### 9.1 Standalone

```text
本机既是 Authority 又是 Local Player
Buff 修改 Health
直接调用 RefreshOwnerHUD
Controller 有 HUD
UI 更新
```

Standalone 中没有网络往返，`OnRep` 不是本机 UI 刷新的唯一入口。

### 9.2 Listen Server 主机

```text
主机 Character 有 Authority
主机 PlayerController 也是 Local Controller
服务器修改后直接刷新主机 HUD
```

如果只依赖客户端 RepNotify，主机 UI 可能不刷新，所以服务器修改后的共享处理路径很重要。

### 9.3 远程客户端

```text
服务器修改 Health
       │ Replication
       ▼
客户端 Character::OnRep_Health
       │
       ▼
客户端自己的 PlayerController
       │
       ▼
本地 HUD 更新
```

### 9.4 Dedicated Server

```text
服务器修改 Health/Shield
服务器没有 LocalPlayer HUD
UI 刷新路径因 IsLocallyControlled/IsLocalController 返回 false 而跳过
属性继续正常复制给客户端
```

这也是把 UI 表现与权威数据分开的价值。

## 10. 常见误区

### 误区一：UI 显示生命值，所以生命值应该放 Controller

错误。显示入口与数据所有权是两件事。生命值属于 Character，Controller 只是把本地数据送到 HUD。

### 误区二：`UPROPERTY(ReplicatedUsing=...)` 已经足够

错误。还需要在 `GetLifetimeReplicatedProps()` 中注册 `DOREPLIFETIME`。

### 误区三：客户端 UI 应通过 Multicast 更新

通常不需要。复制属性加 RepNotify 已经把状态同步给客户端；UI 根据本地复制状态刷新即可。Multicast 容易把瞬时表现与长期状态混在一起。

### 误区四：所有客户端都能拿到所有 PlayerController

错误。服务器有各玩家 Controller；普通客户端主要只有自己的 PlayerController。其他玩家的可见公共数据通常通过 Character 或 PlayerState 复制。

### 误区五：调用一次 `SetHUDHealth()` 就一定成功

错误。HUD 或 Overlay 可能尚未创建。必须有缓存后消费、Ready 通知或初始化快照机制。

## 11. 课堂问题与练习

### 问题一

如果把 `Health` 放进 PlayerController，其他客户端如何显示这个玩家头顶的生命条？

提示：其他客户端通常没有该玩家的 PlayerController。

### 问题二

为什么 `OnRep_Health()` 已经调用 `UpdateHUDHealth()`，Listen Server 主机仍可能需要在服务器修改后主动刷新？

提示：区分“Authority 本地赋值”和“客户端接收属性复制”。

### 问题三

为什么 Controller 里缓存了 `HUDHealth`，初始 UI 仍可能不正确？

答案方向：缓存数据只有被 Overlay Ready 流程消费才有意义。

### 练习一：画出实例表

分别列出 Dedicated Server、客户端 A、客户端 B 上存在的：

- Character A/B；
- PlayerController A/B；
- HUD A/B；
- PlayerState A/B。

说明客户端 A 为什么不能依靠 PlayerController B 获取玩家 B 的公开数据。

### 练习二：补齐护盾链路

只做源码分析，列出让远程客户端护盾 UI 正常更新至少需要补齐的三个环节。

参考方向：

1. `DOREPLIFETIME(Shield)`；
2. `OnRep_Shield()` 调用共享刷新路径；
3. Overlay Ready 后进行初始同步。

### 练习三：比较两种架构

比较以下方案的耦合和生命周期成本：

```text
Character → Controller → HUD → Widget
Character Delegate → Widget/ViewModel
```

说明在 Pawn 高频更换和 UI 页面较多时，为什么事件或 ViewModel 方案可能更合适。

## 12. 最终记忆模型

```text
谁保存真实状态？
    Character / PlayerState / Attribute Component

谁负责网络同步？
    服务器权威属性 + Replication / RepNotify

谁代表本地玩家？
    PlayerController

谁创建屏幕 UI？
    HUD / LocalPlayer UI Layer

谁真正修改控件？
    UserWidget
```

一句话总结：

> Character 保存生命值，服务器负责修改与复制；PlayerController 识别本地玩家并连接 HUD，Widget 只负责展示。Controller 是协调层，不是生命值的数据层。

