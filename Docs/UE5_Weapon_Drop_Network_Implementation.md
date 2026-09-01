# BrokenEdge 多人武器丢弃功能实现指南

> 项目：`C:\UnrealProjects\BrokenEdge`
>
> 引擎版本：Unreal Engine 5.5.4
>
> 检查日期：2026-09-01
>
> 文档性质：基于当前源码的静态分析与推荐实现
>
> 当前状态：本文档已创建，但文中的 C++ 修改尚未写入项目，也未执行编译、PIE 或联机验证

## 1. 结论先行

武器丢弃功能不应该写在 `ProjectileBullet.cpp` 中。投射物只负责飞行、碰撞和伤害，丢弃武器属于角色持有状态与武器 Actor 状态之间的切换，应由以下三个层次协作：

```text
ABrokenEdgeCharacter
    负责接收 DropAction 输入

UCombatComponent
    负责发起服务器 RPC
    负责修改 EquippedWeapon / SecondaryWeapon
    负责停止开火、退出瞄准、刷新 HUD

AWeapon
    负责解除附着、清除 Owner
    负责恢复物理、重力、碰撞和拾取范围
```

当前项目已经完成了输入、武器物理掉落和属性复制的大部分基础工作。真正缺少的是：

1. 客户端按下丢弃键后，将请求发送给服务器；
2. 只允许服务器修改 `EquippedWeapon` 和武器状态；
3. 武器变为空时清理 HUD、瞄准、角色旋转和相机 FOV；
4. 有副武器时，把副武器提升为当前武器；
5. 避免丢枪时尚未结束的开火计时器把 `bCanFire` 永久留在 `false`。

## 2. 当前项目已经具备的基础

### 2.1 输入链路已经存在

当前角色已经声明并绑定 `DropAction`：

