#include "ExtractionCombatComponent.h"
#include "ExtractionMeleeComponent.h"
#include "ExtractionState.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionPlayerController.h"
#include "ExtractionGuard.h"
#include "ExtractionGuardAim.h"
#include "ExtractionAbilities.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/HitScanWeapon.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
// 撤离模式专用战斗协议：拥有者预测表现，服务器校验射击与装填并维护弹药，其他客户端只播放效果。
// Server/Client/NetMulticast RPC 分别对应请求、拥有者回执和远端表现；Reliable 不等于免除服务器参数校验。
// 权威装填和切枪使用定时器，不依赖专服是否渲染动画或触发 AnimNotify。

static TAutoConsoleVariable<int32> CVarWeaponStats(
	TEXT("BrokenEdge.Weapon.DebugStats"), 0, TEXT("Show ADS, breath, spread and source revision."));

// 随机种子只同步给拥有者，使本地预测与服务器用相同射击 ID 构造散布。
void UExtractionCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionCombatComponent, MechanicsSeed, COND_OwnerOnly);
}

// 武器、附件或配置版本变化时重新解析参数并重置短期枪械状态；切枪不能补满玩家屏息资源。
void UExtractionCombatComponent::RefreshMechanics()
{
	auto* C = Character();
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (W == MechanicsWeapon.Get() && (Stats.IsValid() || !W) && (!W || (MechanicsParts == W->ExtractionParts &&
		Configuration == W->ExtractionConfiguration)))
		return;
	MechanicsParts = W ? W->ExtractionParts : TArray<FName>();
	Configuration = W ? W->ExtractionConfiguration : 0;
	MechanicsWeapon = W;
	CancelFireInput();
	Stats = {};
	ResetTransientMechanics();
	if (W)
	{
		FExtractionItem Item;
		Item.Definition = W->ExtractionDefinition;
		for (FName Part : W->ExtractionParts)
		{
			FExtractionAttachment A;
			A.Definition = Part;
			Item.Attachments.Add(A);
		}
		if (!W->ExtractionAssembly.IsEmpty()) Item.Attachments = W->ExtractionAssembly;
		Stats = WeaponMechanics::ResolveWeaponStats(Item);
		if (Stats.IsValid())
		{
			W->FireDelay = Stats.Interval;
			ReadyAt = GetWorld()->GetTimeSeconds() + Stats.Equip;
		}
	}
	if (C && C->HasAuthority() && !MechanicsSeed) MechanicsSeed = FMath::RandHelper(MAX_int32 - 1) + 1;
}

// 有有效参数时检查冲刺和举枪/跑射延迟；无参数时允许既有逻辑继续判断。
bool UExtractionCombatComponent::CanFireMechanics() const
{
	const auto* C = Character();
	return !Stats.IsValid() || (C && !C->IsSprinting() && GetWorld()->GetTimeSeconds() >= ReadyAt
		&& SprintFire.CanFire(GetWorld()->GetTimeSeconds()));
}

bool UExtractionCombatComponent::CanSprintWithWeapon() const
{
	return SprintFire.CanSprint(GetWorld()->GetTimeSeconds());
}

void UExtractionCombatComponent::OnSprintEnded()
{
	SprintFire.EndSprint(GetWorld()->GetTimeSeconds(), Stats.SprintToFire);
}

bool UExtractionCombatComponent::ApplyFireInput(bool bHeld)
{
	RefreshMechanics();
	auto* C = Character();
	bHeld = bHeld && CanAct() && C && C->GetEquippedWeapon() && !C->Melee->Active
		&& !C->GetExtractionInteraction()->InventoryOpen && !C->GetCombat()->bLocallyReloading
		&& C->GetCombatState() == ECombatState::ECS_Unoccuiped;
	SprintFire.SetFireHeld(bHeld, GetWorld()->GetTimeSeconds(), Stats.ReturnSprintInterval);
	if (C) C->UpdateSprint();
	return bHeld;
}

bool UExtractionCombatComponent::SetFireInputHeld(bool bHeld)
{
	const bool Accepted = ApplyFireInput(bHeld);
	if (Character() && Character()->IsLocallyControlled() && !Character()->HasAuthority())
		ServerSetFireInputHeld(Accepted);
	return Accepted;
}

void UExtractionCombatComponent::ServerSetFireInputHeld_Implementation(bool bHeld)
{
	if (IsEnabled()) ApplyFireInput(bHeld);
}

