# UE5 委托：从声明、绑定到生命周期管理

## 1. 委托解决什么问题

委托（Delegate）可以把“什么时候发生某件事”和“发生后具体做什么”分开。

假设角色的生命值发生变化。角色只负责修改生命值并发布通知，不需要知道界面、音效、任务系统分别如何响应：

```text
角色受到伤害
    │
    └─ 广播 OnHealthChanged
          ├─ UI 更新血条
          ├─ 音效系统播放受伤音效
          └─ 任务系统统计受伤次数
```

这种结构的主要价值是降低耦合：发布者不必持有每个监听者，也不必随着监听者数量增加而不断修改业务代码。

委托适合表达：

- 状态变化通知，例如生命值、分数、任务进度发生变化；
- 异步操作完成回调，例如资源加载或网络请求结束；
- 模块间事件，例如一个系统通知多个互不依赖的对象；
- 可由蓝图绑定的 Event Dispatcher；
- 把一段可执行逻辑作为参数传递给函数。

## 2. UE 委托的主要类型

| 类型 | 监听者数量 | 蓝图/反射支持 | 常见用途 |
| --- | ---: | --- | --- |
| 原生单播委托 | 1 | 不支持 | C++ 回调、异步完成通知 |
| 原生多播委托 | 多个 | 不支持 | 纯 C++ 的一对多事件 |
| 动态单播委托 | 1 | 支持 | 蓝图委托参数、需要反射的回调 |
| 动态多播委托 | 多个 | 支持 | `BlueprintAssignable`、Event Dispatcher |
| Event | 多个 | 不支持 | 限制只有事件拥有者可以广播 |

选择时可以先问两个问题：

1. 是只允许一个回调，还是允许多个监听者？
2. 是否必须暴露给蓝图，或者需要被 UE 反射、序列化？

如果答案是“纯 C++，不需要蓝图”，优先使用原生委托。动态委托需要通过反射按函数名调用，功能更多，但开销也更高。

## 3. 原生单播委托

### 3.1 声明

```cpp
DECLARE_DELEGATE(FOnOperationFinished);
DECLARE_DELEGATE_OneParam(FOnDamageCalculated, float);
DECLARE_DELEGATE_TwoParams(FOnItemSelected, int32, const FString&);
DECLARE_DELEGATE_RetVal(bool, FCanStartOperation);
DECLARE_DELEGATE_RetVal_OneParam(int32, FCalculateScore, float);
```

宏名中的 `OneParam`、`TwoParams` 表示广播时需要传入的参数数量。带 `RetVal` 的单播委托可以返回值。

### 3.2 绑定与执行

```cpp
class FDamageCalculator
{
public:
    float ApplyArmor(float Damage) const
    {
        return Damage * 0.8f;
    }
};

DECLARE_DELEGATE_RetVal_OneParam(float, FModifyDamage, float);

FDamageCalculator Calculator;
FModifyDamage ModifyDamage;
ModifyDamage.BindRaw(&Calculator, &FDamageCalculator::ApplyArmor);

if (ModifyDamage.IsBound())
{
    const float FinalDamage = ModifyDamage.Execute(100.0f);
}
```

无返回值的单播委托通常可以直接使用 `ExecuteIfBound()`：

```cpp
DECLARE_DELEGATE_OneParam(FOnMessage, const FString&);

FOnMessage OnMessage;
OnMessage.BindLambda([](const FString& Message)
{
    UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
});

OnMessage.ExecuteIfBound(TEXT("Operation completed"));
```

不要在未确认绑定状态时直接调用 `Execute()`。对于有返回值的委托，需要先用 `IsBound()` 判断，因为未绑定时不存在合理的返回值。

## 4. 原生多播委托：真正的一对多事件

### 4.1 声明发布者

```cpp
// HealthComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(
    FOnHealthChanged,
    float /* NewHealth */,
    float /* HealthDelta */);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UHealthComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    void ApplyDamage(float Damage);

    // 监听者只能绑定、解绑；发布者在自身实现中负责广播。
    FOnHealthChanged OnHealthChanged;

private:
    float Health = 100.0f;
};
```

