#include "BrokenEdge/Extraction/Core/WeaponRangeGameMode.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
// 一次性本地训练模式，复用撤离武器和背包规则，但跳过正式出战票据、结算与奖励流程。
// 不要把训练场能开枪等同于正式撤离完整闭环通过；持久化层另为训练地图选择隔离目录。

// 训练没有大厅小队分组，直接使用基础 GameMode 的显式 PlayerStart。
AActor* AWeaponRangeGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Local training has no lobby squad assignment; use its explicit PlayerStart.
	return AGameModeBase::ChoosePlayerStart_Implementation(Player);
}

// 指定训练地图定义并强制单人预期人数，其余初始化仍由撤离基类完成。
void AWeaponRangeGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
	MapDefinition = FSoftObjectPath(TEXT("/Game/Extraction/Training/DA_WeaponRangeMap.DA_WeaponRangeMap"));
	Super::InitGame(Map, Options + TEXT("?ExpectedPlayers=1"), Error);
}

// 训练生成后直接配置 AK、SCAR-H 和测试弹药附件，再同步可见武器。
void AWeaponRangeGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
	Super::HandleStartingNewPlayer_Implementation(Player);
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || !Player->GetPawn()) return;
	auto* Inventory = PS->Inventory.Get();
	FExtractionContainer Candidate;
	Candidate.Revision = Inventory->GetContents().Revision;
	Candidate.Id = FGuid::NewGuid();
	Candidate.WeaponSlots = true;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto Gun = ExtractionItems::Make(Slot == 0 ? TEXT("AK") : TEXT("SCARH"));
		Gun.Slot = Slot;
		Candidate.Items.Add(Gun);
	}
	for (FName Id : {
		     FName(TEXT("Ammo")), FName(TEXT("Ammo76251")), FName(TEXT("VerticalGrip")), FName(TEXT("RedDot")),
		     FName(TEXT("Compensator"))
	     })
		ExtractionItems::AutoPlace(Candidate,
		                           ExtractionItems::Make(Id, Id.ToString().StartsWith(TEXT("Ammo")) ? 60 : 1));
	const auto Result = Inventory->Mutate([&](FExtractionContainer& Next, FString&)
	{ Next = Candidate; return true; });
	if (!Result.Ok()) UE_LOG(LogTemp, Error, TEXT("EXTRACTION_RANGE_LOADOUT_FAILED %s"), *Result.Error);
}

// 有意不调用基类对局 Tick：持续提供 Active 测试环境并解除冻结，不激活或结算长期出战记录。
void AWeaponRangeGameMode::Tick(float Delta)
{
	// Training deliberately does not tick raid start/settlement or issue persistent rewards.
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	GS->Phase = EExtractionPhase::Active;
	GS->Deadline = GetWorld()->GetTimeSeconds() + 3600;
	GS->StorageError.Reset();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (auto* C = Cast<ABrokenEdgeCharacter>(It->Get()->GetPawn()))
		{
			C->bDisableGameplay = false;
			if (C->GetCharacterMovement()->MovementMode == MOVE_None)
				C->GetCharacterMovement()->SetMovementMode(
					MOVE_Walking);
		}
}