void UExtractionCombatComponent::CancelFireInput()
{
	SprintFire.SetFireHeld(false, GetWorld()->GetTimeSeconds(), Stats.ReturnSprintInterval);
	if (auto* C = Character(); C && C->GetCombat()) C->GetCombat()->bFireButtonPressed = false;
}

// 清除开镜、连射、散布与镜头冲量，不重置屏息余量及冷却，避免切枪刷资源。
void UExtractionCombatComponent::ResetTransientMechanics()
{
	AimAlpha = 0;
	BurstIndex = 0;
	SpreadAccumulation = FVector2D::ZeroVector;
	LastMechanicsShot = -100;
	VisualKick = 0;
	RecoilDebt = FVector2D::ZeroVector;
	bHaveControl = false;
	bBreathHeld = false;
	// Breath and cooldown belong to the player; changing weapons must not refill them.
	if (auto* C = Character(); C && C->IsLocallyControlled() && C->GetFollowCamera())
		C->GetFollowCamera()->ClearAdditiveOffset();
}

// 只有能行动、非冲刺持刀、未开背包且未装填时，才允许稳定瞄准相关状态。
bool UExtractionCombatComponent::CanSteadyWeapon() const
{
	const auto* C = Character();
	return CanAct() && C && !C->IsSprinting() && !C->Melee->Active
		&& !C->GetExtractionInteraction()->InventoryOpen
		&& !C->GetCombat()->bLocallyReloading
		&& C->GetCombat()->CombatState == ECombatState::ECS_Unoccuiped;
}

// 本地检查开镜资格并发出屏息请求；服务器仍会重新校验当前状态。
void UExtractionCombatComponent::HoldBreathPressed()
{
	bBreathHeld = CanSteadyWeapon() && Character()->IsAiming();
	ServerHoldBreath(bBreathHeld);
}

// 本地和服务器同时解除屏息输入，资源恢复仍按 Tick 规则执行。
void UExtractionCombatComponent::HoldBreathReleased()
{
	bBreathHeld = false;
	ServerHoldBreath(false);
}

// 服务器只接受当前可稳定瞄准且已开镜的屏息请求。
void UExtractionCombatComponent::ServerHoldBreath_Implementation(bool bHeld)
{
	bBreathHeld = bHeld && CanSteadyWeapon() && Character()->IsAiming();
}

// 推进开镜插值、屏息消耗恢复、散布回落和后座偿还；本地额外更新镜头表现及自动连射。
void UExtractionCombatComponent::TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Kind, Tick);
	if (!IsEnabled()) return;
	RefreshMechanics();
	auto* C = Character();
	if (C && C->IsLocallyControlled() && C->GetFollowCamera()) C->GetFollowCamera()->ClearAdditiveOffset();
	if (!C || !Stats.IsValid()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Steady = CanSteadyWeapon();
	if (!Steady) ResetTransientMechanics();
	const bool Aiming = Steady && C->IsAiming();
	if (!Aiming) bBreathHeld = false;
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, Aiming ? 1.f : 0.f, Delta,
	                                    1.f / FMath::Max(.01f, Aiming ? Stats.AimIn : Stats.AimOut));
	// 跑射时间只在退出冲刺时记录；逐帧推迟会让按住 Shift 的射击永远无法就绪。
	if (!CanAct() || C->Melee->Active || C->GetExtractionInteraction()->InventoryOpen
		|| C->GetCombatState() != ECombatState::ECS_Unoccuiped || C->GetCombat()->bLocallyReloading)
		CancelFireInput();
	const bool Hold = CanAct() && bBreathHeld && AimAlpha > .99f && Breath > 0 && Now >= BreathResume;
	Breath = FMath::Clamp(Breath + Delta * (Hold ? -Stats.BreathDrain : Stats.BreathRecovery), 0.f, 100.f);
	if (Hold && Breath <= 0) BreathResume = Now + Stats.BreathCooldown;
	const FVector2D Recover = FMath::Lerp(Stats.Spread[0].Recovery, Stats.Spread[1].Recovery, AimAlpha) * Delta;
	SpreadAccumulation.X = FMath::Max(0., SpreadAccumulation.X - Recover.X);
	SpreadAccumulation.Y = FMath::Max(0., SpreadAccumulation.Y - Recover.Y);
	VisualKick *= FMath::Exp(-Stats.KickRecovery * Delta);
	if (C->IsLocallyControlled() && C->GetFollowCamera() && CanAct())
	{
		// Compatibility camera impulse, deliberately excluded from the authoritative aim ray.
		const float CameraPitch = VisualKick * Stats.CameraKickScale * FMath::Sin(
			(Now - LastMechanicsShot) * Stats.CameraFrequency);
		C->GetFollowCamera()->AddAdditiveOffset(FTransform(FRotator(CameraPitch, 0, 0)), 0);
	}
	if (!C->bExtractionGuard && C->IsLocallyControlled() && C->Controller)
	{
		FRotator Control = C->Controller->GetControlRotation();
		if (bHaveControl)
		{
			const auto Input = (Control - PreviousControl).GetNormalized();
			RecoilDebt.X = WeaponMechanics::CompensatedDebt(RecoilDebt.X, Input.Pitch);
			RecoilDebt.Y = WeaponMechanics::CompensatedDebt(RecoilDebt.Y, Input.Yaw);
		}
		if (Now - LastMechanicsShot > Stats.Interval * Stats.BurstGapIntervals)
		{
			const FVector2D RecoverDebt = RecoilDebt * (1.f - FMath::Exp(-Stats.RecoilRecovery * Delta));
			Control.Pitch -= RecoverDebt.X;
			Control.Yaw -= RecoverDebt.Y;
			RecoilDebt -= RecoverDebt;
			C->Controller->SetControlRotation(Control);
		}
		PreviousControl = Control;
		bHaveControl = true;
	}
	if (C->IsLocallyControlled() && CanFireMechanics() && C->GetCombat()->bFireButtonPressed
		&& C->GetCombat()->bCanFire && MechanicsWeapon.IsValid()
		&& (MechanicsWeapon->bAutomatic || SprintFire.PendingShot)
		&& !C->GetExtractionInteraction()->InventoryOpen)
		C->GetCombat()->Fire();
	if (CVarWeaponStats.GetValueOnGameThread() && C->IsLocallyControlled())
		DrawDebugString(GetWorld(), C->GetActorLocation() + FVector(0, 0, 100), FString::Printf(
			                TEXT("%s rev=%u ADS=%.2f Breath=%.1f Spread=%s RPM=%.1f [compatibility]"),
			                *Stats.Definition.ToString(), Stats.Revision, AimAlpha, Breath,
			                *SpreadAccumulation.ToString(), 60 / Stats.Interval), nullptr, FColor::Green, 0);
}

