# UE5 委托：从基础绑定到多人会话异步回调

本文以 BrokenEdge 项目的 `UMultiplayerSessionsSubsystem` 为主要示例，说明 Unreal Engine 5 中委托的类型、声明、绑定、执行、解绑和生命周期管理。

相关项目源码：

- [`MultiplayerSessionsSubsystem.h`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/public/MultiplayerSessionsSubsystem.h)
- [`MultiplayerSessionsSubsystem.cpp`](../Plugins/MultiplayerSessions/Source/MultiplayerSessions/Private/MultiplayerSessionsSubsystem.cpp)

## 1. 委托解决什么问题

普通函数调用要求调用方知道具体对象和函数：

```cpp
Menu->HandleCreateSessionComplete(true);
```

委托把“事件发生”和“由谁处理”解耦：

```cpp
MultiplayerOnCreateSessionComplete.Broadcast(true);
```

会话子系统只负责报告“创建完成”，不需要知道菜单、HUD 或其他系统会如何响应。监听者可以加入或退出，事件发送者不需要随之修改。

典型用途包括：

- 异步操作完成通知，例如创建、搜索和加入会话。
- UI 按钮与业务逻辑之间的通知。
- Actor、组件、子系统之间的一对多事件。
- C++ 向 Blueprint 暴露 Event Dispatcher。
- 延迟回调和模块间解耦。

## 2. UE 委托的主要类型

| 类型 | 常见声明宏 | 监听者数量 | Blueprint/反射 | 常见调用方式 |
|---|---|---:|---:|---|
| 原生单播委托 | `DECLARE_DELEGATE...` | 1 | 否 | `ExecuteIfBound()` |
| 原生多播委托 | `DECLARE_MULTICAST_DELEGATE...` | 多个 | 否 | `Broadcast()` |
| 动态单播委托 | `DECLARE_DYNAMIC_DELEGATE...` | 1 | 是 | `ExecuteIfBound()` |
| 动态多播委托 | `DECLARE_DYNAMIC_MULTICAST_DELEGATE...` | 多个 | 是 | `Broadcast()` |
| Event | `DECLARE_EVENT...` | 多个 | 否 | 仅拥有者可广播 |

“原生”委托通常更轻量并具有更强的 C++ 类型表达能力；“动态”委托通过 UE 反射系统保存函数信息，因此能够序列化并与 Blueprint 交互，但有额外开销。

### 2.1 原生单播委托

单播委托一次只能绑定一个回调。再次绑定会替换旧绑定。

```cpp
DECLARE_DELEGATE_OneParam(FOnHealthChanged, float);

FOnHealthChanged OnHealthChanged;
OnHealthChanged.BindUObject(this, &ThisClass::HandleHealthChanged);
OnHealthChanged.ExecuteIfBound(75.0f);
```

单播委托可以声明返回值：

```cpp
DECLARE_DELEGATE_RetVal_OneParam(bool, FCanJoinMatch, int32);
```

### 2.2 原生多播委托

多播委托可以同时通知多个监听者：

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnScoreChanged, int32, int32);

FOnScoreChanged OnScoreChanged;

const FDelegateHandle Handle = OnScoreChanged.AddUObject(
	this,
	&ThisClass::HandleScoreChanged);

OnScoreChanged.Broadcast(10, 20);
OnScoreChanged.Remove(Handle);
```

多播委托没有有意义的统一返回值，因为多个监听者可能返回不同结果。需要返回结果时，应使用单播委托或重新设计数据流。

### 2.3 动态多播委托与 Blueprint

当前会话子系统对创建结果采用动态多播委托：

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnCreateSessionComplete,
	bool,
	bWasSuccessful);

UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
FMultiplayerOnCreateSessionComplete MultiplayerOnCreateSessionComplete;
```

`BlueprintAssignable` 会让它在 Blueprint 中表现为 Event Dispatcher。C++ 监听动态委托时，回调必须是 `UFUNCTION`：

```cpp
UFUNCTION()
void HandleCreateSessionComplete(bool bWasSuccessful);

MultiplayerOnCreateSessionComplete.AddDynamic(
	this,
	&ThisClass::HandleCreateSessionComplete);
```

解绑可以使用：

```cpp
MultiplayerOnCreateSessionComplete.RemoveDynamic(
	this,
	&ThisClass::HandleCreateSessionComplete);
```

如果不需要 Blueprint、序列化或按函数名调用，优先使用原生委托。

### 2.4 Event 与公开多播委托

普通多播委托如果被公开，外部代码不仅可以监听，也可能调用 `Broadcast()` 或清空全部监听者。`DECLARE_EVENT` 可以把广播权限限制给事件拥有者：

