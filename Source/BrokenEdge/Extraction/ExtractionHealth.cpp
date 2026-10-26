#include "ExtractionHealth.h"
#include "ExtractionAbilities.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionGameMode.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionState.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Net/UnrealNetwork.h"
// 当前角色的创伤组件：处理弹药倍率、护甲吸收、出血与腿部骨折；护甲耐久仍保存在物品实例。
// 命中由服务器执行，复制只传创伤状态；死亡/倒地的最终分流交给 ExtractionGameMode。

// 启用复制并每 0.1 秒检查出血时间门槛。
UExtractionHealth::UExtractionHealth()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .1f;
}

// 获取当前受伤角色，组件不管理跨关卡的账号生命状态。
ABrokenEdgeCharacter* UExtractionHealth::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

// 同步出血与骨折布尔状态，供 UI 和角色表现读取。
void UExtractionHealth::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExtractionHealth, Bleeding);
	DOREPLIFETIME(UExtractionHealth, LegFracture);
}

// 兼容项目骨架的腿部骨名，用于决定有效创伤是否附带骨折。
bool UExtractionHealth::IsLeg(FName Bone)
{
	const FString Name = Bone.ToString();
	return Name.StartsWith(TEXT("thigh_")) || Name.StartsWith(TEXT("calf_")) || Name.StartsWith(TEXT("foot_")) || Name.
		StartsWith(TEXT("ball_"))
		|| Name == TEXT("LeftUpLeg") || Name == TEXT("RightUpLeg") || Name == TEXT("LeftLeg") || Name == TEXT(
			"RightLeg")
		|| Name == TEXT("LeftFoot") || Name == TEXT("RightFoot") || Name == TEXT("LeftToeBase") || Name == TEXT(
			"RightToeBase");
}

// 头部映射头盔槽 2，骨盆/脊柱映射护甲槽 3；其余部位返回 -1 不套用护甲。
int32 UExtractionHealth::ArmorSlotForBone(FName Bone)
{
	if (Bone == TEXT("head")) return 2;
	return Bone == TEXT("pelvis") || Bone == TEXT("Hips") || Bone.ToString().StartsWith(TEXT("spine")) ? 3 : -1;
}

// 按穿透等级选择吸收率，并受剩余耐久限制；引用参数扣耐久，返回未被吸收的伤害。
float UExtractionHealth::Absorb(float Damage, int32 Penetration, int32 ArmorLevel, float& Durability)
{
	const auto* Rules = GetDefault<UExtractionEquipmentRules>();
	const float Absorbed = FMath::Min(FMath::Max(0.f, Durability), FMath::Max(0.f, Damage) *
	                                  FMath::Clamp(Penetration < ArmorLevel
		                                               ? Rules->StoppedAbsorption
		                                               : Rules->PenetratedAbsorption, 0.f, 1.f));
	Durability -= Absorbed;
	return FMath::Max(0.f, Damage - Absorbed);
}