```cpp
// HealthComponent.cpp

#include "HealthComponent.h"

void UHealthComponent::ApplyDamage(const float Damage)
{
    if (Damage <= 0.0f)
    {
        return;
    }

    const float OldHealth = Health;
    Health = FMath::Clamp(Health - Damage, 0.0f, 100.0f);
    const float HealthDelta = Health - OldHealth;

    if (!FMath::IsNearlyZero(HealthDelta))
    {
        OnHealthChanged.Broadcast(Health, HealthDelta);
    }
}
```

### 4.2 绑定多个监听者

```cpp
// CombatObserver.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CombatObserver.generated.h"

class UHealthComponent;

UCLASS()
class UCombatObserver : public UObject
{
    GENERATED_BODY()

public:
    void StartObserving(UHealthComponent* HealthComponent);
    void StopObserving();

private:
    void HandleHealthChanged(float NewHealth, float HealthDelta);

    TWeakObjectPtr<UHealthComponent> ObservedHealthComponent;
    FDelegateHandle HealthChangedHandle;
};
```

```cpp
// CombatObserver.cpp

#include "CombatObserver.h"
#include "HealthComponent.h"

void UCombatObserver::StartObserving(UHealthComponent* HealthComponent)
{
    StopObserving();

    if (!IsValid(HealthComponent))
    {
        return;
    }

    ObservedHealthComponent = HealthComponent;
    HealthChangedHandle = HealthComponent->OnHealthChanged.AddUObject(
        this,
        &UCombatObserver::HandleHealthChanged);
}

void UCombatObserver::StopObserving()
{
    if (UHealthComponent* HealthComponent = ObservedHealthComponent.Get())
    {
        HealthComponent->OnHealthChanged.Remove(HealthChangedHandle);
    }

    HealthChangedHandle.Reset();
    ObservedHealthComponent.Reset();
}

void UCombatObserver::HandleHealthChanged(
    const float NewHealth,
    const float HealthDelta)
{
    UE_LOG(
        LogTemp,
        Log,
        TEXT("Health changed: NewHealth=%.1f Delta=%.1f"),
        NewHealth,
        HealthDelta);
}
```

还可以同时注册另一个监听者：

```cpp
const FDelegateHandle AudioHandle = HealthComponent->OnHealthChanged.AddLambda(
    [](const float NewHealth, const float HealthDelta)
    {
        if (HealthDelta < 0.0f)
        {
            UE_LOG(LogTemp, Log, TEXT("Play hurt sound"));
        }
    });
```

发布者只调用一次 `Broadcast()`，所有当前有效的监听者都会收到通知。这比发布者逐个调用 UI、音效和任务对象更容易扩展。

### 4.3 `FDelegateHandle` 的作用

`Add...()` 通常返回一个 `FDelegateHandle`。它是某次绑定的标识，可用于精确移除该监听：

```cpp
FDelegateHandle Handle = Publisher.OnChanged.AddUObject(
    this,
    &ThisClass::HandleChanged);

Publisher.OnChanged.Remove(Handle);
Handle.Reset();
```

如果同一个对象的同一个函数可能被绑定多次，句柄可以明确指出要删除哪一次绑定。`RemoveAll(this)` 会移除目标对象在该委托上的全部绑定，范围更大，应根据实际意图选择。

多播委托不提供返回值。多个监听者同时返回不同结果时，没有通用方式决定应该采用哪个结果。如果业务需要收集结果，应由发布者显式遍历处理对象，或者改用单播委托。

## 5. 动态多播委托与蓝图 Event Dispatcher

当事件需要出现在蓝图中时，可以使用动态多播委托：

```cpp
// BlueprintHealthComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BlueprintHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnBlueprintHealthChanged,
    float,
    NewHealth,
    float,
    HealthDelta);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UBlueprintHealthComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Health|Events")
    FOnBlueprintHealthChanged OnHealthChanged;

    UFUNCTION(BlueprintCallable, Category = "Health")
    void ApplyDamage(float Damage);

private:
    UPROPERTY()
    float Health = 100.0f;
};
```

```cpp
void UBlueprintHealthComponent::ApplyDamage(const float Damage)
{
    const float OldHealth = Health;
    Health = FMath::Clamp(Health - FMath::Max(0.0f, Damage), 0.0f, 100.0f);

    if (!FMath::IsNearlyEqual(OldHealth, Health))
    {
        OnHealthChanged.Broadcast(Health, Health - OldHealth);
    }
}
```

编译后，蓝图可以对 `On Health Changed` 使用 `Bind Event`、`Assign` 或组件事件节点。