- [`BrokenEdgeCharacter.h`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.h#L220)：声明 `DropAction`；
- [`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp#L408)：把输入绑定到 `DropButtonPressed()`；
- [`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp#L733)：调用 `Combat->Drop()`。

项目中也已经存在以下 Enhanced Input 资产：

```text
Content/Input/Actions/IA_Drop.uasset
Content/Input/IMC_CharacterDefault.uasset
```

这里只确认了资产文件存在。由于 `.uasset` 是二进制资源，仍需在编辑器中确认角色蓝图已经给 `DropAction` 赋值，并确认 `IMC_CharacterDefault` 中确实绑定了按键。

### 2.2 武器掉落表现已经存在

[`AWeapon::Dropped()`](../Source/BrokenEdge/Weapon/Weapon.cpp#L223) 当前已经负责：

```text
WeaponState = EWS_Dropped
    -> 解除武器网格与角色的附着
    -> SetOwner(nullptr)
    -> 清空武器缓存的角色和控制器
```

[`AWeapon::OnDropped()`](../Source/BrokenEdge/Weapon/Weapon.cpp#L330) 当前已经负责：

```text
服务器开启 AreaSphere 拾取检测
所有实例开启武器物理模拟
开启重力
开启 QueryAndPhysics
忽略 Pawn 和 Camera 碰撞
恢复蓝色描边
```

`AWeapon` 构造函数已经设置：

```cpp
bReplicates = true;
SetReplicateMovement(true);
```

`WeaponState` 也已使用 `ReplicatedUsing = OnRep_WeaponState`，并在 `GetLifetimeReplicatedProps()` 中注册。因此正确做法是让服务器调用一次 `Dropped()`，再由属性复制和移动复制同步到客户端，不需要额外编写丢枪 Multicast RPC。

### 2.3 持有武器指针已经复制

[`UCombatComponent::GetLifetimeReplicatedProps()`](../Source/BrokenEdge/Component/CombatComponent.cpp#L31) 已经复制：

```cpp
DOREPLIFETIME(UCombatComponent, EquippedWeapon);
DOREPLIFETIME(UCombatComponent, SecondaryWeapon);
```

角色构造函数也已经执行：

```cpp
Combat->SetIsReplicated(true);
```

因此 `UCombatComponent` 可以承载由所属客户端调用的 Server RPC。

## 3. 当前实现为什么有问题

当前 [`UCombatComponent::Drop()`](../Source/BrokenEdge/Component/CombatComponent.cpp#L155) 是：

```cpp
void UCombatComponent::Drop()
{
	//@TODO 有bug
	if (EquippedWeapon)
	{
		EquippedWeapon->Dropped();
		EquippedWeapon = nullptr;
	}
}
```

`DropButtonPressed()` 是由本地玩家输入调用的，因此远程客户端会在自己的客户端实例上直接执行上面的代码。这会产生以下问题：

1. 客户端无权把自己的结果复制回服务器；
2. 服务器仍然认为角色持有原武器；
3. 客户端本地把 `WeaponState` 改成 `EWS_Dropped`，但服务器并没有发生状态变化；
4. 后续服务器复制可能把客户端的 `EquippedWeapon` 和武器状态恢复；
5. `OnRep_WeaponState()` 不会因为一次纯客户端赋值而收到服务器复制通知；
6. 不同机器可能看到不同的附着、物理和拾取状态。

核心原则是：

```text
客户端只请求丢枪
服务器决定能否丢枪并修改真实状态
客户端通过 RepNotify 接收结果
```

## 4. 推荐行为规则

本文推荐手动丢弃功能遵循以下规则：

| 场景 | 推荐行为 |
| --- | --- |
| 未装备武器 | 忽略输入 |
| 正在换弹或切枪 | 暂时拒绝丢弃 |
| 正在持旗 | 拒绝普通武器丢弃，旗帜使用独立归还/掉落流程 |
| 正在瞄准 | 退出瞄准并关闭狙击镜 |
| 正在持续开火 | 停止持续开火 |
| 只有一把武器 | 丢下武器并进入空手状态 |
| 还有副武器 | 丢下当前武器，把副武器提升到右手 |
| 掉落武器仍在角色脚边 | 可以再次按装备键拾取；若不希望立即拾取，可增加冷却 |

## 5. 推荐实现

### 5.1 在 CombatComponent.h 声明服务器 RPC

保留现有公开入口：

```cpp
void Drop();
```

在 `protected` 或 `private` 区域增加：

```cpp
/** 客户端向服务器请求丢弃当前武器。 */
UFUNCTION(Server, Reliable)
void ServerDrop();

/** 只在服务器执行真正的武器状态修改。 */
void DropOnServer();
```

不需要手动声明 `ServerDrop_Implementation()`；Unreal Header Tool 会生成 RPC 所需的包装代码，`.cpp` 中只实现 `_Implementation` 函数。

### 5.2 修改公开的 Drop() 入口

将当前 `UCombatComponent::Drop()` 替换为：

```cpp
void UCombatComponent::Drop()
{
	if (
		Character == nullptr ||
		EquippedWeapon == nullptr ||
		CombatState != ECombatState::ECS_Unoccuiped ||
		bHoldingTheFlag
	)
	{
		return;
	}

	// 本地立即停止持续开火，避免等待一个 RTT 后仍然连射。
	bFireButtonPressed = false;

	// 同时更新本地瞄准表现，并通过现有 ServerSetAiming RPC 更新服务器。
	SetAiming(false);

	if (Character->HasAuthority())
	{
		DropOnServer();
		return;
	}

	ServerDrop();
}
```

这里不在客户端调用 `EquippedWeapon->Dropped()`，也不在客户端把 `EquippedWeapon` 设为 `nullptr`。这样可以避免客户端预测状态与服务器权威状态互相覆盖。

当前方案只对“停止开火”和“退出瞄准”进行本地即时响应，武器真正脱手需要等待服务器确认。如果以后需要完整丢枪预测，应另外设计可回滚的预测状态，而不是提前修改复制属性。

### 5.3 实现 ServerDrop RPC

```cpp
void UCombatComponent::ServerDrop_Implementation()
{
	DropOnServer();
}
```

服务器函数必须再次检查条件，不能信任客户端第一次检查的结果。

### 5.4 在服务器执行真正的掉落

```cpp
void UCombatComponent::DropOnServer()
{
	if (
		Character == nullptr ||
		!Character->HasAuthority() ||
		EquippedWeapon == nullptr ||
		CombatState != ECombatState::ECS_Unoccuiped ||
		bHoldingTheFlag
	)
	{
		return;
	}

	bFireButtonPressed = false;
	bAiming = false;

	AWeapon* WeaponToDrop = EquippedWeapon;
	AWeapon* WeaponToPromote = SecondaryWeapon;

	// 先断开 CombatComponent 对当前武器的权威引用。
	EquippedWeapon = nullptr;

	// 只在服务器调用；WeaponState、Owner、附着和移动随后复制。
	WeaponToDrop->Dropped();

	if (WeaponToPromote)
	{
		SecondaryWeapon = nullptr;
		EquipPrimaryWeapon(WeaponToPromote);

		Character->GetCharacterMovement()->bOrientRotationToMovement = false;
		Character->bUseControllerRotationYaw = true;
	}
	else
	{
		Character->GetCharacterMovement()->bOrientRotationToMovement = true;
		Character->bUseControllerRotationYaw = false;

		// 权威实例不会执行自己的 OnRep，这段用于 Listen Server 主机。
		if (Character->IsLocallyControlled())
		{
			Controller = Controller == nullptr
				? Cast<ABrokenEdgePlayerController>(Character->GetController())
				: Controller;

			if (Controller)
			{
				Controller->SetHUDWeaponAmmo(0);
				Controller->SetHUDCarriedAmmo(0);
			}
		}
	}

	// 不是正确性的必要条件，但可以减少等待下一次常规网络更新的时间。
	WeaponToDrop->ForceNetUpdate();
	Character->ForceNetUpdate();
}
```

### 5.5 为什么要自动提升副武器

如果只执行：

```cpp
EquippedWeapon = nullptr;
```

却继续让 `SecondaryWeapon` 留在背包，当前 `ShouldSwapWeapons()` 会返回 `false`，因为它要求主副武器都非空。玩家此时无法通过现有切枪输入把背包武器拿到手上。

因此推荐：

```text
丢弃当前主武器
    -> 如果 SecondaryWeapon 存在
        -> SecondaryWeapon 置空
        -> 将旧副武器设置为 EquippedWeapon
        -> 挂到 RightHandSocket
```

如果玩法设计明确要求“丢枪后保持空手”，则还需要单独实现“从背包取出副武器”的输入和状态机，不能只把主武器指针清空。

## 6. 处理 EquippedWeapon 复制为空

当前 [`OnRep_EquippedWeapon()`](../Source/BrokenEdge/Component/CombatComponent.cpp#L688) 只处理非空武器。武器复制为 `nullptr` 时，客户端不会清理旋转模式、HUD 和狙击镜。

建议将函数整理为：

```cpp
void UCombatComponent::OnRep_EquippedWeapon()
{
	if (Character == nullptr) return;

	if (EquippedWeapon)
	{
		EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);
		AttachActorToRightHand(EquippedWeapon);

		Character->GetCharacterMovement()->bOrientRotationToMovement = false;
		Character->bUseControllerRotationYaw = true;

		EquippedWeapon->EnableCustomDepth(false);
		EquippedWeapon->SetHUDAmmo();
		return;
	}

	// EquippedWeapon 从有效武器变为 nullptr。
	bAiming = false;
	bAimButtonPressed = false;

	Character->GetCharacterMovement()->bOrientRotationToMovement = true;
	Character->bUseControllerRotationYaw = false;

	if (Character->IsLocallyControlled())
	{
		Character->ShowSniperScopeWidget(false);

		Controller = Controller == nullptr
			? Cast<ABrokenEdgePlayerController>(Character->GetController())
			: Controller;

		if (Controller)
		{
			Controller->SetHUDWeaponAmmo(0);
			Controller->SetHUDCarriedAmmo(0);
		}
	}
}
```

当服务器把副武器直接提升为主武器时，客户端收到的是“旧主武器指针变成新主武器指针”，会进入非空分支并把新武器挂到右手。

## 7. 修正开火计时器的空武器分支

当前 [`FireTimerFinished()`](../Source/BrokenEdge/Component/CombatComponent.cpp#L314) 在 `EquippedWeapon == nullptr` 时直接返回：

```cpp
if (EquippedWeapon == nullptr) return;
bCanFire = true;
```

如果玩家刚开火就丢弃唯一武器，计时器触发时会提前返回，客户端的 `bCanFire` 可能永久保持为 `false`。之后再拾取武器也无法开火。

应先恢复开火门控，再检查武器：

```cpp
void UCombatComponent::FireTimerFinished()
{
	bCanFire = true;

	if (EquippedWeapon == nullptr) return;

	if (bFireButtonPressed && EquippedWeapon->bAutomatic)
	{
		Fire();
	}

	ReloadEmptyWeapon();
}
```

不建议仅在客户端按下丢弃键时无条件清除计时器并把 `bCanFire` 设为 `true`。如果服务器最终拒绝丢弃，这种做法可能提前解除射速限制。

## 8. 修正无武器时的 FOV

当前 [`InterpFOV()`](../Source/BrokenEdge/Component/CombatComponent.cpp#L725) 在没有武器时直接返回。如果玩家正在使用狙击镜，并且丢弃后没有副武器，相机可能停留在尚未恢复完成的缩放 FOV。

建议让“没有武器”也以 `DefaultFOV` 为目标：

```cpp
void UCombatComponent::InterpFOV(float DeltaTime)
{
	if (Character == nullptr || Character->GetFollowCamera() == nullptr) return;

	float TargetFOV = DefaultFOV;
	float InterpSpeed = ZoomInterpSpeed;

	if (EquippedWeapon && bAiming)
	{
		TargetFOV = EquippedWeapon->ZoomedFOV;
		InterpSpeed = EquippedWeapon->ZoomInterpSpeed;
	}

	CurrentFOV = FMath::FInterpTo(
		CurrentFOV,
		TargetFOV,
		DeltaTime,
		InterpSpeed
	);

	Character->GetFollowCamera()->SetFieldOfView(CurrentFOV);
}
```

这样以下状态都会自然恢复默认视野：

- 松开瞄准键；
- 丢弃唯一武器；
- 服务器强制卸下武器；
- 淘汰流程清空武器。

## 9. AWeapon::Dropped() 保持为状态执行器

当前 [`AWeapon::Dropped()`](../Source/BrokenEdge/Weapon/Weapon.cpp#L223) 可以继续保留：

```cpp
void AWeapon::Dropped()
{
	SetWeaponState(EWeaponState::EWS_Dropped);

	FDetachmentTransformRules DetachRules(
		EDetachmentRule::KeepWorld,
		true
	);
	WeaponMesh->DetachFromComponent(DetachRules);

	SetOwner(nullptr);
	BrokenEdgeOwnerCharacter = nullptr;
	BrokenEdgeOwnerController = nullptr;
}
```

不要在这个函数内部添加 Server RPC。`AWeapon` 在掉落前虽然由角色拥有，但“玩家想丢枪”属于 CombatComponent 的操作意图；`AWeapon::Dropped()` 只负责执行服务器已经批准的状态切换。

## 10. Drop() 与 DropEquippedWeapon() 的职责

当前项目同时存在：

```text
Drop()
    玩家主动按键丢弃

DropEquippedWeapon()
    EquipPrimaryWeapon() 替换主武器前丢下旧武器
```

推荐保持两者职责分离：

- `Drop()`：可以从拥有者客户端调用，负责本地输入表现和 Server RPC；
- `DropOnServer()`：处理玩家主动丢弃、指针变更和副武器提升；
- `DropEquippedWeapon()`：只作为服务器内部的“替换旧主武器”辅助函数。

不要让 `DropEquippedWeapon()` 自动提升副武器。否则 `EquipPrimaryWeapon()` 在拾取第三把武器时可能先把副武器提升为主武器，随后又立即被新拾取的武器覆盖，造成武器指针或附着关系错误。

## 11. 与淘汰丢枪的关系

[`ABrokenEdgeCharacter::DropWeapons()`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp#L1024) 在淘汰时丢弃主副武器。这个调用来自服务器淘汰流程，因此不需要再走玩家输入 RPC。

后续可以把重复代码整理为 CombatComponent 的服务器辅助函数，例如：

```cpp
void UCombatComponent::DropAllWeaponsOnServer();
```

但它与手动丢弃的行为不同：

| 手动丢弃 | 淘汰丢弃 |
| --- | --- |
| 只丢当前武器 | 丢弃所有武器 |
| 可以提升副武器 | 不应该提升副武器 |
| 角色继续游戏 | 角色进入淘汰状态 |
| 需要拥有者 Server RPC | 已经处于服务器权威流程 |

因此不要直接让淘汰逻辑调用本文的 `Drop()`。

## 12. 网络执行流程

### 12.1 远程客户端主动丢枪

```text
Owning Client
    DropAction Started
        -> ABrokenEdgeCharacter::DropButtonPressed()
        -> UCombatComponent::Drop()
        -> 本地停止持续开火、退出瞄准
        -> ServerDrop()

Server
    ServerDrop_Implementation()
        -> DropOnServer()
        -> 再次校验 CombatState / EquippedWeapon / Flag
        -> EquippedWeapon = nullptr
        -> AWeapon::Dropped()
        -> 可选：SecondaryWeapon 提升为主武器

All Clients
    EquippedWeapon RepNotify
        -> 更新右手挂接或空手表现

    WeaponState RepNotify
        -> OnDropped()
        -> 开启物理、重力和碰撞

    ReplicateMovement
        -> 同步掉落武器的位置和旋转
```

### 12.2 Listen Server 主机主动丢枪

```text
Listen Server Local Character
    Drop()
        -> HasAuthority() == true
        -> 直接调用 DropOnServer()
        -> 手动刷新主机 HUD
```

权威实例不会因为自己修改复制属性而执行自己的 `OnRep_EquippedWeapon()`，所以服务器函数中的本地主机 HUD 分支不能完全依赖 RepNotify。

## 13. 可选：让武器向前抛出

当前掉落只解除附着并开启物理，武器通常会从手部位置自然落下。如果需要“扔出去”的感觉，应只在服务器开启物理后施加冲量：

```cpp
WeaponToDrop->Dropped();

if (USkeletalMeshComponent* WeaponMesh = WeaponToDrop->GetWeaponMesh())
{
	const FVector TossDirection =
		Character->GetActorForwardVector() + FVector(0.f, 0.f, 0.25f);

	WeaponMesh->AddImpulse(
		TossDirection.GetSafeNormal() * 300.f,
		NAME_None,
		true
	);
}
```

冲量大小与武器网格的质量、阻尼、碰撞形状有关，`300.f` 只能作为起始调参值，不是已经验证的项目参数。

## 14. 可选：防止立即捡回

`OnDropped()` 会在服务器立即开启 `AreaSphere`。武器仍靠近角色时，服务器可能立刻把它设置为该角色的 `OverlappingWeapon`，玩家再次按装备键就能捡回。

如果玩法不允许立即捡回，可以增加短暂冷却：

```text
服务器执行 Dropped()
    -> 暂时保持 AreaSphere = NoCollision
    -> 0.3 至 0.8 秒后启用 QueryOnly
```

冷却必须由服务器控制。具体时间应结合武器抛出速度、角色移动速度和实际手感在 PIE 中调试。

## 15. 编辑器配置检查

打开角色 Blueprint，确认：

1. `DropAction` 指向 `/Game/Input/Actions/IA_Drop`；
2. `DefaultMappingContext` 指向 `/Game/Input/IMC_CharacterDefault`；
3. `IMC_CharacterDefault` 给 `IA_Drop` 绑定了目标按键，例如 `G`；
4. `IA_Drop` 是数字按钮类型；
5. C++ 使用 `ETriggerEvent::Started`，一次按键只触发一次丢弃。

如果断点无法进入 `DropButtonPressed()`，应先排查 Enhanced Input 资产和 Mapping Context，而不是排查网络 RPC。

## 16. 验收清单

### 16.1 静态与编译检查

- [ ] `ServerDrop` 带有 `UFUNCTION(Server, Reliable)`；
- [ ] `.cpp` 实现名是 `ServerDrop_Implementation()`；
- [ ] 客户端路径不直接调用 `AWeapon::Dropped()`；
- [ ] 服务器再次检查 `CombatState`、持旗状态和武器有效性；
- [ ] `EquippedWeapon` 和 `SecondaryWeapon` 只在服务器修改；
- [ ] `FireTimerFinished()` 在空武器判断之前恢复 `bCanFire`；
- [ ] `OnRep_EquippedWeapon()` 处理 `nullptr`；
- [ ] `InterpFOV()` 在无武器时恢复默认 FOV；
- [ ] 关闭编辑器后使用 `-NoHotReloadFromIDE` 完整编译；
- [ ] Unreal Header Tool、C++ 编译和最终链接均成功。

### 16.2 Standalone

- [ ] 只有一把武器时，按键后武器落地；
- [ ] 武器恢复物理、重力、碰撞和描边；
- [ ] 准星、弹匣弹药和携带弹药 HUD 被清空；
- [ ] 角色从持枪旋转模式恢复到移动朝向；
- [ ] 瞄准丢枪后狙击镜和 FOV 正常恢复；
- [ ] 开火后立即丢枪，再捡枪仍然可以开火。

### 16.3 Listen Server：主机与远程客户端

- [ ] 主机丢枪，主机和客户端都看到同一把武器落地；
- [ ] 客户端丢枪，服务器和所有客户端都看到正确结果；
- [ ] 客户端不能通过本地修改让服务器武器消失；
- [ ] 有副武器时，所有机器都看到同一把副武器切到右手；
- [ ] 原主武器在所有机器上都解除附着并启用物理；
- [ ] 掉落武器能够被另一名玩家拾取；
- [ ] 换弹、切枪和持旗状态下，服务器正确拒绝普通丢枪。

### 16.4 Dedicated Server

- [ ] 两个客户端之间的武器状态一致；
- [ ] Dedicated Server 不依赖 HUD 或本地控制器；
- [ ] 武器掉落位置由服务器物理和移动复制决定；
- [ ] 客户端断线、淘汰或高延迟时不存在重复武器和幽灵附着。

## 17. 当前验证边界

本文档基于以下源码进行了静态检查：

- `BrokenEdgeCharacter.h/.cpp`；
- `CombatComponent.h/.cpp`；
- `Weapon.h/.cpp`；
- Enhanced Input 资产的文件路径。

截至 2026-09-01：

- 已确认当前客户端直改复制状态是手动丢枪的核心问题；
- 已确认项目具备武器状态复制、持有指针复制和武器移动复制基础；
- 已确认 `AWeapon::Dropped()` 与 `OnDropped()` 已具备掉落表现；
- 尚未把本文推荐代码写入 C++；
- 尚未执行 UHT、编译、链接、PIE、Standalone 或 Dedicated Server 测试；
- 尚未在编辑器中验证 `IA_Drop` 的实际按键映射和角色蓝图属性。

因此本文代码属于针对当前源码结构的实现方案，不应被描述为已经通过运行时验证的完成状态。

## 18. 相关源码索引

- [`BrokenEdgeCharacter.h`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.h)
- [`BrokenEdgeCharacter.cpp`](../Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp)
- [`CombatComponent.h`](../Source/BrokenEdge/Component/CombatComponent.h)
- [`CombatComponent.cpp`](../Source/BrokenEdge/Component/CombatComponent.cpp)
- [`Weapon.h`](../Source/BrokenEdge/Weapon/Weapon.h)
- [`Weapon.cpp`](../Source/BrokenEdge/Weapon/Weapon.cpp)
- [`ProjectileBullet.cpp`](../Source/BrokenEdge/Weapon/ProjectileBullet.cpp)

`ProjectileBullet.cpp` 被列在这里仅用于明确职责边界：它属于子弹命中与伤害链路，不是丢弃武器功能的实现位置。
