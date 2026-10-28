#include "ExtractionMeleeComponent.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameFramework/DamageType.h"
// 常驻近战槽独立于持久化枪械槽：Active 由服务器复制，刀模型和蒙太奇在各端按视角呈现。
// 命中使用服务器定时球形扫描；动画资产仅提供表现，原型伤害和时机不等同于源游戏规则。

// 开启复制与逐帧表现刷新，以应对第一人称手臂的销毁和重建。
UExtractionMeleeComponent::UExtractionMeleeComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
}

// 返回持刀角色，所有输入和挂接状态以该角色为上下文。
ABrokenEdgeCharacter* UExtractionMeleeComponent::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

// 沿用撤离战斗资格并要求背包关闭，不能在失能或物品操作中使用近战。
bool UExtractionMeleeComponent::CanAct() const
{
	const auto* C = Character();
	return C && C->GetExtractionCombat()->CanAct() && !C->GetExtractionInteraction()->InventoryOpen;
}

// 攻击冷却截止前视为忙碌，防止切槽或重复发起攻击。
bool UExtractionMeleeComponent::Busy() const { return GetWorld() && GetWorld()->GetTimeSeconds() < NextAttack; }

// 复制是否选中刀槽；模型组件在本地创建，不依赖复制组件对象。
void UExtractionMeleeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExtractionMeleeComponent, Active);
}

// 服务器检查行动与动作锁，取消枪械输入和医疗，再选择刀或返回枪械。
void UExtractionMeleeComponent::Select_Implementation(bool Knife, bool SwapGun)
{
	auto* C = Character();
	if (!CanAct() || Busy() || !C->GetCombat() || C->GetCombatState() != ECombatState::ECS_Unoccuiped)
		return;
	C->GetExtractionInteraction()->StopCombatInput();
	C->GetExtractionInteraction()->CancelMedical();
	Active = Knife;
	if (!Knife && SwapGun) C->GetCombat()->SwapWeapons();
	C->ForceNetUpdate();
}

// 服务器设置轻/重击冷却和伤害时刻，再广播动作；命中不依赖动画通知。
void UExtractionMeleeComponent::Attack_Implementation(bool Heavy)
{
	if (!Active || !CanAct() || Busy()) return;
	HeavyAttack = Heavy;
	// Prototype timing and damage are intentionally separate from source animation metadata.
	NextAttack = GetWorld()->GetTimeSeconds() + (Heavy ? 1.0 : .6);
	Character()->GetExtractionInteraction()->CancelMedical();
	GetWorld()->GetTimerManager().SetTimer(StrikeTimer, this, &ThisClass::Strike, Heavy ? .35f : .18f);
	PlayAttack(Heavy);
}

// 伤害时刻再次检查状态，从眼睛沿朝向做球体扫掠并应用点伤害，避免攻击开始后失能仍命中。
void UExtractionMeleeComponent::Strike()
{
	if (!Active || !CanAct()) return;
	auto* C = Character();
	FVector Start;
	FRotator Direction;
	C->GetActorEyesViewPoint(Start, Direction);
	const FVector End = Start + Direction.Vector() * (HeavyAttack ? 170.f : 150.f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionKnife), false, C);
	if (C->GetEquippedWeapon()) Query.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
	                                     FCollisionShape::MakeSphere(14), Query) && Hit.GetActor())
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), HeavyAttack ? 65.f : 35.f, Direction.Vector(), Hit,
		                                   C->GetController(), C, UDamageType::StaticClass());
}

// 蒙太奇打进的槽必须真的存在于动画图里：槽缺失时 Montage_Play 照样返回时长，画面却完全没有动作。
static void PlaySlotMontage(UAnimInstance* Anim, UAnimMontage* Montage, const TCHAR* Label)
{
	if (!Anim || !Montage) return;
	const float Length = Anim->Montage_Play(Montage);
	const FName Slot = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].SlotName : NAME_None;
	if (Length <= 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("MELEE_MONTAGE_REJECTED %s anim=%s slot=%s"), Label,
		       *GetNameSafe(Anim->GetClass()), *Slot.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("MELEE_MONTAGE_PLAY %s anim=%s slot=%s slotweight=%.3f"), Label,
		       *GetNameSafe(Anim->GetClass()), *Slot.ToString(), Anim->GetSlotNodeGlobalWeight(Slot));
	}
}

// 多播播放第三人称与可用的第一人称蒙太奇；不在表现回调中计算伤害。
void UExtractionMeleeComponent::PlayAttack_Implementation(bool Heavy)
{
	auto* C = Character();
	if (!C) return;
	if (auto* Anim = C->GetMesh()->GetAnimInstance())
		PlaySlotMontage(Anim, Heavy ? HeavyBody.Get() : LightBody.Get(), TEXT("body"));
	if (auto* View = C->GetFirstPersonMesh())
		if (auto* Anim = View->GetAnimInstance())
			PlaySlotMontage(Anim, Heavy ? HeavyView.Get() : LightView.Get(), TEXT("view"));
}

