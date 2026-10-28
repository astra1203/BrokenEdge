#include "ExtractionAbilities.h"
#include "ExtractionHealth.h"
#include "ExtractionState.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionSmoke.h"
#include "ExtractionSmokeGrenade.h"
#include "ExtractionStimDart.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
// 救援、治疗能力和烟雾使用服务器计时；客户端请求动作，服务器在开始及每次推进时重新验证资格。
// 规则对象通过 Config 从 Game 配置读取，时间单位为秒、距离为厘米；GetDefault 读取类默认规则。

// 启用复制并以 0.05 秒间隔推进能力动作，客户端不自行完成治疗或救援。
UExtractionAbilities::UExtractionAbilities()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .05f;
}

// 有画面的端提前异步载入激素枪模型，首次施放时直接可用，不在动作里同步加载。
void UExtractionAbilities::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	const auto* R = GetDefault<UExtractionRules>();
	StimGunLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		TArray<FSoftObjectPath>{R->StimGunViewMesh, R->StimGunWorldMesh});
}

// 返回组件所属角色，用于生命状态、距离和动作互斥检查。
ABrokenEdgeCharacter* UExtractionAbilities::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

// 动作、目标和救援占用可复制给相关客户端；个人技能冷却仅发给拥有者。
void UExtractionAbilities::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExtractionAbilities, Action);
	DOREPLIFETIME(UExtractionAbilities, ActionEnd);
	DOREPLIFETIME(UExtractionAbilities, Target);
	DOREPLIFETIME(UExtractionAbilities, Rescuer);
	DOREPLIFETIME_CONDITION(UExtractionAbilities, HealReadyAt, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAbilities, SmokeReadyAt, COND_OwnerOnly);
}

// 只有有效且非零的小队编号相同才算友方，未分组的两个角色不会自动成为队友。
bool UExtractionAbilities::Friendly(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B)
{
	const auto* AP = A ? A->GetPlayerState<AExtractionPlayerState>() : nullptr;
	const auto* BP = B ? B->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return AP && BP && AP->ExtractionSquad > 0 && AP->ExtractionSquad == BP->ExtractionSquad;
}

// 检查距离与视线，忽略双方角色相关武器；Range 使用厘米。
bool UExtractionAbilities::Reach(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B, float Range)
{
	if (!IsValid(A) || !IsValid(B) || FVector::DistSquared(A->GetActorLocation(), B->GetActorLocation()) >
		FMath::Square(Range)) return false;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionAbility), false, A);
	Q.AddIgnoredActor(B);
	for (TActorIterator<AWeapon> It(A->GetWorld()); It; ++It)
		if (It->GetOwner() == A || It->GetOwner() == B) Q.AddIgnoredActor(*It);
	FHitResult Hit;
	return !A->GetWorld()->LineTraceSingleByChannel(Hit, A->GetPawnViewLocation(), B->GetActorLocation(),
	                                                ECC_Visibility, Q);
}

// 从 PlayerState 读取倒地状态，不以角色动画或姿势推断。
bool UExtractionAbilities::IsDowned() const
{
	const auto* PS = Character() ? Character()->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return PS && PS->Life == EExtractionLife::Downed;
}

// 仅可行动的非守卫角色、空闲战斗状态且背包关闭时允许启动能力。
bool UExtractionAbilities::CanStart() const
{
	const auto* C = Character();
	return C && !C->bExtractionGuard && C->GetExtractionCombat()->CanAct()
		&& C->GetCombatState() == ECombatState::ECS_Unoccuiped && !C->GetExtractionInteraction()->InventoryOpen;
}

