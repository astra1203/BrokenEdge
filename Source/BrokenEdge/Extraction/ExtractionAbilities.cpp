#include "ExtractionAbilities.h"
#include "ExtractionHealth.h"
#include "ExtractionState.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionSmoke.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
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

// 从视点射线选择治疗目标，没命中其他角色时默认自己；友方和血量条件稍后检查。
ABrokenEdgeCharacter* UExtractionAbilities::HealFocus() const
{
	auto* C = Character();
	if (!C) return nullptr;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionHealAim), false, C);
	if (C->GetEquippedWeapon()) Q.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	const auto Start = C->GetPawnViewLocation();
	GetWorld()->LineTraceSingleByChannel(
		Hit, Start, Start + C->GetControlRotation().Vector() * GetDefault<UExtractionRules>()->HealRange,
		ECC_Visibility, Q);
	if (auto* Other = Cast<ABrokenEdgeCharacter>(Hit.GetActor())) return Other;
	return C;
}

// 递增本地序号并发送动作请求，序号用于服务器拒绝旧消息。
void UExtractionAbilities::Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other)
{
	ServerBegin(++LocalSequence, Kind, Other);
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

// 校验序号、动作种类、冷却和目标，再锁定救援者或计算烟雾落点并设定完成时刻。
void UExtractionAbilities::ServerBegin_Implementation(uint32 Sequence, EExtractionAction Kind,
                                                      ABrokenEdgeCharacter* Other)
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
	if (Kind == EExtractionAction::Smoke)
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionSmokeAim), false, C);
		if (C->GetEquippedWeapon()) Q.AddIgnoredActor(C->GetEquippedWeapon());
		const auto Eye = C->GetPawnViewLocation();
		SmokePoint = Eye + C->GetControlRotation().Vector() * R->SmokeRange;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, SmokePoint, ECC_Visibility, Q)) SmokePoint = Hit.ImpactPoint
			+ Hit.ImpactNormal * 10;
	}
	StartedAt = C->GetActorLocation();
	ActionEnd = Now + (Kind == EExtractionAction::Revive
		                   ? R->ReviveSeconds
		                   : Kind == EExtractionAction::Heal
		                   ? R->HealSeconds
		                   : R->SmokeCastSeconds);
	C->GetCombat()->FireButtonPressed(false);
	C->GetExtractionInteraction()->StopCombatInput();
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
	return Action == EExtractionAction::Heal && PS->Life == EExtractionLife::Alive
		&& Target->GetHealth() < Target->GetMaxHealth() && (Target == C || Reach(C, Target, R->HealRange));
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
		Target->SetHealth(Target->GetHealth() + R->HealAmount);
		Target->UpdateHUDHealth();
		Target->ForceNetUpdate();
		HealReadyAt = Now + R->HealCooldown;
	}
	else if (Action == EExtractionAction::Smoke)
	{
		auto* Smoke = GetWorld()->SpawnActor<AExtractionSmoke>(SmokePoint, FRotator::ZeroRotator);
		if (Smoke) SmokeReadyAt = Now + R->SmokeCooldown;
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ABILITY Actor=%s Action=%d Completed"), *GetOwner()->GetName(),
	       static_cast<int32>(Action));
	Cancel();
}

// 仅服务器推进：目标失效、移动、下落或资格丢失立即取消，到截止时间才调用 Complete。
void UExtractionAbilities::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Tick, Function);
	if (!GetOwner()->HasAuthority() || Action == EExtractionAction::None) return;
	const auto* C = Character();
	if (!CanStart() || !ValidTarget() || C->GetCharacterMovement()->IsFalling()
		|| C->GetVelocity().SizeSquared() > 25 || FVector::DistSquared(StartedAt, C->GetActorLocation()) > 4)
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
	Super::EndPlay(Reason);
}