// 服务器先过滤友军等无效伤害，再应用弹药/守卫倍率、护甲和生命损失，最后依据实际掉血产生创伤。
void UExtractionHealth::Bullet(float Damage, FName Bone, FName Ammo, AController* Attacker, AActor* Causer)
{
	auto* C = Character();
	auto* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>();
	if (!C || !C->HasAuthority() || !GM || C->IsDefeated() || !FMath::IsFinite(Damage)) return;
	Damage = GM->CalculateDamage(Attacker, C->GetController(), Damage);
	if (Damage <= 0) return; // Friendly fire cannot damage equipment or create trauma.
	const auto* AmmoDef = UExtractionItemDefinition::Get(Ammo);
	if (!AmmoDef || AmmoDef->Kind != EExtractionItemKind::Ammo) return;
	// Bullet damage is validated again in ReceiveDamage; scale only at this entry.
	// The attacker, not the weapon instance, determines guard difficulty after looting.
	if (const auto* Shooter = Attacker ? Cast<ABrokenEdgeCharacter>(Attacker->GetPawn()) : nullptr;
		Shooter && Shooter->bExtractionGuard)
		Damage *= FMath::Clamp(GetDefault<UExtractionRules>()->GuardDamageScale, 0.f, 1.f);
	Damage *= AmmoDef->DamageScale;
	if (C->GetExtractionAbilities()->IsDowned())
	{
		C->ReceiveDamage(C, Damage, nullptr, Attacker, Causer);
		return;
	}
	C->GetExtractionInteraction()->CancelMedical();
	C->GetExtractionAbilities()->Damaged();
	const int32 Slot = ArmorSlotForBone(Bone);
	if (auto* Inv = C->GetExtractionInteraction()->Inventory(); Inv && Slot >= 0)
		for (auto& Item : Inv->Contents.Items)
			if (Item.Slot == Slot)
			{
				const auto* Armor = UExtractionItemDefinition::Get(Item.Definition);
				if (Armor && Armor->Kind == EExtractionItemKind::Armor && Item.Durability > 0)
				{
					Damage = Absorb(Damage, AmmoDef->Penetration, Armor->ArmorLevel, Item.Durability);
					++Inv->Contents.Revision;
					Inv->Changed();
				}
				break;
			}
	// 记录护甲后的实际生命变化，创伤判定不使用被护甲吸收的部分。
	const float Before = C->GetHealth();
	C->ReceiveDamage(C, Damage, nullptr, Attacker, Causer);
	const float Lost = Before - C->GetHealth();
#if WITH_DEV_AUTOMATION_TESTS
	++TestBulletSequence;
	TestHitBone = Bone;
	TestHealthLoss = Lost;
#endif
	if (Lost >= GetDefault<UExtractionEquipmentRules>()->TraumaThreshold && C->GetHealth() > 0 && !C->IsDefeated())
	{
		if (!Bleeding)
			NextBleed = GetWorld()->GetTimeSeconds() + GetDefault<UExtractionEquipmentRules>()->
				BleedingInterval;
		Bleeding = true;
		BleedInstigator = Attacker;
		if (IsLeg(Bone)) LegFracture = true;
		C->ForceNetUpdate();
	}
	UE_LOG(LogTemp, Display,
	       TEXT("EXTRACTION_EQUIPMENT_HIT Victim=%s Bone=%s Ammo=%s HealthDamage=%.2f Bleed=%d Fracture=%d"),
	       *C->GetName(), *Bone.ToString(), *Ammo.ToString(), Lost, Bleeding, LegFracture);
}

// 按治疗类型检查是否缺血、出血或骨折，供医疗开始与完成时复核。
bool UExtractionHealth::Needs(EExtractionMedicalEffect Effect) const
{
	const auto* C = Character();
	return C && (Effect == EExtractionMedicalEffect::Heal
		             ? C->GetHealth() < C->GetMaxHealth()
		             : Effect == EExtractionMedicalEffect::Bandage
		             ? Bleeding
		             : LegFracture);
}

// 服务器应用对应治疗并复制结果；返回 false 时上层不应消耗物品。
bool UExtractionHealth::Treat(const UExtractionItemDefinition* D)
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !D || !Needs(D->MedicalEffect)) return false;
	if (D->MedicalEffect == EExtractionMedicalEffect::Heal) C->SetHealth(C->GetHealth() + D->HealAmount);
	else if (D->MedicalEffect == EExtractionMedicalEffect::Bandage)
	{
		Bleeding = false;
		NextBleed = 0;
	}
	else LegFracture = false;
	C->UpdateHUDHealth();
	C->ForceNetUpdate();
	return true;
}

// 救起时清除出血与计时；这里没有清除骨折，避免注释误写为恢复全部创伤。
void UExtractionHealth::Revived()
{
	Bleeding = false;
	NextBleed = 0;
	GetOwner()->ForceNetUpdate();
}

// 服务器按间隔直接扣出血生命，不再经过护甲也不重新施加创伤；不能行动时推迟下一次扣血。
void UExtractionHealth::TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Kind, Tick);
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->GetExtractionCombat()->IsEnabled() || !Bleeding) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!C->GetExtractionCombat()->CanAct())
	{
		NextBleed = Now + GetDefault<UExtractionEquipmentRules>()->BleedingInterval;
		return;
	}
	if (Now < NextBleed) return;
	NextBleed = Now + FMath::Max(.1f, GetDefault<UExtractionEquipmentRules>()->BleedingInterval);
	C->SetHealth(C->GetHealth() - FMath::Max(0.f, GetDefault<UExtractionEquipmentRules>()->BleedingDamage));
	C->UpdateHUDHealth();
	C->ForceNetUpdate();
	// Internal bleeding bypasses armor and never interrupts treatment or applies a new wound.
	if (C->GetHealth() <= 0)
		if (auto* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
			GM->
				DefeatCombatant(C, BleedInstigator.Get());
}