// 以共享种子和射击 ID 计算散布，叠加姿态与屏息影响，再推进连射、后座和资源状态。
// bApplyInput 只控制是否给本地控制旋转施加后座，镜头附加冲量与权威瞄准射线分开。
FVector UExtractionCombatComponent::ResolveShot(const FVector& Target, uint32 Id, bool bApplyInput)
{
	auto* C = Character();
	auto* W = MechanicsWeapon.Get();
	if (!C || !W || !Stats.IsValid()) return Target;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Continuous = Now - LastMechanicsShot < FMath::Max(.2f, Stats.Interval * Stats.BurstGapIntervals);
	if (!Continuous) BurstIndex = 0;
	const FVector Start = W->GetWeaponMesh()->GetSocketLocation(TEXT("MuzzleFlash"));
	FVector2D Spread = FMath::Lerp(Stats.Spread[0].Base, Stats.Spread[1].Base, AimAlpha) + SpreadAccumulation;
	Spread *= Stats.SpreadScale;
	if (C->GetVelocity().SizeSquared2D() > 100) Spread *= Stats.MoveSpread;
	if (C->bIsCrouched) Spread *= Stats.CrouchSpread;
	if (C->GetCharacterMovement()->IsFalling()) Spread *= Stats.JumpSpread;
	const uint32 Seed = HashCombine(MechanicsSeed, Id);
	const bool Hold = CanSteadyWeapon() && bBreathHeld && AimAlpha > .99f && Breath > 0 && Now >= BreathResume;
	// Continuous deterministic sway, separate from random spread. Frequency/amplitude are compatibility tuning.
	// 普通测试地图可能没有 GameState，仍可使用本地服务器世界时间计算武器摆动。
	const auto* GameState = GetWorld()->GetGameState();
	const float Phase = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Sway = AimAlpha * (C->bIsCrouched ? Stats.CrouchBreath : Stats.StandBreath) * (
		Hold ? Stats.HeldSwayScale : 1.f) * Stats.SwayDegrees;
	FRotator Aim = (Target - Start).Rotation();
	Aim.Pitch += FMath::Sin(Phase * Stats.SwayPitchFrequency) * Sway;
	Aim.Yaw += FMath::Sin(Phase * Stats.SwayYawFrequency) * Sway;
	const FVector End = Start + WeaponMechanics::SpreadDirection(Aim.Vector(), Spread, Seed) * Stats.MaxDistance;
	auto Recoil = Stats.Recoil(BurstIndex++, Continuous, AimAlpha, Seed) * (Hold ? Stats.HeldRecoilScale : 1.f);
	if (C->bIsCrouched) Recoil *= Stats.CrouchRecoil;
	// AI 的 ControlRotation 属于 Focus/导航；后坐力仍影响散布，但不能当作玩家输入反写 AI 朝向。
	if (bApplyInput && !C->bExtractionGuard && C->IsLocallyControlled() && C->Controller)
	{
		FRotator Control = C->Controller->GetControlRotation();
		Control.Pitch += Recoil.X;
		Control.Yaw += Recoil.Y;
		C->Controller->SetControlRotation(Control);
		RecoilDebt += Recoil;
		PreviousControl = Control;
		bHaveControl = true;
	}
	VisualKick += Recoil.X * Stats.KickPerShot;
	SpreadAccumulation += FMath::Lerp(Stats.Spread[0].PerShot, Stats.Spread[1].PerShot, AimAlpha);
	const auto Max = FMath::Lerp(Stats.Spread[0].Maximum, Stats.Spread[1].Maximum, AimAlpha);
	SpreadAccumulation.X = FMath::Min(SpreadAccumulation.X, Max.X);
	SpreadAccumulation.Y = FMath::Min(SpreadAccumulation.Y, Max.Y);
	Breath = FMath::Max(0.f, Breath - Stats.BreathShot);
	BreathResume = Now + Stats.BreathCooldown;
	LastMechanicsShot = Now;
	return End;
}