// 寻找前方可触及的最近倒地队友供本地提示；服务器仍独立验证目标。
ABrokenEdgeCharacter* UExtractionAbilities::RescueFocus() const
{
	const auto* C = Character();
	if (!C || !CanStart()) return nullptr;
	ABrokenEdgeCharacter* Best = nullptr;
	double Distance = TNumericLimits<double>::Max();
	for (TActorIterator<ABrokenEdgeCharacter> It(GetWorld()); It; ++It)
		if (*It != C && Friendly(C, *It) && It->GetExtractionAbilities()->IsDowned()
			&& Reach(C, *It, GetDefault<UExtractionRules>()->ReviveRange))
		{
			const auto Delta = It->GetActorLocation() - C->GetActorLocation();
			if (FVector::DotProduct(C->GetControlRotation().Vector().GetSafeNormal2D(), Delta.GetSafeNormal2D()) < .4f)
				continue;
			if (Delta.SizeSquared() < Distance)
			{
				Best = *It;
				Distance = Delta.SizeSquared();
			}
		}
	return Best;
}

// 从视点射线判断准星下是不是队友，只决定"射飞镖还是自用"；飞镖是否命中由服务器弹道决定。
ABrokenEdgeCharacter* UExtractionAbilities::HealFocus() const
{
	auto* C = Character();
	if (!C) return nullptr;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionHealAim), false, C);
	if (C->GetEquippedWeapon()) Q.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	const auto Start = C->GetPawnViewLocation();
	GetWorld()->LineTraceSingleByChannel(
		Hit, Start, Start + C->GetControlRotation().Vector() * GetDefault<UExtractionRules>()->StimDartMaxDistance,
		ECC_Visibility, Q);
	// 只有准星对着队友才射飞镖；对着敌人或空处都按自用处理。
	if (auto* Other = Cast<ABrokenEdgeCharacter>(Hit.GetActor()); Other && Friendly(C, Other)) return Other;
	return C;
}

// 递增本地序号并发送动作请求，序号用于服务器拒绝旧消息。
void UExtractionAbilities::Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other, bool bLowThrow)
{
	ServerBegin(++LocalSequence, Kind, Other, bLowThrow);
}

// 松开救援输入时发送带新序号的取消，避免旧开始请求覆盖较新的取消。
void UExtractionAbilities::ReleaseRescue() { ServerCancel(++LocalSequence); }

// 只接受更新序号，取消当前能力并释放对目标的救援占用。
void UExtractionAbilities::ServerCancel_Implementation(uint32 Sequence)
{
	if (Sequence <= LastSequence) return;
	LastSequence = Sequence;
	Cancel();
}

// 动作复制到客户端时同步表现；服务器侧由 ServerBegin/Cancel 直接调用，不依赖 RepNotify。
void UExtractionAbilities::OnRep_Action()
{
	RefreshAbilityMontage();
}

void UExtractionAbilities::RefreshAbilityMontage()
{
	auto* C = Character();
	if (C == nullptr) return;
	RefreshStimGun();
	if (Action == EExtractionAction::None)
	{
		C->StopAbilityMontage();
		return;
	}
	const auto* R = GetDefault<UExtractionRules>();
	const float Seconds = Action == EExtractionAction::Revive
		                      ? R->ReviveSeconds
		                      : Action == EExtractionAction::Heal
		                      ? R->HealSeconds
		                      : R->SmokeCastSeconds;
	// 激素枪对准队友是射击动作，否则是给自己扎针；Target 与 Action 同批复制。
	const bool bHealOther = Action == EExtractionAction::Heal && IsValid(Target) && Target != C;
	C->PlayAbilityMontage(bHealOther ? ExtractionHealOtherPresentation : static_cast<uint8>(Action), Seconds);
}

USkeletalMeshComponent* UExtractionAbilities::MakeStimGun(USkeletalMeshComponent* Parent, const FSoftObjectPath& Path,
                                                           bool bView)
{
	// 只用 BeginPlay 已异步载入且编译完成的模型；同步加载或等待编译会卡住游戏线程（实测 11 s 收不到包）。
	auto* Mesh = Cast<USkeletalMesh>(Path.ResolveObject());
	if (!Mesh || Mesh->IsCompiling() || !Parent) return nullptr;
	const FName Bone = Parent->DoesSocketExist(GetDefault<UExtractionRules>()->StimGunBone)
		                   ? GetDefault<UExtractionRules>()->StimGunBone
		                   : FName(TEXT("RightHand"));
	auto* Gun = NewObject<USkeletalMeshComponent>(GetOwner());
	Gun->SetupAttachment(Parent, Bone);
	Gun->SetSkeletalMeshAsset(Mesh);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetCanEverAffectNavigation(false);
	if (bView)
	{
		Gun->SetOnlyOwnerSee(true);
		Gun->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		Gun->SetCastShadow(false);
	}
	Gun->RegisterComponent();
	if (bView) Gun->AddTickPrerequisiteComponent(Parent);
	return Gun;
}