```cpp
DECLARE_EVENT_OneParam(UHealthComponent, FOnHealthChangedEvent, float);

FOnHealthChangedEvent& OnHealthChanged()
{
	return HealthChangedEvent;
}

private:
	FOnHealthChangedEvent HealthChangedEvent;
```

外部可以订阅返回的事件引用，但不能合法地代替 `UHealthComponent` 广播事件。

## 3. `CreateUObject` 与 `BindUObject`

二者都创建 UObject 成员函数绑定，并通过弱 UObject 引用避免在对象失效后调用悬空地址。区别是使用形式。

### 3.1 创建一个新的委托值

`CreateUObject` 是静态工厂函数，返回一个已绑定的委托对象：

```cpp
FOnCreateSessionCompleteDelegate Delegate =
	FOnCreateSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleCreateSessionComplete);
```

适合以下场景：

- 初始化局部委托。
- 初始化或赋值成员变量。
- 直接把委托作为函数参数传递。
- 从函数返回一个委托值。

### 3.2 修改一个已经存在的委托对象

`BindUObject` 直接修改现有单播委托：

```cpp
CreateSessionCompleteDelegate.BindUObject(
	this,
	&ThisClass::HandleCreateSessionComplete);
```

它适合“类里已经有一个委托成员，现在给它绑定处理函数”的表达方式。若该委托原来有绑定，新绑定会替换旧绑定。

下面两种写法在本项目里基本等价：

```cpp
CreateSessionCompleteDelegate =
	FOnCreateSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleCreateSessionComplete);
```

```cpp
CreateSessionCompleteDelegate.BindUObject(
	this,
	&ThisClass::HandleCreateSessionComplete);
```

选择规则可以简化为：

- 需要“一个新的委托值”：使用 `CreateUObject`。
- 已经有委托变量，需要“绑定这个变量”：使用 `BindUObject`。
- 多播委托不是 `BindUObject`，而是 `AddUObject`。

## 4. 根据对象所有权选择绑定函数

委托可能在未来某个时间才执行，所以选择正确的生命周期策略非常重要。

| 目标 | 单播绑定 | 创建委托值 | 多播添加 | 生命周期特征 |
|---|---|---|---|---|
| `UObject` | `BindUObject` | `CreateUObject` | `AddUObject` | 弱 UObject 引用 |
| `TSharedPtr` 管理对象 | `BindSP` | `CreateSP` | `AddSP` | 弱共享指针 |
| 普通裸指针对象 | `BindRaw` | `CreateRaw` | `AddRaw` | 不检查生命周期 |
| 静态/全局函数 | `BindStatic` | `CreateStatic` | `AddStatic` | 不依赖对象 |
| Lambda | `BindLambda` | `CreateLambda` | `AddLambda` | 捕获生命周期由调用者负责 |
| UObject 弱 Lambda | `BindWeakLambda` | `CreateWeakLambda` | `AddWeakLambda` | UObject 失效后不调用 |

### 4.1 为什么 UE 对 UObject 使用弱引用

`BindUObject`、`CreateUObject` 和 `AddUObject` 不会因为委托绑定而阻止 UObject 被垃圾回收。执行时，如果 UObject 已经失效，委托不会调用那个成员函数。

这避免了悬空调用，但不代表可以完全忽略解绑：

- 无效绑定仍可能留在长期存在的多播列表中。
- 重复绑定可能制造重复通知。
- 异步接口可能一直保存注册记录。
- 明确解绑更容易保证对象生命周期和请求生命周期一致。

### 4.2 `BindRaw` 的风险

`BindRaw` 不知道裸指针对象是否已经销毁：

```cpp
Delegate.BindRaw(Listener, &FListener::HandleEvent);
```

如果 `Listener` 先被释放，之后执行委托就可能访问无效内存。除非生命周期关系非常明确，否则 UObject 使用 `BindUObject`，共享对象使用 `BindSP`。

### 4.3 Lambda 捕获的风险

下面的委托不会自动知道 `this` 是否还有效：

```cpp
Delegate.BindLambda([this]()
{
	RefreshUI();
});
```

对于 UObject，可以改为：

```cpp
Delegate.BindWeakLambda(this, [this]()
{
	RefreshUI();
});
```

即使使用弱 Lambda，其他按引用捕获的变量仍需要调用方保证生命周期。

## 5. 单播执行、多播广播与安全检查

### 5.1 单播委托

```cpp
if (Delegate.IsBound())
{
	Delegate.Execute(Value);
}
```

更常用的无返回值安全写法是：

```cpp
Delegate.ExecuteIfBound(Value);
```