// 启用复制与逐帧更新，服务器规则和本地预测共用同一个组件类型。
UExtractionCombatComponent::UExtractionCombatComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
}

// 组件所属 Actor 应为 BrokenEdgeCharacter，转换失败时返回空指针。
ABrokenEdgeCharacter* UExtractionCombatComponent::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

// 通过当前 GameState 判断是否启用撤离协议，其他模式保持原战斗入口。
bool UExtractionCombatComponent::IsEnabled() const
{
	const auto* C = Character();
	return GetWorld() && (GetWorld()->GetGameState<AExtractionGameState>() || (C && C->bExtractionGuard));
}

// 要求活动对局、存活或守卫身份、未失能且未攀爬；这是多种战斗交互共用的前置条件。
bool UExtractionCombatComponent::CanAct() const
{
	const auto* C = Character();
	const auto* GS = GetWorld() ? GetWorld()->GetGameState<AExtractionGameState>() : nullptr;
	const auto* PS = C ? C->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!C || C->IsDefeated() || C->bDisableGameplay || C->IsTraversing()) return false;
	if (GS)
		return GS->Phase == EExtractionPhase::Active
			&& (C->bExtractionGuard || (PS && PS->Life == EExtractionLife::Alive));
	// 独立测试地图没有搜打撤对局阶段；仅给服务器实际接管的守卫放行，不改变普通玩家权限。
	return C->bExtractionGuard && C->HasAuthority() && IsValid(Cast<AExtractionGuard>(C->GetController()));
}

// 服务器 AI 走同一射击请求校验链路，不能绕过弹药和射速规则。
void UExtractionCombatComponent::FireGuard(const FVector& Target)
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->bExtractionGuard) return;
	ServerRequestShot(++LocalShotId, C->GetEquippedWeapon(), Target);
}

// 本地验证基础条件，非权威拥有者先预测开火并登记待确认射击，再向服务器发送目标。
void UExtractionCombatComponent::RequestShot(const FVector& Target)
{
	RefreshMechanics();
	auto* C = Character();
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !CanFireMechanics() || C->Melee->Active || !Cast<AHitScanWeapon>(W)) return;
	if (C->GetExtractionInteraction()->InventoryOpen) return;
	const uint32 Id = ++LocalShotId;
	SprintFire.PendingShot = false;
	if (!C->HasAuthority())
	{
		PendingShots.Add(Id, W);
		C->GetCombat()->LocalFire(ResolveShot(Target, Id, true));
	}
	ServerRequestShot(Id, W, Target);
}

// 补齐当前武器配置版本并转发到版本化 RPC；旧调用入口仍接受同一套服务器检查。
void UExtractionCombatComponent::ServerRequestShot(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target)
{
	const auto* Profile = Weapon ? WeaponMechanics::Find(Weapon->ExtractionDefinition) : nullptr;
	ServerRequestShotVersioned(ShotId, Weapon, Target,
	                           Weapon && Weapon->ExtractionConfiguration
		                           ? Weapon->ExtractionConfiguration
		                           : Profile
		                           ? Profile->Revision
		                           : 0);
}