void UExtractionAbilities::RefreshStimGun()
{
	auto* C = Character();
	if (!C || GetNetMode() == NM_DedicatedServer) return;
	const auto* R = GetDefault<UExtractionRules>();
	// 救援官方也是端着激素枪（MedicalGun Rescue 动作），烟雾用另一套道具，不显示枪。
	const bool bShow = (Action == EExtractionAction::Heal || Action == EExtractionAction::Revive) && !C->IsDefeated();
	auto* Gun = C->GetEquippedWeapon();
	if (HiddenGun.IsValid() && (HiddenGun != Gun || !bShow)) HiddenGun->SetActorHiddenInGame(false);
	HiddenGun = bShow ? Gun : nullptr;
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(true);
	if (bShow && !WorldStimGun) WorldStimGun = MakeStimGun(C->GetMesh(), R->StimGunWorldMesh, false);
	if (WorldStimGun)
	{
		WorldStimGun->SetVisibility(bShow);
		WorldStimGun->SetOwnerNoSee(C->IsLocallyControlled() && C->IsFirstPersonView());
	}
	auto* Arms = C->GetFirstPersonMesh();
	if (ViewStimGun && ViewStimGun->GetAttachParent() != Arms)
	{
		ViewStimGun->DestroyComponent();
		ViewStimGun = nullptr;
	}
	if (bShow && !ViewStimGun && IsValid(Arms) && C->IsLocallyControlled() && C->IsFirstPersonView())
		ViewStimGun = MakeStimGun(Arms, R->StimGunViewMesh, true);
	if (ViewStimGun) ViewStimGun->SetVisibility(bShow && C->IsFirstPersonView());
}

// 校验序号、动作种类、冷却和目标，再锁定救援者或计算烟雾落点并设定完成时刻。
void UExtractionAbilities::ServerBegin_Implementation(uint32 Sequence, EExtractionAction Kind,
                                                      ABrokenEdgeCharacter* Other, bool bLowThrow)
{
	if (Sequence <= LastSequence) return;
	LastSequence = Sequence;
	if (!CanStart() || Action != EExtractionAction::None) return;
	const auto* R = GetDefault<UExtractionRules>();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Kind != EExtractionAction::Revive && Kind != EExtractionAction::Heal && Kind != EExtractionAction::Smoke)
		return;
	if ((Kind == EExtractionAction::Heal && Now < HealReadyAt) || (Kind == EExtractionAction::Smoke && Now <
		SmokeReadyAt)) return;
	auto* C = Character();
	C->GetExtractionInteraction()->CancelMedical();
	Action = Kind;
	Target = Other;
	if (!ValidTarget())
	{
		Cancel();
		return;
	}
	if (Kind == EExtractionAction::Revive)
	{
		auto* Ability = Target->GetExtractionAbilities();
		if (IsValid(Ability->Rescuer) && Ability->Rescuer != C)
		{
			Cancel();
			return;
		}
		Ability->Rescuer = C;
		Target->ForceNetUpdate();
	}
	LowThrow = Kind == EExtractionAction::Smoke && bLowThrow;
	StartedAt = C->GetActorLocation();
	ActionEnd = Now + (Kind == EExtractionAction::Revive
		                   ? R->ReviveSeconds
		                   : Kind == EExtractionAction::Heal
		                   ? R->HealSeconds
		                   : R->SmokeCastSeconds);
	C->GetCombat()->FireButtonPressed(false);
	C->GetExtractionInteraction()->StopCombatInput();
	// 时长确定后再挂表现，保证蒙太奇与 ActionEnd 对齐。
	RefreshAbilityMontage();
	C->ForceNetUpdate();
}