未绑定时直接调用 `Execute()` 会触发检查失败。

### 5.2 多播委托

```cpp
OnSomethingHappened.Broadcast(Value);
```

多播委托可以在没有监听者时安全广播。通常不需要先调用 `IsBound()`。

不要依赖多个监听者的执行顺序。若业务逻辑要求严格先后关系，应显式编排调用，而不是把多播委托当作有序任务队列。

## 6. `FDelegateHandle`：精确取消一个订阅

原生多播委托的 `Add...` 通常返回 `FDelegateHandle`：

```cpp
FDelegateHandle ScoreChangedHandle;

ScoreChangedHandle = OnScoreChanged.AddUObject(
	this,
	&ThisClass::HandleScoreChanged);
```

解绑时必须向同一个委托实例交回该句柄：

```cpp
if (ScoreChangedHandle.IsValid())
{
	OnScoreChanged.Remove(ScoreChangedHandle);
	ScoreChangedHandle.Reset();
}
```

常见错误包括：

- 只保存回调函数，不保存返回的句柄。
- 在另一个委托对象上调用 `Remove(Handle)`。
- 移除后不 `Reset()`，导致代码误认为请求仍在进行。
- 每次调用业务函数都重复 `AddUObject()`，却从不移除。

还可以使用 `RemoveAll(this)` 一次移除某个对象在该多播委托上的全部绑定，但精确句柄更适合管理多个独立异步请求。

## 7. 当前多人会话子系统里的三层委托

会话代码里出现了多个名字相近的 Delegate 和 Handle。它们属于三层不同职责。

### 7.1 第一层：保存一个 OSS 回调函数

```cpp
FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
```

它是单播委托，表示：OSS 创建完成时，调用本对象的哪个函数。

初始化时绑定：

```cpp
CreateSessionCompleteDelegate =
	FOnCreateSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleCreateSessionComplete);
```

### 7.2 第二层：把回调注册到 OSS 的完成事件

```cpp
CreateSessionCompleteDelegateHandle =
	SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegate);
```

这里的 `SessionInterface` 内部维护多个完成监听者。返回的 `FDelegateHandle` 标识“本子系统的这一项注册”。

开始异步请求前必须先注册，因为某些实现可能很快完成请求：

```cpp
CreateSessionCompleteDelegateHandle =
	SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegate);

const bool bRequestStarted = SessionInterface->CreateSession(...);
```

如果 `CreateSession()` 立即返回 `false`，表示请求根本没有成功启动，通常不会再收到完成回调，因此必须马上清理句柄并主动报告失败。

### 7.3 第三层：向插件使用者广播统一结果

OSS 回调进入子系统后，先移除底层注册，再向外广播：

```cpp
void UMultiplayerSessionsSubsystem::HandleCreateSessionComplete(
	FName SessionName,
	bool bWasSuccessful)
{
	SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegateHandle);
	CreateSessionCompleteDelegateHandle.Reset();

	MultiplayerOnCreateSessionComplete.Broadcast(bWasSuccessful);
}
```

完整数据流是：

```text
CreateSession()
    │
    ├─ 注册 FOnCreateSessionCompleteDelegate
    │      └─ 得到 FDelegateHandle
    │
    ├─ 调用 IOnlineSession::CreateSession()
    │
    └─ OSS 异步完成
           │
           └─ HandleCreateSessionComplete()
                  ├─ 清理 OSS 注册句柄
                  └─ Broadcast 给 UI / Blueprint / 其他系统
```

所以这三个成员不是重复设计：

| 成员 | 作用 |
|---|---|
| `CreateSessionCompleteDelegate` | 描述 OSS 应回调哪个 C++ 函数 |
| `CreateSessionCompleteDelegateHandle` | 标识该回调在 OSS 中的注册，供清理使用 |
| `MultiplayerOnCreateSessionComplete` | 将最终结果广播给插件外部监听者 |

## 8. 异步委托的正确生命周期

一个稳妥的异步请求通常遵循以下流程：

1. 确认接口有效。
2. 阻止或处理同类重复请求。
3. 准备需要跨越异步过程存活的数据。
4. 注册完成委托并保存 `FDelegateHandle`。
5. 发起异步请求。
6. 请求未启动时立即解绑并广播失败。
7. 完成回调触发时先解绑，再处理结果。
8. 对象 `Deinitialize()` 时清理所有仍未完成的注册。

当前项目使用成员保存搜索和创建设置：

```cpp
TSharedPtr<FOnlineSessionSettings> LastSessionSettings;
TSharedPtr<FOnlineSessionSearch> LastSessionSearch;
```