### 5.1 在 C++ 中绑定动态委托

动态委托使用 `AddDynamic()` 和 `RemoveDynamic()`，目标函数必须带 `UFUNCTION()`：

```cpp
UCLASS()
class UHealthWidgetController : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UBlueprintHealthComponent* HealthComponent)
    {
        if (IsValid(HealthComponent))
        {
            HealthComponent->OnHealthChanged.AddDynamic(
                this,
                &ThisClass::HandleHealthChanged);
        }
    }

private:
    UFUNCTION()
    void HandleHealthChanged(float NewHealth, float HealthDelta);
};
```

解绑时函数签名必须与绑定时一致：

```cpp
HealthComponent->OnHealthChanged.RemoveDynamic(
    this,
    &ThisClass::HandleHealthChanged);
```

`AddUniqueDynamic()` 可以避免同一个对象与函数组合被重复添加。

### 5.2 为什么动态委托更慢

原生委托保存可直接执行的 C++ 绑定信息。动态委托需要 UE 反射系统识别 `UObject` 和 `UFUNCTION`，并支持蓝图及序列化，因此调用路径更长。

这不意味着动态委托不能用于游戏逻辑。通常只要遵循下面的选择原则即可：

- 需要蓝图绑定：使用动态多播委托；
- 纯 C++ 高频通知：优先原生委托；
- 只有一个完成回调：优先单播委托；
- 不要仅为了“以后可能用蓝图”而把所有委托都改成动态委托。

## 6. 动态单播委托作为函数参数

动态单播委托常用于向蓝图节点提供回调引脚：

下面的类需要在头文件中包含 `Kismet/BlueprintFunctionLibrary.h`。

```cpp
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnConfirmResult, bool, bConfirmed);

UCLASS()
class UDialogLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(
        BlueprintCallable,
        Category = "Dialog",
        meta = (AutoCreateRefTerm = "Callback"))
    static void ShowConfirmDialog(const FOnConfirmResult& Callback);
};
```

执行前检查是否已经绑定：

```cpp
void UDialogLibrary::ShowConfirmDialog(const FOnConfirmResult& Callback)
{
    const bool bConfirmed = true;

    if (Callback.IsBound())
    {
        Callback.Execute(bConfirmed);
    }
}
```

动态委托的函数参数应使用 UE 反射支持的类型。自定义结构体若作为动态委托参数，应声明为 `USTRUCT()`，而且必须在委托宏之前完成定义，否则 UnrealHeaderTool 无法取得它的反射信息。

```cpp
USTRUCT(BlueprintType)
struct FOperationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSucceeded = false;
};

// 必须放在 FOperationResult 的完整 USTRUCT 定义之后。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnOperationCompleted,
    FOperationResult,
    Result);
```

仅写普通 C++ 前向声明通常不足以满足 UHT 对动态委托参数的处理需求。

## 7. 常用绑定方式

### 7.1 原生单播与多播

| 绑定方式 | 目标 | 生命周期特点 |
| --- | --- | --- |
| `BindUObject` / `AddUObject` | `UObject` 成员函数 | UObject 失效后不会继续安全执行该绑定 |
| `BindSP` / `AddSP` | 共享指针对象 | 使用弱引用语义检查对象是否仍然有效 |
| `BindRaw` / `AddRaw` | 普通 C++ 裸指针对象 | 不跟踪生命周期，销毁前必须解绑 |
| `BindStatic` / `AddStatic` | 静态或自由函数 | 不依赖对象实例 |
| `BindLambda` / `AddLambda` | Lambda | 捕获对象的生命周期由开发者负责 |
| `AddWeakLambda` | UObject + Lambda | UObject 失效后不再执行 Lambda |

示例：

```cpp
NativeDelegate.AddUObject(this, &ThisClass::HandleEvent);
NativeDelegate.AddStatic(&HandleGlobalEvent);

NativeDelegate.AddWeakLambda(this, [this](const int32 Value)
{
    UpdateState(Value);
});
```

不要使用捕获 `this` 的普通 `AddLambda()` 绑定一个可能比当前对象活得更久的发布者。即使 Lambda 本身仍在委托列表中，被捕获的对象也可能已经销毁。此时应使用 `AddWeakLambda()`，或者保存句柄并在对象结束生命周期前解绑。

### 7.2 动态委托