// 救援必须是可触及的倒地队友；治疗需要存活、缺血的友方；烟雾没有角色目标。
bool UExtractionAbilities::ValidTarget() const
{
	if (Action == EExtractionAction::Smoke) return true;
	const auto* C = Character();
	const auto* R = GetDefault<UExtractionRules>();
	if (!IsValid(Target) || !Friendly(C, Target)) return false;
	const auto* PS = Target->GetPlayerState<AExtractionPlayerState>();
	if (Action == EExtractionAction::Revive)
		return Target != C && PS->Life == EExtractionLife::Downed && Reach(C, Target, R->ReviveRange);
	// 官方激素 Buff 不判血量（满血也有止痛），只排除倒地/阵亡（IncludeImpendingDeath=false）。
	// 射向队友时不再要求距离和视线：飞镖真实飞过去，被挡住或队友躲开就打空。
	return Action == EExtractionAction::Heal && (Target != C || PS->Life == EExtractionLife::Alive);
}

// 服务器释放由自己持有的救援锁，清空动作、目标和截止时间，通知复制。
void UExtractionAbilities::Cancel()
{
	if (!GetOwner()->HasAuthority()) return;
	if (IsValid(Target) && Target->GetExtractionAbilities()->Rescuer == Character())
	{
		Target->GetExtractionAbilities()->Rescuer = nullptr;
		Target->ForceNetUpdate();
	}
	Action = EExtractionAction::None;
	ActionEnd = 0;
	Target = nullptr;
	// 取消、完成、受伤都会经过这里，统一收表现。
	RefreshAbilityMontage();
	GetOwner()->ForceNetUpdate();
}

// 受伤时同时取消自己动作和对自己的救援，避免一方已受伤另一方仍继续计时。
void UExtractionAbilities::Damaged()
{
	if (IsValid(Rescuer)) Rescuer->GetExtractionAbilities()->Cancel();
	Cancel();
}