// 按序号去重，核验武器归属、装配版本、瞄准方向、射速、弹药和动作互斥，再执行服务端命中。
// 无论接受或拒绝都给拥有者回执，但绝不回传伪造请求指定的他人物品状态。
void UExtractionCombatComponent::ServerRequestShotVersioned_Implementation(uint32 ShotId, AWeapon* Weapon,
                                                                           FVector_NetQuantize Target, uint32 Revision)
{
	if (!IsEnabled()) return;
	RefreshMechanics();
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	// 先推进已处理水位，再核验请求；被拒绝的序号也不能重复提交获利。
	const bool Fresh = ShotId > LastProcessedShot;
	if (Fresh) LastProcessedShot = ShotId;
	const bool Valid = IsEnabled() && Fresh && CanAct() && CanFireMechanics() && Combat && IsValid(Weapon)
		&& !C->Melee->Active
		&& !C->GetExtractionInteraction()->InventoryOpen
		&& Weapon == C->GetEquippedWeapon() && Weapon->GetOwner() == C && Cast<AHitScanWeapon>(Weapon)
		&& (!Stats.IsValid() || (Revision == Stats.Revision && (C->bExtractionGuard ||
			FVector::DotProduct((FVector(Target) - C->GetPawnViewLocation()).GetSafeNormal(),
			                    C->GetBaseAimRotation().Vector()) > .7f)))
		&& Weapon->FireType == EFireType::EFT_HitScan && !Weapon->IsEmpty()
		&& Combat->CombatState == ECombatState::ECS_Unoccuiped && !ReloadTimer.IsValid()
		&& FMath::IsFinite(Weapon->FireDelay) && Weapon->FireDelay > 0
		&& Now + .005 >= NextShotTime && !Target.ContainsNaN()
		&& FVector::DistSquared(Target, C->GetActorLocation()) <= FMath::Square(120000.0);
	if (Valid)
	{
		C->GetExtractionInteraction()->CancelMedical();
		NextShotTime = FMath::Max(Now, NextShotTime) + Weapon->FireDelay;
		Weapon->UseImmediateServerHitScan();
		float SavedScatter = Weapon->SphereRadius;
		if (const auto* Inventory = C->GetExtractionInteraction()->Inventory())
			if (const auto* Item = Inventory->Contents.Find(Weapon->ExtractionItemId))
				Weapon->SphereRadius *= ExtractionItems::ScatterScale(*Item);
		FVector End = Stats.IsValid()
			                    ? ResolveShot(Target, ShotId, true)
			                    : Weapon->bUseScatter
			                    ? Weapon->TraceEndWithScatter(Target)
			                    : FVector(Target);
		Weapon->SphereRadius = SavedScatter;
		if (C->bExtractionGuard)
		{
			// 在武器/附件原有散布之后再加 AI 专属误差，不修改武器资产，也不影响拾取这把枪的玩家。
			// 使用独立的种子混合，避免与武器自身的随机采样使用同一序列。
			const uint32 GuardSeed = HashCombine(HashCombine(MechanicsSeed, ShotId), 0x47554152u);
			End = ExtractionGuardAim::ApplySpread(Weapon->GetWeaponMesh()->GetSocketLocation(TEXT("MuzzleFlash")),
				End, GetDefault<UExtractionRules>()->GuardExtraSpreadDegrees, GuardSeed);
		}
		C->PlayFireMontage(Combat->bAiming);
		Weapon->Fire(End);
		if (auto* Inventory = C->GetExtractionInteraction()->Inventory()) Inventory->RecordShot(Weapon);
		++AcceptedShots;
#if WITH_DEV_AUTOMATION_TESTS
		TestShotTimes.Emplace(AcceptedShots, Now);
		if (TestShotTimes.Num() > 64)
			TestShotTimes.RemoveAt(0, TestShotTimes.Num() - 64, EAllowShrinking::No);
#endif
		// 广播已经追加散射的最终端点；客户端不得再次随机采样，以免命中与表现不一致。
		MulticastShotEffects(Weapon, End, Weapon->GetAmmo());
		// 玩家枪声会引来附近守卫；装了消音/消声类枪口配件时听觉半径缩小。
		if (!C->bExtractionGuard)
		{
			bool bSuppressed = false;
			if (const auto* Inventory = C->GetExtractionInteraction()->Inventory())
				if (const auto* Item = Inventory->Contents.Find(Weapon->ExtractionItemId))
					for (const auto& Part : Item->Attachments)
						if (const auto* D = UExtractionItemDefinition::Get(Part.Definition))
							bSuppressed |= D->Label.ToString().Contains(TEXT("消音")) || D->Label.ToString().Contains(TEXT("消声"));
			const auto* Rules = GetDefault<UExtractionRules>();
			AExtractionGuard::HearShot(GetWorld(), C->GetActorLocation(),
			                           bSuppressed ? Rules->GuardHearingSuppressedRadius : Rules->GuardHearingRadius);
		}
	}
	else ++RejectedShots;
	// Never expose or overwrite an unrelated player's weapon from a forged request.
	AWeapon* OwnedWeapon = IsValid(Weapon) && Weapon->GetOwner() == C ? Weapon : (C ? C->GetEquippedWeapon() : nullptr);
	if (!C || !C->bExtractionGuard)
		ClientShotResult(LastProcessedShot, OwnedWeapon,
		                 OwnedWeapon ? OwnedWeapon->GetAmmo() : 0, Valid);
	if (C && !C->bExtractionGuard)
		ClientMechanicsResult(LastProcessedShot, OwnedWeapon, BurstIndex,
		                      SpreadAccumulation, Breath, Now, LastMechanicsShot);
}

