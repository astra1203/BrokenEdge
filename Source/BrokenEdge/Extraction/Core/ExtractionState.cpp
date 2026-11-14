#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Extraction/Map/ExtractionMapDefinition.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "BrokenEdge/Extraction/World/ExtractionBreakableGlass.h"
#include "EngineUtils.h"
// 对局状态与玩家状态是服务器到客户端的可复制读模型；实际规则由 GameMode 等服务器逻辑执行。
// GameState 描述整局，PlayerState 描述单人结果和背包，不把对局状态绑定在可销毁的角色 Pawn 上。

// 创建玩家背包默认子对象，使物品状态随 PlayerState 管理。
AExtractionPlayerState::AExtractionPlayerState()
{
	Inventory = CreateDefaultSubobject<UExtractionInventoryComponent>(TEXT("ExtractionInventory"));
}

// 注册本类需要同步的属性并保留父类复制项；客户端读取状态，不在复制回调中做仓库结算。
void AExtractionGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionGameState, Phase);
	DOREPLIFETIME(AExtractionGameState, MapDefinition);
	DOREPLIFETIME(AExtractionGameState, Deadline);
	DOREPLIFETIME(AExtractionGameState, AlivePlayers);
	DOREPLIFETIME(AExtractionGameState, EndReason);
	DOREPLIFETIME(AExtractionGameState, StorageError);
	DOREPLIFETIME(AExtractionGameState, BrokenGlass);
}

void AExtractionGameState::OnRep_BrokenGlass()
{
	const TSet<FGuid> Broken(BrokenGlass);
	for (TActorIterator<AExtractionBreakableGlass> It(GetWorld()); It; ++It)
		if (Broken.Contains(It->PaneId)) It->ApplyBroken();
}

void AExtractionGameState::MulticastGlassBreak_Implementation(FGuid PaneId, FVector_NetQuantize Impact,
                                                              FVector_NetQuantizeNormal Direction)
{
	for (TActorIterator<AExtractionBreakableGlass> It(GetWorld()); It; ++It)
		if (It->PaneId == PaneId)
		{
			It->ApplyBroken();
			It->PlayBreakEffect(Impact, Direction);
			break;
		}
}

// 注册本类需要同步的属性并保留父类复制项；客户端读取状态，不在复制回调中做仓库结算。
void AExtractionPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionPlayerState, Life);
	DOREPLIFETIME(AExtractionPlayerState, BleedAt);
	DOREPLIFETIME(AExtractionPlayerState, DownHealth);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, ExtractAt, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, ExtractExitId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, ExtractBlockedReason, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, RaidProgress, COND_OwnerOnly);
}