| 操作 | 单播 | 多播 |
| --- | --- | --- |
| 绑定 | `BindDynamic` | `AddDynamic` / `AddUniqueDynamic` |
| 执行 | `Execute` / `ExecuteIfBound` | `Broadcast` |
| 解绑 | `Unbind` | `RemoveDynamic` / `Clear` |

这些宏要求接收函数是 `UFUNCTION()`，并且参数类型、顺序及 `const` 约束必须匹配委托签名。

## 8. Event：限制广播权限

公开一个普通多播委托意味着外部代码不仅能绑定，还可能直接调用 `Broadcast()`。如果希望外部只能监听，而只有拥有者可以广播，可以使用 Event 宏：

```cpp
DECLARE_EVENT_OneParam(UInventoryComponent, FOnItemCountChanged, int32);

UCLASS()
class UInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    FOnItemCountChanged& OnItemCountChanged()
    {
        return ItemCountChangedEvent;
    }

private:
    void NotifyItemCountChanged()
    {
        ItemCountChangedEvent.Broadcast(ItemCount);
    }

    int32 ItemCount = 0;
    FOnItemCountChanged ItemCountChangedEvent;
};
```

外部可以调用 `OnItemCountChanged().AddUObject(...)`，但广播接口只对声明 Event 的拥有类型开放。它适合需要严格维护“谁有权发布事件”的公共 C++ API。

## 9. 参数、载荷与签名匹配

广播参数是在执行委托时提供的：

```cpp
DECLARE_DELEGATE_OneParam(FOnValueChanged, int32);

FOnValueChanged Callback;
Callback.ExecuteIfBound(42);
```

原生委托还可以在绑定时追加载荷参数。载荷不属于广播参数，而是跟随某次绑定保存：

```cpp
void HandleValueChanged(int32 NewValue, FString SourceName);

Callback.BindUObject(
    this,
    &ThisClass::HandleValueChanged,
    FString(TEXT("Inventory")));

Callback.ExecuteIfBound(42);
```

执行时 `NewValue` 为 `42`，绑定时保存的 `SourceName` 为 `Inventory`。

为了减少复制，可以对较大的只读参数使用 `const Type&`。但不要广播指向临时对象的引用，并假设监听者会长期保存它。`Broadcast()` 是同步调用，若监听者需要在回调结束后继续使用数据，应自行复制或使用明确的所有权类型。

## 10. 生命周期与解绑策略

### 10.1 推荐的成对管理

根据对象类型，在明确的开始和结束位置成对绑定、解绑：

```cpp
void UExampleComponent::BeginPlay()
{
    Super::BeginPlay();

    if (IsValid(SourceComponent))
    {
        ChangedHandle = SourceComponent->OnChanged.AddUObject(
            this,
            &ThisClass::HandleChanged);
    }
}

void UExampleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IsValid(SourceComponent))
    {
        SourceComponent->OnChanged.Remove(ChangedHandle);
    }

    ChangedHandle.Reset();
    Super::EndPlay(EndPlayReason);
}
```

常见绑定位置包括 `BeginPlay()`、`OnRegister()`、初始化函数或 UI 打开时；对应解绑位置包括 `EndPlay()`、`OnUnregister()`、反初始化函数或 UI 关闭时。

### 10.2 UObject 安全不等于不用管理

`AddUObject()` 能识别已经失效的 UObject，避免像裸指针绑定那样直接调用已销毁对象。但显式解绑仍然有价值：

- 表达监听关系何时结束；
- 防止仍然存活但已经不该接收事件的对象继续响应；
- 避免长期发布者保留无用绑定项；
- 防止重复初始化造成重复通知。

### 10.3 Raw 与 Lambda 的风险

`AddRaw()` 不知道普通 C++ 对象何时销毁。对象销毁前没有解绑，就可能访问悬空指针。

普通 `AddLambda()` 同样不会自动理解捕获对象的生命周期。尤其要警惕：

```cpp
// 如果 Publisher 比 this 活得更久，这个绑定存在风险。
Publisher.OnChanged.AddLambda([this]()
{
    Refresh();
});
```

优先改用 `AddWeakLambda(this, ...)` 或保存 `FDelegateHandle` 并可靠解绑。

## 11. 广播时需要知道的行为边界