// 拥有者收到服务器枪械状态后叠加尚未确认的本地射击，避免校正抹掉后续预测。
void UExtractionCombatComponent::ClientMechanicsResult_Implementation(uint32 ShotId, AWeapon* Weapon, uint32 Sequence,
                                                                      FVector2D Spread, float InBreath,
                                                                      double ServerTime, double ShotTime)
{
	if (GetOwner()->HasAuthority() || Weapon != MechanicsWeapon.Get() || ShotId < LastAcknowledgedShot) return;
	uint32 Pending = 0;
	for (const auto& Entry : PendingShots) if (Entry.Value.Get() == Weapon) ++Pending;
	BurstIndex = Sequence + Pending;
	SpreadAccumulation = Spread + FMath::Lerp(Stats.Spread[0].PerShot, Stats.Spread[1].PerShot, AimAlpha) * Pending;
	Breath = FMath::Clamp(InBreath - Pending * Stats.BreathShot, 0.f, 100.f);
	if (!Pending) LastMechanicsShot = GetWorld()->GetTimeSeconds() - FMath::Max(0., ServerTime - ShotTime);
}

// 清除已确认序号及更早请求，用服务器弹药减去剩余预测发数恢复本地显示。
void UExtractionCombatComponent::ClientShotResult_Implementation(uint32 ShotId, AWeapon* Weapon, int32 Ammo,
                                                                 bool bAccepted)
{
	if (GetOwner()->HasAuthority() || ShotId < LastAcknowledgedShot) return;
	LastAcknowledgedShot = ShotId;
	for (auto It = PendingShots.CreateIterator(); It; ++It)
		if (It.Key() <= ShotId) It.RemoveCurrent();
	int32 Predicted = 0;
	for (const auto& Entry : PendingShots) if (Entry.Value.Get() == Weapon) ++Predicted;
	if (IsValid(Weapon)) Weapon->SetExtractionAmmo(Ammo - Predicted);
}

// 仅远端非拥有者播放射击；服务器已执行命中、拥有者已预测，二者不能再重复播放或扣弹。
void UExtractionCombatComponent::MulticastShotEffects_Implementation(AWeapon* Weapon, FVector_NetQuantize Target,
                                                                     int32 Ammo)
{
	auto* C = Character();
	if (!C || C->HasAuthority() || !IsValid(Weapon) || C->IsLocallyControlled()) return;
	C->PlayFireMontage(C->IsAiming());
	Weapon->Fire(Target); // Clients produce effects only; rewind requests are disabled for this mode.
	Weapon->SetExtractionAmmo(Ammo);
}

// 旧丢枪入口也转入背包事务，防止仅销毁 Actor 而保留可再次使用的物品。
void UExtractionCombatComponent::ServerDropWeapon_Implementation()
{
	if (Character() && Character()->Melee->Active) return;
	// Legacy endpoint must still enter the inventory transaction path.
	if (CanAct())
		if (auto* PC = Cast<AExtractionPlayerController>(Character()->GetController())) PC->DropEquippedItem();
}

// 拥有者先播放装填表现并请求服务器；此时不真正从背包转移弹药。
void UExtractionCombatComponent::RequestReload()
{
	if (Character() && Character()->Melee->Active) return;
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !W || W->IsFull() || Combat->CarriedAmmo <= 0 || Combat->bLocallyReloading
		|| C->GetExtractionInteraction()->InventoryOpen
		|| Combat->CombatState != ECombatState::ECS_Unoccuiped)
		return;
	Combat->bLocallyReloading = true;
	CancelFireInput();
	Combat->HandleReload();
	Combat->ServerReload();
}