这是因为 OnlineSubsystem 可能在当前函数返回后才读取这些对象。不能把需要跨越异步调用的数据仅保存在短生命周期局部变量中。

## 9. 原生多播和动态多播如何选择

当前头文件同时使用了两种多播委托：

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnCreateSessionComplete,
	bool,
	bWasSuccessful);
```

创建结果只有一个 Blueprint 支持的 `bool`，所以可以作为 `BlueprintAssignable` 的动态多播委托。

搜索结果使用原生多播：

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FMultiplayerOnFindSessionsComplete,
	const TArray<FOnlineSessionSearchResult>&,
	bool);
```

`FOnlineSessionSearchResult` 不是适合直接暴露给 Blueprint 的反射类型，因此这里保留 C++ 原生委托。

选择时可以按以下顺序判断：

1. 是否需要多个监听者？需要则选多播，否则选单播。
2. 是否必须暴露给 Blueprint 或序列化？需要则选动态委托。
3. 参数是否都是 UE 反射系统支持的类型？不是则保留原生 C++ 委托，或定义 Blueprint 友好的包装 `USTRUCT`。
4. 是否需要返回值？需要则优先单播委托。

## 10. 常见编译和运行问题

### 10.1 回调签名不完全匹配

参数数量、类型、`const`、引用和成员函数的 `const` 限定都必须满足委托签名：

```cpp
// 委托要求：void(FName, bool)
void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
```

不要随意改成 `const FString&` 或交换参数顺序。

### 10.2 动态绑定函数没有 `UFUNCTION`

下面的绑定要求回调函数参与反射：

```cpp
DynamicDelegate.AddDynamic(this, &ThisClass::HandleEvent);
```

对应声明必须有：

```cpp
UFUNCTION()
void HandleEvent(bool bWasSuccessful);
```

原生 `AddUObject` 和 `BindUObject` 不要求 `UFUNCTION`。

### 10.3 把单播 API 用在多播委托上

```cpp
SingleDelegate.BindUObject(...);   // 单播
MulticastDelegate.AddUObject(...); // 多播
```

多播委托使用 `Add...`，因为它是在监听列表中增加一项，而不是替换唯一绑定。

### 10.4 绑定后忘记执行或广播

绑定只是在登记回调，不会自动执行：

```cpp
Delegate.BindUObject(...); // 登记
Delegate.ExecuteIfBound(); // 执行

Event.AddUObject(...);     // 订阅
Event.Broadcast();         // 广播
```

### 10.5 只依靠 UObject 弱引用，不主动清理注册

弱引用可以避免调用已销毁 UObject，但仍应在异步完成、取消请求或 `Deinitialize()` 时清理句柄。这样可以避免重复绑定和长期积累的无效注册。

### 10.6 在高频函数里重复绑定

不要在 `Tick()` 或每次按钮点击时无条件 `AddUObject()`。如果确实需要临时监听，应保存句柄并在正确时机移除，或者先检查当前是否已经注册。

## 11. 快速选择表

遇到新需求时可以直接按下面选择：

| 需求 | 推荐方案 |
|---|---|
| 一个 C++ 回调 | `DECLARE_DELEGATE` + `BindUObject` |
| 多个 C++ 监听者 | `DECLARE_MULTICAST_DELEGATE` + `AddUObject` |
| Blueprint Event Dispatcher | `DECLARE_DYNAMIC_MULTICAST_DELEGATE` + `BlueprintAssignable` |
| Blueprint 传入一个回调参数 | `DECLARE_DYNAMIC_DELEGATE` |
| 只允许事件拥有者广播 | `DECLARE_EVENT` |
| 绑定 UObject | `BindUObject` / `AddUObject` |
| 绑定共享对象 | `BindSP` / `AddSP` |
| 创建后立即作为参数传递 | `CreateUObject` / `CreateSP` / `CreateLambda` |
| 精确取消原生多播监听 | 保存并移除 `FDelegateHandle` |
| 异步 OnlineSubsystem 回调 | 先注册、再请求、立即失败和完成时都清理 |

## 12. 总结

可以把 UE 委托记成三个问题：

1. **谁能监听？** 单播一个，多播多个。
2. **是否需要反射？** Blueprint/序列化需要动态委托，纯 C++ 优先原生委托。
3. **目标对象如何存活？** UObject 用 `UObject` 系列，共享对象用 `SP` 系列，裸指针和普通 Lambda 必须自己保证生命周期。

在当前多人会话子系统中，最重要的实践不是某一个宏，而是完整的异步闭环：注册完成委托、保存句柄、发起请求、处理立即失败、收到回调后解绑，并在子系统退出时清理所有尚未完成的注册。