// 在服务器应用救起、回血或生成烟雾；成功作用后记录相应冷却，最后统一释放动作状态。
void UExtractionAbilities::Complete()
{
	const auto* R = GetDefault<UExtractionRules>();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Action == EExtractionAction::Revive)
	{
		auto* PS = Target->GetPlayerState<AExtractionPlayerState>();
		PS->Life = EExtractionLife::Alive;
		PS->BleedAt = 0;
		PS->DownHealth = 0;
		PS->ForceNetUpdate();
		Target->SetHealth(R->ReviveHealth);
		Target->SetShield(0);
		Target->GetExtractionHealth()->Revived();
		Target->UnCrouch();
		Target->UpdateHUDHealth();
		Target->ForceNetUpdate();
	}
	else if (Action == EExtractionAction::Heal)
	{
		// 一发激素弹，出手即进入冷却。自用直接上效果（官方 UseForMe 由动画通知 AddBuff）；
		// 射向队友则沿当前准星发射飞镖，由飞镖命中时施加效果，打空就浪费这一发。
		if (Target == Character()) Target->GetExtractionHealth()->ApplyStim();
		else
		{
			auto* C = Character();
			const FVector Direction = C->GetControlRotation().Vector();
			FVector From = C->GetPawnViewLocation() + Direction * 30.f;
			// 贴墙出手时不能把飞镖生成在墙另一侧，与枪口穿墙处理一致。
			FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionStimMuzzle), false, C);
			FHitResult Wall;
			if (GetWorld()->LineTraceSingleByChannel(Wall, C->GetPawnViewLocation(), From, ECC_Visibility, Q))
				From = Wall.ImpactPoint - Direction * (R->StimDartRadius + .1f);
			FActorSpawnParameters Params;
			Params.Owner = C;
			Params.Instigator = C;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (auto* Dart = GetWorld()->SpawnActor<AExtractionStimDart>(From, Direction.Rotation(), Params))
				Dart->Launch(C, Direction);
		}
		HealReadyAt = Now + R->HealCooldown;
	}
	else if (Action == EExtractionAction::Smoke)
	{
		// 真实投掷：沿出手瞬间的准星方向（加俯仰修正）按官方初速抛出，落点交给重力与弹跳。
		// 出手点照官方偏移：前 10、右 15（低抛 24）、上 2（低抛 0）厘米，相对视点。
		auto* C = Character();
		const FRotator View = C->GetControlRotation().GetNormalized();
		FRotator Throw = View;
		Throw.Pitch = FMath::Clamp(View.Pitch + R->SmokeThrowPitch, -89.f, 89.f);
		const FRotationMatrix Axes(View);
		const FVector Eye = C->GetPawnViewLocation();
		FVector From = Eye + Axes.GetUnitAxis(EAxis::X) * 10.f
			+ Axes.GetUnitAxis(EAxis::Y) * (LowThrow ? 24.f : 15.f) + Axes.GetUnitAxis(EAxis::Z) * (LowThrow ? 0.f : 2.f);
		// 贴墙出手时不能把弹体生成在墙另一侧。
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionSmokeMuzzle), false, C);
		FHitResult Wall;
		if (GetWorld()->LineTraceSingleByChannel(Wall, Eye, From, ECC_Visibility, Q))
			From = Wall.ImpactPoint + Wall.ImpactNormal * 6.f;
		const FVector Velocity = Throw.Vector() * (LowThrow ? R->SmokeThrowLowSpeed : R->SmokeThrowSpeed);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = C;
		if (auto* Grenade = GetWorld()->SpawnActor<AExtractionSmokeGrenade>(
			From, FRotator::ZeroRotator, Params))
		{
			Grenade->SetSmokeTuning(R->SmokeRadius, R->SmokeSeconds, R->SmokeFuseSeconds);
			Grenade->Launch(From, Velocity);
			SmokeReadyAt = Now + R->SmokeCooldown;
			UE_LOG(LogTemp, Warning, TEXT("[SmokeGrenade] %s throw from %s velocity %s"), LowThrow ? TEXT("low") : TEXT("high"),
				*From.ToCompactString(), *Velocity.ToCompactString());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[SmokeGrenade] spawn failed at %s"), *From.ToCompactString());
		}
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ABILITY Actor=%s Action=%d Completed"), *GetOwner()->GetName(),
	       static_cast<int32>(Action));
	Cancel();
}

// 仅服务器推进：目标失效、移动、下落或资格丢失立即取消，到截止时间才调用 Complete。
bool UExtractionAbilities::CancelledByMovement(EExtractionAction Kind, bool bMoved)
{
	// 没有动作时不存在"取消"；治疗是唯一允许移动施放的动作，其余（含将来新增）默认站定。
	return bMoved && Kind != EExtractionAction::None && Kind != EExtractionAction::Heal;
}

void UExtractionAbilities::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Tick, Function);
	// 表现每端都维护：手臂重建或换枪后重新挂接、重新隐藏。
	if (Action != EExtractionAction::None || HiddenGun.IsValid()) RefreshStimGun();
	if (!GetOwner()->HasAuthority() || Action == EExtractionAction::None) return;
	const auto* C = Character();
	// 治疗允许边走边施放（移速由角色按 HealMoveSpeedScale 降低）；救援与烟雾仍需站定，
	// 否则施法点会跟着角色漂移，救援脱离范围、烟雾落点失去意义。
	const bool bMoved = C->GetVelocity().SizeSquared() > 25
		|| FVector::DistSquared(StartedAt, C->GetActorLocation()) > 4;
	if (!CanStart() || !ValidTarget() || C->GetCharacterMovement()->IsFalling() || CancelledByMovement(Action, bMoved))
	{
		Cancel();
		return;
	}
	if (GetWorld()->GetTimeSeconds() >= ActionEnd) Complete();
}

// 销毁时释放双向救援关系，再调用父类生命周期清理。
void UExtractionAbilities::EndPlay(const EEndPlayReason::Type Reason)
{
	Damaged();
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(false);
	if (WorldStimGun) WorldStimGun->DestroyComponent();
	if (ViewStimGun) ViewStimGun->DestroyComponent();
	Super::EndPlay(Reason);
}