// 服务器重验武器归属和动作条件，记录目标武器并按空仓/普通装填时长安排完成回调。
void UExtractionCombatComponent::BeginServerReload()
{
	if (Character() && Character()->Melee->Active) return;
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !W || W->GetOwner() != C || !Cast<AHitScanWeapon>(W) || W->IsFull()
		|| C->GetExtractionInteraction()->InventoryOpen
		|| Combat->CarriedAmmo <= 0 || Combat->CombatState != ECombatState::ECS_Unoccuiped || ReloadTimer.IsValid())
	{
		ClientReloadRejected();
		return;
	}
	ReloadWeapon = W;
	CancelFireInput();
	C->GetExtractionInteraction()->CancelMedical();
	Combat->CombatState = ECombatState::ECS_Reloading;
	if (!C->IsLocallyControlled()) Combat->HandleReload();
	float Duration = 2.f;
	if (const UAnimMontage* Montage = C->GetReloadMontage())
	{
		const int32 RifleSection = Montage->GetSectionIndex(TEXT("Rifle"));
		// The shared montage also contains shotgun sections which are not played by the AK.
		Duration = RifleSection != INDEX_NONE ? Montage->GetSectionLength(RifleSection) : Montage->GetPlayLength();
	}
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishServerReload,
	                                       FMath::Max(.25f, Stats.IsValid()
		                                                        ? (W->IsEmpty() ? Stats.EmptyReload : Stats.Reload)
		                                                        : Duration));
}

// 到时再次确认仍持有原武器且处于装填状态，再把背包子弹转入弹匣并同步回执。
void UExtractionCombatComponent::FinishServerReload()
{
	ReloadTimer.Invalidate();
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	AWeapon* W = ReloadWeapon.Get();
	if (CanAct() && W && W == C->GetEquippedWeapon() && W->GetOwner() == C && Combat->CombatState ==
		ECombatState::ECS_Reloading)
	{
		if (auto* Inventory = C->GetExtractionInteraction()->Inventory()) Inventory->Reload(W);
		// Keep reload and shot snapshots on this actor's reliable channel so an old
		// shot cannot arrive after a newer refill and restore a stale ammo count.
		MulticastReloadAmmo(W, W->GetAmmo());
		ClientShotResult(LastProcessedShot, W, W->GetAmmo(), true);
		Combat->UpdateCarriedAmmo();
	}
	CancelReload();
}

// 在客户端应用服务器弹药快照，与射击回执保持同组件可靠通道的顺序。
void UExtractionCombatComponent::MulticastReloadAmmo_Implementation(AWeapon* Weapon, int32 Ammo)
{
	if (!GetOwner()->HasAuthority() && IsValid(Weapon)) Weapon->SetExtractionAmmo(Ammo);
}

// 服务器装备同步时设置弹药，并通过同一回执通道更新客户端，防止旧预测覆盖新装备状态。
void UExtractionCombatComponent::EquipmentAmmo(AWeapon* Weapon, int32 Ammo)
{
	if (!GetOwner()->HasAuthority() || !Weapon) return;
	Weapon->SetExtractionAmmo(Ammo);
	Weapon->ExtractionInitialAmmo = Ammo;
	MulticastReloadAmmo(Weapon, Ammo);
	ClientShotResult(LastProcessedShot, Weapon, Ammo, true);
}

// 清除定时器与弱引用，退出装填状态并撤销拥有者预测标记；取消本身不补弹。
void UExtractionCombatComponent::CancelReload()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	ReloadWeapon.Reset();
	if (auto* C = Character())
	{
		UCombatComponent* Combat = C->GetCombat();
		if (Combat->CombatState == ECombatState::ECS_Reloading) Combat->CombatState = ECombatState::ECS_Unoccuiped;
		Combat->bLocallyReloading = false;
		if (C->HasAuthority()) ClientReloadRejected();
	}
}

// 撤销本地装填预测标记；真实弹药以服务器快照为准。
void UExtractionCombatComponent::ClientReloadRejected_Implementation()
{
	if (auto* C = Character()) C->GetCombat()->bLocallyReloading = false;
}