- `Broadcast()` 是同步调用；所有监听逻辑会在广播所在的调用栈中执行。
- 不要依赖多个监听者的执行顺序，业务正确性不应建立在绑定先后上。
- 监听者可能在回调中解绑自己或其他监听者，不应缓存并长期依赖委托内部列表状态。
- 委托本身不是跨线程调度器。从工作线程广播，并不会自动把回调切换到游戏线程。
- UObject 和大多数 Gameplay API 应在游戏线程访问。工作线程完成后，可显式调度到游戏线程再广播。
- 高频事件中应避免昂贵的动态委托、频繁临时分配和过多监听者工作。
- 不要把委托当成网络复制。`Broadcast()` 只发生在当前进程、当前 World 实例中；多人同步仍需 RPC 或属性复制。

切回游戏线程的简化示例：

```cpp
// 需要包含 Async/Async.h；UExampleComponent 代表当前 UObject 的实际类型。
TWeakObjectPtr<UExampleComponent> WeakThis(this);

AsyncTask(ENamedThreads::GameThread, [WeakThis]()
{
    if (WeakThis.IsValid())
    {
        WeakThis->OnOperationFinished.Broadcast();
    }
});
```

## 12. 常见编译与 UHT 错误

### 12.1 `code generation hash is zero`

动态委托引用了尚未完成反射解析的 `USTRUCT`，常见原因是结构体定义在委托宏之后、缺少直接 include，或存在循环依赖。

处理顺序：

1. 确保参数结构体是 `USTRUCT()` 并包含 `GENERATED_BODY()`；
2. 把完整结构体定义放到动态委托声明之前；
3. 直接 include 定义该反射类型的头文件；
4. 再检查模块依赖与循环 include。

### 12.2 `AddDynamic` 找不到匹配函数

检查目标函数：

- 是否标记 `UFUNCTION()`；
- 参数数量与顺序是否一致；
- 值、引用和 `const` 是否匹配；
- 目标对象是否是 UObject 类型；
- 是否误把原生委托的绑定方式用于动态委托。

### 12.3 `Execute` 或 `Broadcast` 使用错误

- 单播委托使用 `Execute()` 或 `ExecuteIfBound()`；
- 多播委托使用 `Broadcast()`；
- 多播委托没有返回值；
- 动态多播委托不能使用原生多播委托的 `AddUObject()` 代替 `AddDynamic()`。

### 12.4 重复收到事件

通常是初始化函数被执行多次，但旧绑定没有移除。可以：

- 保存并移除 `FDelegateHandle`；
- 动态多播使用 `AddUniqueDynamic()`；
- 重新绑定前先解绑；
- 检查绑定和解绑是否处在对称的生命周期阶段。

## 13. 选择速查

```text
是否需要蓝图绑定或反射？
├─ 否
│  ├─ 只需要一个回调 → 原生单播委托
│  └─ 需要多个监听者 → 原生多播委托 / Event
└─ 是
   ├─ 只需要一个回调 → 动态单播委托
   └─ 需要多个监听者 → 动态多播委托 + BlueprintAssignable
```

推荐习惯：

- 默认从原生委托开始，只有需要蓝图或反射时才使用动态委托；
- 一对多通知使用多播，一次完成回调使用单播；
- 对公共 C++ API，可用 Event 限制外部广播权限；
- 保存 `FDelegateHandle`，并把解绑纳入对象生命周期；
- UObject 使用 `AddUObject()`，共享对象使用 `AddSP()`；
- 谨慎使用 `AddRaw()` 和捕获裸 `this` 的 `AddLambda()`；
- 不依赖多播监听者顺序，不把委托误认为线程切换或网络复制机制。

## 14. 最小记忆模板

纯 C++ 多播事件：

```cpp
DECLARE_MULTICAST_DELEGATE_OneParam(FOnValueChanged, int32);

FOnValueChanged OnValueChanged;

FDelegateHandle Handle = OnValueChanged.AddUObject(
    this,
    &ThisClass::HandleValueChanged);

OnValueChanged.Broadcast(42);
OnValueChanged.Remove(Handle);
```

蓝图 Event Dispatcher：

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnValueChangedDynamic,
    int32,
    NewValue);

UPROPERTY(BlueprintAssignable, Category = "Events")
FOnValueChangedDynamic OnValueChanged;

OnValueChanged.Broadcast(42);
```

掌握“声明签名 → 选择安全绑定方式 → 执行或广播 → 在正确生命周期解绑”这条主线，就掌握了 UE 委托的大部分实际用法。