// 每帧维护刀的可见性和挂接，视角切换后不保留失效的手臂父组件。
void UExtractionMeleeComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Type, Function);
	RefreshPresentation();
}

// 销毁第一人称刀组件并置空，允许下次按新的手臂实例重建。
void UExtractionMeleeComponent::ReleaseViewMesh()
{
	if (IsValid(ViewMesh)) ViewMesh->DestroyComponent();
	ViewMesh = nullptr;
}

// 同时检查父组件和 RightHand 挂点，单纯组件存在不能证明挂在当前手臂上。
bool UExtractionMeleeComponent::IsViewAttached() const
{
	const auto* C = Character();
	return C && IsValid(ViewMesh) && IsValid(C->GetFirstPersonMesh())
		&& ViewMesh->GetAttachParent() == C->GetFirstPersonMesh()
		&& ViewMesh->GetAttachSocketName() == TEXT("RightHand");
}

// 重建第一人称手臂时，按第三人称正在播放的攻击进度和时长比例恢复动作。
void UExtractionMeleeComponent::RestoreViewAttack()
{
	auto* C = Character();
	if (!C || !Active || !IsValid(C->GetFirstPersonMesh())) return;
	auto* Body = C->GetMesh()->GetAnimInstance();
	auto* View = C->GetFirstPersonMesh()->GetAnimInstance();
	if (!Body || !View) return;
	for (bool Heavy : {false, true})
	{
		auto* BodyAction = Heavy ? HeavyBody.Get() : LightBody.Get();
		auto* ViewAction = Heavy ? HeavyView.Get() : LightView.Get();
		if (!BodyAction || !ViewAction || !Body->Montage_IsPlaying(BodyAction)) continue;
		const float Scale = ViewAction->GetPlayLength() / FMath::Max(BodyAction->GetPlayLength(), .01f);
		View->Montage_Play(ViewAction, Scale * Body->Montage_GetPlayRate(BodyAction),
		                   EMontagePlayReturnType::MontageLength, Body->Montage_GetPosition(BodyAction) * Scale);
		break;
	}
}

// 失去行动资格时服务器收刀；各端维护枪械隐藏、世界刀与本人专用刀，并重建过期挂接。
void UExtractionMeleeComponent::RefreshPresentation()
{
	auto* C = Character();
	if (!C) return;
	if (C->HasAuthority() && Active && !C->GetExtractionCombat()->CanAct())
	{
		Active = false;
		GetWorld()->GetTimerManager().ClearTimer(StrikeTimer);
	}
	auto* Gun = C->GetEquippedWeapon();
	if (HiddenGun.IsValid() && (HiddenGun != Gun || !Active)) HiddenGun->SetActorHiddenInGame(false);
	HiddenGun = Active ? Gun : nullptr;
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(true);
	if (!Mesh) return;
	if (!WorldMesh)
	{
		WorldMesh = NewObject<USkeletalMeshComponent>(C);
		WorldMesh->SetupAttachment(C->GetMesh(), TEXT("RightHand"));
		WorldMesh->SetSkeletalMeshAsset(Mesh);
		WorldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WorldMesh->SetCanEverAffectNavigation(false);
		WorldMesh->SetRelativeTransform(BodyGripCorrection * HandOffset);
		WorldMesh->RegisterComponent();
	}
	WorldMesh->SetVisibility(Active && !C->IsDefeated());
	WorldMesh->SetOwnerNoSee(C->IsLocallyControlled() && C->IsFirstPersonView());
	// The dedicated arms are destroyed on every return to third person.
	// A surviving knife component must never retain that obsolete parent.
	if (ViewMesh && (!IsViewAttached() || !C->IsFirstPersonView())) ReleaseViewMesh();
	if (!ViewMesh && IsValid(C->GetFirstPersonMesh()) && C->IsFirstPersonView())
	{
		ViewMesh = NewObject<USkeletalMeshComponent>(C);
		ViewMesh->SetupAttachment(C->GetFirstPersonMesh(), TEXT("RightHand"));
		ViewMesh->SetSkeletalMeshAsset(Mesh);
		ViewMesh->SetOnlyOwnerSee(true);
		ViewMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		ViewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ViewMesh->SetCastShadow(false);
		ViewMesh->SetCanEverAffectNavigation(false);
		ViewMesh->SetRelativeTransform(ViewGripCorrection * HandOffset);
		ViewMesh->RegisterComponent();
		ViewMesh->AddTickPrerequisiteComponent(C->GetFirstPersonMesh());
		RestoreViewAttack();
	}
	if (ViewMesh)
		ViewMesh->SetVisibility(
			Active && C->IsLocallyControlled() && C->IsFirstPersonView() && !C->IsDefeated());
}

// 清除命中定时器，恢复曾隐藏的枪械并销毁动态刀组件。
void UExtractionMeleeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(StrikeTimer);
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(false);
	if (WorldMesh) WorldMesh->DestroyComponent();
	ReleaseViewMesh();
	Super::EndPlay(Reason);
}