// 记录原主副武器，用参数或通知时刻确定挂接和结束时间；广播蒙太奇后由服务器定时器推进。
void UExtractionCombatComponent::BeginServerSwap()
{
	if (Character() && Character()->Melee->Active) return;
	auto* C = Character();
	if (!C || !C->HasAuthority() || !CanAct() || C->GetExtractionInteraction()->InventoryOpen) return;
	auto* Combat = C->GetCombat().Get();
	if (Combat->CombatState != ECombatState::ECS_Unoccuiped || !Combat->ShouldSwapWeapons()) return;
	CancelSwap();
	CancelFireInput();
	C->GetExtractionInteraction()->CancelMedical();
	SwapPrimary = Combat->EquippedWeapon;
	SwapSecondary = Combat->SecondaryWeapon;
	float AttachTime = .75f, FinishTime = 1.35f;
	if (const auto* Montage = C->GetSwapMontage())
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			const FString Name = Notify.GetNotifyEventName().ToString();
			if (Name.Contains(TEXT("AttachWeapons"))) AttachTime = Notify.GetTriggerTime();
			if (Name.Contains(TEXT("SwapFinished"))) FinishTime = Notify.GetTriggerTime();
		}
	RefreshMechanics();
	if (Stats.IsValid())
		if (const auto* Next = WeaponMechanics::Find(Combat->SecondaryWeapon->ExtractionDefinition))
		{
			AttachTime = Stats.Unequip;
			FinishTime = AttachTime + Next->Equip;
		}
	AttachTime = FMath::Max(.05f, AttachTime);
	FinishTime = FMath::Max(AttachTime + .05f, FinishTime);
	Combat->CombatState = ECombatState::ECS_SwappingWeapon;
	C->bFinishedSwapping = false;
	Combat->SecondaryWeapon->EnableCustomDepth(false);
	MulticastSwapStarted();
	C->ForceNetUpdate();
	// Authority does not depend on a rendered montage firing every notify.
	// 服务器拥有挂接时刻，专服即使不评估蒙太奇通知也能完成切枪。
	GetWorld()->GetTimerManager().SetTimer(SwapAttachTimer, this, &ThisClass::CommitServerSwap, AttachTime);
	GetWorld()->GetTimerManager().SetTimer(SwapFinishTimer, this, &ThisClass::FinishServerSwap, FinishTime);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SWAP_SCHEDULE Attach=%.3f Finish=%.3f"), AttachTime, FinishTime);
}

// 在各端播放切枪动作，表现通知不承担服务器最终状态提交。
void UExtractionCombatComponent::MulticastSwapStarted_Implementation()
{
	if (auto* C = Character()) C->PlaySwapMontage();
}

// 挂接时再检查两把枪与开始时一致，避免期间丢弃或更换后把过期对象重新挂回。
void UExtractionCombatComponent::CommitServerSwap()
{
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SWAP_COMMIT"));
	SwapAttachTimer.Invalidate();
	auto* C = Character();
	if (!C || !CanAct())
	{
		CancelSwap();
		return;
	}
	auto* Combat = C->GetCombat().Get();
	if (Combat->CombatState != ECombatState::ECS_SwappingWeapon || !SwapPrimary.IsValid()
		|| !SwapSecondary.IsValid() || Combat->EquippedWeapon != SwapPrimary.Get()
		|| Combat->SecondaryWeapon != SwapSecondary.Get())
	{
		CancelSwap();
		return;
	}
	Combat->AttachSwappedWeapons();
	SwapPrimary.Reset();
	SwapSecondary.Reset();
	C->ForceNetUpdate();
}

// 结束计时后统一进入切枪清理，解除动作锁并恢复副武器轮廓。
void UExtractionCombatComponent::FinishServerSwap() { CancelSwap(); }

// 取消挂接/结束计时器并清空快照；已完成的挂接不会被此函数反向交换。
void UExtractionCombatComponent::CancelSwap()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SwapAttachTimer);
		GetWorld()->GetTimerManager().ClearTimer(SwapFinishTimer);
	}
	SwapPrimary.Reset();
	SwapSecondary.Reset();
	if (auto* C = Character())
	{
		auto* Combat = C->GetCombat().Get();
		if (Combat->CombatState == ECombatState::ECS_SwappingWeapon) Combat->CombatState = ECombatState::ECS_Unoccuiped;
		C->bFinishedSwapping = true;
		if (Combat->SecondaryWeapon) Combat->SecondaryWeapon->EnableCustomDepth(true);
		C->ForceNetUpdate();
	}
}

// 销毁前清除镜头临时偏移和定时器，防止后续回调访问失效角色。
void UExtractionCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelFireInput();
	ResetTransientMechanics();
	CancelSwap();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	Super::EndPlay(Reason);
}
