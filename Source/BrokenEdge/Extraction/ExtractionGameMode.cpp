#include "ExtractionGameMode.h"
#include "ExtractionLootLayout.h"
#include "ExtractionRegionStream.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionState.h"
#include "ExtractionPlayerController.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionAccountComponent.h"
#include "ExtractionExit.h"
#include "ExtractionAbilities.h"
#include "ExtractionGuard.h"
#include "ExtractionSmoke.h"
#include "EngineUtils.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "MultiplayerSessionsSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#endif
// 撤离对局的服务器调度中心：地图配置和出战凭据就绪后才进入 Active。
// GameMode 不复制到客户端；需要显示的阶段、截止时间和玩家结果写入 GameState/PlayerState。
// 结算遵循先持久化、后清理角色的顺序，避免客户端看到成功但仓库尚未保存。

// 配置本模式的状态、控制器和角色类型；延迟开局，等待大厅票据及地图加载校验。
AExtractionGameMode::AExtractionGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	bDelayedStart = true;
	bUseSeamlessTravel = true;
	GameStateClass = AExtractionGameState::StaticClass();
	PlayerStateClass = AExtractionPlayerState::StaticClass();
	PlayerControllerClass = AExtractionPlayerController::StaticClass();
	static ConstructorHelpers::FClassFinder<APawn>
		Pawn(TEXT("/Game/Characters/Stinger/Blueprints/BP_StingerCharacter"));
	if (Pawn.Succeeded()) DefaultPawnClass = Pawn.Class;
	HUDClass = ABrokenEdgeHUD::StaticClass();
	for (FName Id : UExtractionItemDefinition::Ids())
		if (const auto* Definition = UExtractionItemDefinition::Get(Id))
			ItemDefinitions.Add(const_cast<UExtractionItemDefinition*>(Definition));
}

// 读取人数、时长与出生随机种子；这里只准备参数，不代表对局已经激活。
void AExtractionGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
	Super::InitGame(Map, Options, Error);
	ExpectedPlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ExpectedPlayers"), 2), 1, 6);
	// 航天基地官方单局 30 分钟（公开玩法规则；解包资源里没有该值，由官方服务器下发）。
	float Duration = 1800;
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionRoundSeconds="), Duration);
	RoundSeconds = FMath::Clamp(static_cast<double>(Duration), 10.0, 3600.0);
	int32 Seed = FMath::Rand();
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionSpawnSeed="), Seed);
	SpawnRandom.Initialize(Seed);
	LoadedMapDefinition = MapDefinition.LoadSynchronous();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SPAWN_SEED %d"), Seed);
}

// 加载仓库和地图数据，生成战利品与撤离点，并绑定会话结束回调。
// 出生组或撤离点配置不足时保持阻塞，不能用无效地图启动正式对局。
void AExtractionGameMode::BeginPlay()
{
	Super::BeginPlay();
	LoadedAt = GetWorld()->GetTimeSeconds();
	if (auto* Persistence = Store()) Persistence->EnsureLoaded();
	LoadedMapDefinition = MapDefinition.LoadSynchronous();
	if (LoadedMapDefinition)
	{
		TSet<FName> Ids;
		if (LoadedMapDefinition->LootLayout)
		{
			int32 LootSeed = FMath::Rand();
			FParse::Value(FCommandLine::Get(), TEXT("ExtractionLootSeed="), LootSeed);
			LoadedMapDefinition->LootLayout->Spawn(GetWorld(), LootSeed);
		}
		for (const auto& Definition : LoadedMapDefinition->Exits)
		{
			if (!Definition.IsValid() || Ids.Contains(Definition.Id))
			{
				UE_LOG(LogTemp, Error, TEXT("EXTRACTION_INVALID_EXIT %s"), *Definition.Id.ToString());
				continue;
			}
			Ids.Add(Definition.Id);
			if (auto* NewExit = GetWorld()->SpawnActor<AExtractionExit>())
			{
				NewExit->Configure(Definition);
				Exits.Add(NewExit);
			}
		}
	}
	if (Exits.IsEmpty())
	{
		if (auto* GS = GetGameState<AExtractionGameState>()) GS->StorageError = TEXT("地图撤离配置缺失或无效，无法开始对局");
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_MAP_CONFIGURATION_INVALID"));
	}
	bMapConfigurationValid = LoadedMapDefinition && Exits.Num() == LoadedMapDefinition->Exits.Num()
		&& !Exits.IsEmpty() && LoadedMapDefinition->SpawnGroups.Num() >= 2;
	if (auto* Instance = GetGameInstance())
		if (auto* Sessions = Instance->GetSubsystem<UMultiplayerSessionsSubsystem>())
			Sessions->MultiplayerOnEndSessionComplete.AddUniqueDynamic(this, &ThisClass::SessionEnded);
}

// 连接准入：仅 Waiting 接受新玩家；活动中或已结束的对局返回 MATCH_IN_PROGRESS。
void AExtractionGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id,
                                   FString& Error)
{
	Super::PreLogin(Options, Address, Id, Error);
	const auto* State = GetGameState<AExtractionGameState>();
	if (State && State->Phase != EExtractionPhase::Waiting) Error = TEXT("MATCH_IN_PROGRESS");
}

// 等待阶段创建角色、从出战记录恢复携带物品并冻结操作；未生成 Pawn 时允许后续 Tick 重试。
void AExtractionGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
	const auto* State = GetGameState<AExtractionGameState>();
	if (!Player || !State || State->Phase != EExtractionPhase::Waiting) return;
	if (auto* PS = Player->GetPlayerState<AExtractionPlayerState>())
	{
		PS->Life = EExtractionLife::Alive;
		PS->SetLobbyReady(false);
		PS->SetTeam(ETeam::ET_NoTeam);
	}
	if (!Player->GetPawn()) RestartPlayer(Player);
	if (!Player->GetPawn()) return;
	if (auto* PS = Player->GetPlayerState<AExtractionPlayerState>()) PS->Inventory->InitializeLoadout();
	FreezePlayer(Player, true);
	if (auto* Character = Cast<ABrokenEdgeCharacter>(Player->GetPawn()))
		Character->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

// 只有等待阶段、存活且没有 Pawn 的玩家可生成角色；撤离模式不提供战斗中复活。
bool AExtractionGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const auto* GS = GetGameState<AExtractionGameState>();
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return GS && GS->Phase == EExtractionPhase::Waiting && PS && PS->Life == EExtractionLife::Alive && !Player->
		GetPawn();
}

// 将出生入口统一到自定义分组选择，避免默认实现退回世界原点。
AActor* AExtractionGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// GameModeBase otherwise falls back to WorldSettings at the world origin.
	return ChoosePlayerStart(Player);
}

// 按小队分配不重复的出生组，再为组员依次分配位置；返回空指针表示暂时无法安全出生。
// 候选位置必须已加载、可落地、坡度合适且胶囊无阻挡；已有分配优先复用。
AActor* AExtractionGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const auto* Existing = PlayerStarts.Find(Player); Existing && Existing->IsValid()) return Existing->Get();

	// 出生失败以前是静默 return nullptr，日志里只剩 GameModeBase 的 "Player start not found"，
	// 完全看不出是哪一步断的。这里对每一种失败只打一次，避免每帧刷屏。
	const auto Fail = [this](const FString& Key, const FString& Reason)
	{
		if (SpawnAttemptWarnings.Contains(Key)) return;
		SpawnAttemptWarnings.Add(Key);
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_SPAWN_FAIL %s | %s"), *Key, *Reason);
	};

	if (const auto* Region = AExtractionRegionStream::Find(GetWorld()); Region && !Region->IsReady())
	{
		// 启动前几秒正常情况，别当错误刷日志。
		UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_SPAWN_WAIT_REGION"));
		return nullptr;
	}
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!LoadedMapDefinition)
	{
		Fail(TEXT("no_map_definition"), TEXT("MapDefinition 未加载"));
		return nullptr;
	}
	if (!PS)
	{
		Fail(TEXT("no_player_state"), TEXT("PlayerState 不是 AExtractionPlayerState"));
		return nullptr;
	}
	const auto* Pawn = DefaultPawnClass ? Cast<ACharacter>(DefaultPawnClass->GetDefaultObject()) : nullptr;
	if (!Pawn)
	{
		Fail(TEXT("no_default_pawn"), TEXT("DefaultPawnClass 不是 ACharacter，无法取胶囊尺寸"));
		return nullptr;
	}
	// 未分配小队不再直接拒绝：拒绝等于整局没有 Pawn、阶段永远停在 Waiting。
	const uint8 Squad = PS->ExtractionSquad;
	if (Squad == 0) Fail(TEXT("squad_zero"), TEXT("ExtractionSquad=0（未分队），按无小队出生"));

	const float Radius = Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// 探测射线从 authored 位置上方起步：点数据里的 Z 是地面高度，从地面本身起探会与地面共面，
	// 浮点误差一偏就漏检（这是这次「整组被判无效」的主因）。
	const float ProbeUp = Half + 200.f;
	const float ProbeDown = Half * 2.f + 700.f;
	const float MaxDrop = Half + 600.f; // 允许地面比 authored Z 低这么多（本项目地形整体偏低约 1.8m）
	const auto Ground = [&](const FTransform& Position, FTransform& Resolved, FString& Why)
	{
		Why.Reset();
		if (Position.ContainsNaN())
		{
			Why = TEXT("transform contains NaN");
			return false;
		}
		const FVector Center = Position.GetLocation();
		const FVector Start = Center + FVector(0, 0, ProbeUp);
		const FVector End = Start - FVector(0, 0, ProbeDown);
		// 两个通道都试：还原资产里有一部分地面只挡 Pawn、不挡 Visibility
		// （数据脚本正是用 ECC_Pawn 探到的）；只查 Visibility 会把整组判成"无地面"。
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, Start, End, ECC_Pawn) &&
			!GetWorld()->LineTraceSingleByChannel(Floor, Start, End, ECC_Visibility))
		{
			Why = TEXT("probe found no floor");
			return false;
		}
		if (Floor.ImpactNormal.Z < .7f)
		{
			Why = FString::Printf(TEXT("floor normal %.2f"), Floor.ImpactNormal.Z);
			return false;
		}
		const float Drop = Center.Z - Floor.ImpactPoint.Z;
		if (Drop < -ProbeUp || Drop > MaxDrop)
		{
			Why = FString::Printf(TEXT("floor out of window drop=%.0f"), Drop);
			return false;
		}
		Resolved = Position;
		Resolved.SetLocation(Floor.ImpactPoint + FVector(0, 0, Half + 3));
		if (GetWorld()->OverlapBlockingTestByChannel(Resolved.GetLocation(), FQuat::Identity, ECC_Pawn,
		                                             FCollisionShape::MakeCapsule(Radius, Half)))
		{
			Why = TEXT("capsule blocked");
			return false;
		}
		return true;
	};
	int32* Assigned = SquadSpawnGroups.Find(Squad);
	if (!Assigned)
	{
		TArray<int32> Candidates;
		TArray<FString> Rejects;
		for (int32 I = 0; I < LoadedMapDefinition->SpawnGroups.Num(); ++I)
		{
			const auto& Group = LoadedMapDefinition->SpawnGroups[I];
			if (ClaimedSpawnGroups.Contains(I) || Group.Id.IsNone() || Group.Positions.Num() < 3) continue;
			bool Valid = true;
			FTransform Resolved;
			for (int32 P = 0; P < Group.Positions.Num() && Valid; ++P)
			{
				FString Why;
				if (!Ground(Group.Positions[P], Resolved, Why))
				{
					Valid = false;
					Rejects.Add(FString::Printf(TEXT("%s[%d] %s"), *Group.Id.ToString(), P, *Why));
				}
			}
			for (int32 A = 0; Valid && A < Group.Positions.Num(); ++A)
				for (int32 B = A + 1; Valid && B < Group.Positions.Num(); ++B)
					if (FVector::Dist2D(Group.Positions[A].GetLocation(), Group.Positions[B].GetLocation()) <=
						Radius * 2)
					{
						Valid = false;
						Rejects.Add(FString::Printf(TEXT("%s points %d/%d too close"), *Group.Id.ToString(), A, B));
					}
			if (Valid) Candidates.Add(I);
		}
		if (Candidates.IsEmpty())
		{
			Fail(TEXT("no_valid_group"), FString::Join(Rejects, TEXT(" ; ")));
			// 兜底：严格校验全挂时，宁可出生在 authored 位置（可能略悬空/贴地，落地即修正），
			// 也不能让整局卡在 Waiting —— 那会让玩家一直处于冻结状态。
			int32 Fallback = INDEX_NONE;
			int32 Best = 0;
			for (int32 I = 0; I < LoadedMapDefinition->SpawnGroups.Num(); ++I)
			{
				const auto& Group = LoadedMapDefinition->SpawnGroups[I];
				if (Group.Positions.Num() > Best)
				{
					Best = Group.Positions.Num();
					Fallback = I;
				}
			}
			if (Fallback == INDEX_NONE)
			{
				if (auto* GS = GetGameState<AExtractionGameState>())
					GS->StorageError = TEXT("没有已加载且可站立的出生组");
				return nullptr;
			}
			UE_LOG(LogTemp, Error,
			       TEXT("EXTRACTION_SPAWN_FALLBACK_GROUP group=%s points=%d（严格校验全部失败，按 authored 位置出生）"),
			       *LoadedMapDefinition->SpawnGroups[Fallback].Id.ToString(), Best);
			ClaimedSpawnGroups.Add(Fallback);
			Assigned = &SquadSpawnGroups.Add(Squad, Fallback);
		}
		else
		{
			const int32 Index = Candidates[SpawnRandom.RandRange(0, Candidates.Num() - 1)];
			ClaimedSpawnGroups.Add(Index);
			Assigned = &SquadSpawnGroups.Add(Squad, Index);
		}
	}
	const auto& Group = LoadedMapDefinition->SpawnGroups[*Assigned];
	if (Group.Positions.IsEmpty()) return nullptr;
	int32& Slot = SquadSpawnSlots.FindOrAdd(Squad);
	if (!Group.Positions.IsValidIndex(Slot)) Slot = 0;
	FTransform Resolved;
	FString Why;
	if (!Ground(Group.Positions[Slot], Resolved, Why))
	{
		// 单点兜底：这一队已经选定了，不能因为一个点探测失败就让玩家没有 Pawn。
		Resolved = Group.Positions[Slot];
		Resolved.SetLocation(Resolved.GetLocation() + FVector(0, 0, Half + 3));
		UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_SPAWN_UNVERIFIED group=%s slot=%d reason=%s"),
		       *Group.Id.ToString(), Slot, *Why);
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Start = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Resolved, Params);
	if (Start)
	{
		Start->PlayerStartTag = Group.Id;
		ClaimedStarts.Add(Start);
		PlayerStarts.Add(Player, Start);
		// 人数超过点位数时回绕，避免从第二个之后的人永远拿不到出生点。
		Slot = (Slot + 1) % Group.Positions.Num();
	}
	return Start;
}

// 冻结时同时停止移动、射击、装填、切枪和交互；解冻只恢复 Walking 移动模式。
void AExtractionGameMode::FreezePlayer(AController* Player, bool bFreeze)
{
	if (auto* Character = Player ? Cast<ABrokenEdgeCharacter>(Player->GetPawn()) : nullptr)
	{
		Character->bDisableGameplay = bFreeze;
		if (bFreeze)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
			Character->GetCharacterMovement()->DisableMovement();
			Character->GetCombat()->FireButtonPressed(false);
			Character->GetExtractionCombat()->CancelReload();
			Character->GetExtractionCombat()->CancelSwap();
			Character->GetExtractionAbilities()->Damaged();
			Character->GetExtractionInteraction()->Close();
			Character->GetExtractionInteraction()->CancelMedical();
		}
		else Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Character->ForceNetUpdate();
	}
}

// 统一检查活动阶段、玩家存活和仓库可用性；存储故障期间不允许继续有效战斗。
bool AExtractionGameMode::IsCombatActive(AController* Player) const
{
	const auto* GS = GetGameState<AExtractionGameState>();
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	const auto* Persistence = Store();
	return GS && GS->Phase == EExtractionPhase::Active && PS && PS->Life == EExtractionLife::Alive &&
		Persistence && Persistence->IsAvailable();
}

// 从 GameInstance 获取服务器仓库子系统；世界销毁期间返回空，防止访问已反初始化的子系统。
UExtractionPersistence* AExtractionGameMode::Store() const
{
	const auto* World = GetWorld();
	if (bEndingPlay || !World || World->bIsTearingDown) return nullptr;
	auto* Instance = GetGameInstance();
	return Instance ? Instance->GetSubsystem<UExtractionPersistence>() : nullptr;
}

// 将当前枪械弹药捕获到背包后提交仓库结算；返回 false 时调用者不得销毁角色或清空物品。
// 已离开存活/倒地状态的玩家直接视为处理完毕，持久化层还会按出战成员结果防止重复入库。
bool AExtractionGameMode::SettlePlayer(AController* Player, EExtractionOutcome Outcome)
{
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable()) return false;
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS) return false;
	if (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed) return true;
	if (!PS->Inventory || !PS->ExtractionRaid.IsValid() || !PS->ExtractionProfile.IsValid()) return false;
	PS->Inventory->CaptureWeapons();
	auto Progress = PS->RaidProgress;
	for (TActorIterator<AExtractionKeyRoom> It(GetWorld()); It; ++It) Progress.FileLots.Append(It->FileLots);
	if (!Persistence->Settle(PS->ExtractionRaid, PS->ExtractionProfile, Outcome, PS->Inventory->Contents, Progress))
		return false;
#if WITH_DEV_AUTOMATION_TESTS
	FString HoldFile;
	if (Outcome == EExtractionOutcome::Extracted && FParse::Value(FCommandLine::Get(),
	                                                              TEXT("ExtractionHoldSettlement="), HoldFile))
	{
		// External recovery test terminates this owned server after durable commit, before any result acknowledgement.
		FFileHelper::SaveStringToFile(TEXT("committed-before-ack"), *HoldFile);
		for (;;) FPlatformProcess::Sleep(1);
	}
#endif
	PS->ExtractAt = 0;
	if (auto* Account = Player->FindComponentByClass<UExtractionAccountComponent>()) Account->Refresh();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SETTLED Profile=%s Outcome=%d"), *PS->ExtractionProfile.ToString(),
	       static_cast<int32>(Outcome));
	return true;
}

// 逐玩家检测可用撤离区域，以服务器世界时间推进停留倒计时。
// 离开区域会清空进度，换撤离点会重新计时；成功写入仓库后才消耗名额、清空背包并转为观战。
void AExtractionGameMode::UpdateExtraction(double Now)
{
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* PC = Cast<AExtractionPlayerController>(It->Get());
		auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
		auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
		if (!PS || PS->Life != EExtractionLife::Alive || !C || C->IsDefeated()) continue;
		AExtractionExit* Exit = nullptr;
		for (const auto& Candidate : Exits)
			if (IsValid(Candidate) && Candidate->Available(Now) && Candidate->Contains(C->GetActorLocation()))
			{
				if (!Exit || Candidate->Definition.Id == PS->ExtractExitId) Exit = Candidate;
			}
		if (!Exit)
		{
			PS->ExtractAt = 0;
			PS->ExtractExitId = NAME_None;
			if (!PS->ExtractBlockedReason.IsEmpty()) PS->ExtractBlockedReason.Reset();
			continue;
		}
		// 丢包撤：网格里留着任何战利品都不放行，装备槽（Slot>=0）不算背包。
		FString Blocked;
		if (Exit->Definition.bRequireEmptyPack && PS->Inventory)
		{
			int32 Carried = 0;
			for (const auto& Item : PS->Inventory->Contents.Items)
				if (Item.Slot < 0 && Item.Quantity > 0) ++Carried;
			if (Carried > 0)
				Blocked = FString::Printf(TEXT("丢包撤：请先丢弃背包内 %d 件物资"), Carried);
		}
		if (PS->ExtractBlockedReason != Blocked)
		{
			PS->ExtractBlockedReason = Blocked;
			PS->ForceNetUpdate();
		}
		if (!Blocked.IsEmpty())
		{
			PS->ExtractExitId = Exit->Definition.Id;
			if (PS->ExtractAt > 0)
			{
				PS->ExtractAt = 0;
				PS->ForceNetUpdate();
			}
			continue;
		}
		if (PS->ExtractAt <= 0 || PS->ExtractExitId != Exit->Definition.Id)
		{
			PS->ExtractExitId = Exit->Definition.Id;
			PS->ExtractAt = Now + Exit->Definition.HoldSeconds;
			PS->ForceNetUpdate();
		}
		// 倒计时结束也不能立即宣布成功：先冻结并提交存档，失败时保留角色等待后续处理。
		if (Now < PS->ExtractAt) continue;
		FreezePlayer(PC, true);
		if (!SettlePlayer(PC, EExtractionOutcome::Extracted)) continue;
		Exit->Consume();
		PS->ExtractExitId = NAME_None;
		PS->Life = EExtractionLife::Extracted;
		PS->ForceNetUpdate();
		PS->Inventory->ClearSettled();
		PC->BeginExtractionSpectating();
		C->Destroy();
	}
}

// 仅活动对局允许有效伤害；过滤无效数值、失能攻击者、友军和守卫之间的伤害。
float AExtractionGameMode::CalculateDamage(AController* Attacker, AController* Victim, float Damage)
{
	const auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || GS->Phase != EExtractionPhase::Active || !Store() || !Store()->IsAvailable() || !FMath::IsFinite(Damage))
		return 0;
	const auto Valid = [](AController* Controller, bool Attacking)
	{
		const auto* C = Controller ? Cast<ABrokenEdgeCharacter>(Controller->GetPawn()) : nullptr;
		if (!C || C->IsDefeated() || C->bDisableGameplay) return false;
		if (C->bExtractionGuard) return true;
		const auto* PS = Controller->GetPlayerState<AExtractionPlayerState>();
		return PS && (PS->Life == EExtractionLife::Alive || (!Attacking && PS->Life == EExtractionLife::Downed));
	};
	if (!Valid(Victim, false) || (Attacker && !Valid(Attacker, true))) return 0;
	const auto* A = Attacker ? Cast<ABrokenEdgeCharacter>(Attacker->GetPawn()) : nullptr;
	const auto* B = Victim ? Cast<ABrokenEdgeCharacter>(Victim->GetPawn()) : nullptr;
	if (A && B && (UExtractionAbilities::Friendly(A, B) || (A->bExtractionGuard && B->bExtractionGuard))) return 0;
	return FMath::Max(0.f, Damage);
}

// 统计仍在线的存活和倒地成员；倒地玩家仍可能被救援，因此计入未结束人数。
void AExtractionGameMode::RefreshAliveCount()
{
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	int32 Count = 0;
	for (APlayerState* State : GS->PlayerArray)
		if (const auto* PS = Cast<AExtractionPlayerState>(State); PS && (PS->Life == EExtractionLife::Alive || PS->Life
				== EExtractionLife::Downed) && !PS->
			IsInactive())
			++Count;
	GS->AlivePlayers = Count;
}

// 服务器状态机入口：等待加载与票据 → 激活对局 → 处理倒地、超时和撤离 → 结束会话并回大厅。
// 仓库不可用时冻结玩家并提前返回，不让未持久化的物资变化继续扩大。
void AExtractionGameMode::Tick(float Delta)
{
	Super::Tick(Delta);
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bMapConfigurationValid)
	{
		GS->StorageError = TEXT("地图出生或撤离配置无效，无法开始对局");
		return;
	}
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable())
	{
		GS->StorageError = Persistence ? Persistence->Error : TEXT("仓库服务不可用");
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), true);
		return;
	}
	RefreshAliveCount();
	if (GS->Phase == EExtractionPhase::Waiting)
	{
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && !It->Get()->GetPawn())
				HandleStartingNewPlayer(It->Get());
		int32 Ready = 0;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && It->Get()->GetPawn()) ++Ready;
		const auto* Region = AExtractionRegionStream::Find(GetWorld());
		if (Ready >= ExpectedPlayers && (!Region || Region->IsReady()))
		{
			if (GS->StorageError == TEXT("没有已加载且可站立的出生组")) GS->StorageError.Reset();
			FGuid Raid;
			// 核对所有在线成员必须来自同一份未关闭出战记录，且档案不重复、人数与记录一致。
			bool ValidTickets = true;
			TSet<FGuid> Profiles;
			for (APlayerState* State : GS->PlayerArray)
			{
				const auto* PS = Cast<AExtractionPlayerState>(State);
				if (!PS || !PS->ExtractionRaid.IsValid() || !PS->ExtractionProfile.IsValid())
				{
					ValidTickets = false;
					continue;
				}
				if (!Raid.IsValid()) Raid = PS->ExtractionRaid;
				ValidTickets &= Raid == PS->ExtractionRaid && !Profiles.Contains(PS->ExtractionProfile);
				Profiles.Add(PS->ExtractionProfile);
			}
			const auto* Record = Persistence->FindRaid(Raid);
			ValidTickets &= Record && !Record->Closed && Record->Players.Num() == Profiles.Num();
			if (Record)
				for (const auto& Member : Record->Players)
					ValidTickets &= Profiles.Contains(Member.Profile) && Member.Outcome == EExtractionOutcome::None;
			if (!ValidTickets || !Raid.IsValid())
			{
				FinishRound(TEXT("请从大厅准备后进入对局"));
				return;
			}
			if (!Persistence->ActivateRaid(Raid))
			{
				if (Persistence->IsAvailable()) FinishRound(TEXT("出战记录已取消"));
				return;
			}
			// A prepared lobby ticket is the only valid way to enter an active raid.
			GS->Phase = EExtractionPhase::Active;
			for (const auto& Exit : Exits) if (IsValid(Exit)) Exit->StartRound(Now);
			GS->Deadline = Now + RoundSeconds;
			StartMatch();
			SpawnGuards();
			for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), false);
			UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ACTIVE Players=%d Duration=%.1f"), Ready, RoundSeconds);
		}
		else if (Now - LoadedAt >= 120) FinishRound(TEXT("玩家加载超时"));
	}
	else if (GS->Phase == EExtractionPhase::Active)
	{
		UpdateDowned(Now);
		RefreshAliveCount();
		if (GS->AlivePlayers == 0) FinishRound(TEXT("无人存活"));
		else if (Now >= GS->Deadline) FinishRound(TEXT("本轮时间结束"));
		else UpdateExtraction(Now);
	}
	else if (!bTravelRequested && !bEndRequested && Now >= ReturnAt)
	{
		auto* Instance = GetGameInstance();
		auto* Sessions = Instance ? Instance->GetSubsystem<UMultiplayerSessionsSubsystem>() : nullptr;
		if (Sessions && Sessions->IsBusy()) return;
		bEndRequested = true;
		if (Sessions && Sessions->HasSession()) Sessions->EndSession();
		else SessionEnded(true);
	}
}

// 等待阶段取消尚未激活的出战记录；活动阶段将剩余成员按超时结算。
// 只有全部必要写入成功后才发布 Ended，并安排十秒后的会话结束。
void AExtractionGameMode::FinishRound(const FString& Reason)
{
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable()) return;
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || GS->Phase == EExtractionPhase::Ended) return;
	if (GS->Phase == EExtractionPhase::Waiting)
	{
		// A failed hard travel can lose every PlayerState ticket. The server store still owns the records.
		if (!Persistence->AbortPreparedRaids()) return;
	}
	else
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* PS = It->Get()->GetPlayerState<AExtractionPlayerState>();
			if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) continue;
			if (!SettlePlayer(It->Get(), EExtractionOutcome::TimedOut)) return;
			PS->Life = EExtractionLife::TimedOut;
			PS->Inventory->ClearSettled();
		}
	RemoveGuards();
	GS->Phase = EExtractionPhase::Ended;
	GS->EndReason = Reason;
	ReturnAt = GetWorld()->GetTimeSeconds() + 10;
	GS->Deadline = ReturnAt;
	GS->ForceNetUpdate();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), true);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ENDED Reason=%s"), *Reason);
}

// 异步会话结束后的旅行入口；失败时延后两秒重试，标志位阻止重复旅行请求。
void AExtractionGameMode::SessionEnded(bool bSuccess)
{
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	bEndRequested = false;
	if (bTravelRequested) return;
	if (!bSuccess)
	{
		ReturnAt = GetWorld()->GetTimeSeconds() + 2;
		return;
	}
	const int32 Capacity = GameSession ? GameSession->MaxPlayers : 2;
	bTravelRequested = GetWorld()->ServerTravel(FString::Printf(TEXT("/Game/Maps/Lobby?MaxPlayers=%d"), Capacity));
	if (!bTravelRequested) ReturnAt = GetWorld()->GetTimeSeconds() + 2;
}

// 接入既有死亡回调，交由撤离模式决定进入倒地还是最终死亡。
void AExtractionGameMode::PlayerDefeated(ABrokenEdgeCharacter* Character, ABrokenEdgePlayerController* Victim,
                                         ABrokenEdgePlayerController* Attacker)
{
	DefeatCombatant(Character, Attacker);
}

// 判断是否仍有在线、存活且有 Pawn 的同小队成员；不等同于当前已在救援距离内。
bool AExtractionGameMode::HasRescuer(const AExtractionPlayerState* Player) const
{
	const auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || !Player || !Player->ExtractionSquad) return false;
	for (const APlayerState* State : GS->PlayerArray)
		if (const auto* PS = Cast<AExtractionPlayerState>(State); PS && PS != Player && PS->ExtractionSquad == Player->
			ExtractionSquad
			&& PS->Life == EExtractionLife::Alive && !PS->IsInactive() && PS->GetPawn())
			return true;
	return false;
}

// 伤害作用于独立的倒地生命；降至零时强制最终死亡，返回值表示是否处理了倒地伤害。
bool AExtractionGameMode::DamageDowned(ABrokenEdgeCharacter* C, float Damage)
{
	auto* PS = C ? C->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life != EExtractionLife::Downed) return false;
	PS->DownHealth = FMath::Max(0.f, PS->DownHealth - Damage);
	PS->ForceNetUpdate();
	if (PS->DownHealth <= 0) DefeatCombatant(C, nullptr, true);
	return true;
}

// 先取消动作；守卫走击杀统计和尸体回收，玩家则依据救援条件选择倒地或持久化死亡。
void AExtractionGameMode::DefeatCombatant(ABrokenEdgeCharacter* C, AController* Attacker, bool bFinal)
{
	if (!C || C->IsDefeated()) return;
	C->GetExtractionAbilities()->Damaged();
	C->GetExtractionCombat()->CancelReload();
	C->GetExtractionCombat()->CancelSwap();
	C->GetExtractionInteraction()->Close();
	C->GetExtractionInteraction()->CancelMedical();
	C->GetCombat()->FireButtonPressed(false);
	C->GetExtractionInteraction()->StopCombatInput();
	if (C->bExtractionGuard)
	{
		if (auto* Killer = Attacker ? Attacker->GetPlayerState<AExtractionPlayerState>() : nullptr;
			Killer && (Killer->Life == EExtractionLife::Alive || Killer->Life == EExtractionLife::Downed))
		{
			++Killer->RaidProgress.GuardKills;
			Killer->ForceNetUpdate();
		}
		C->Defeated(false);
		C->SetLifeSpan(10);
		return;
	}
	auto* PC = Cast<ABrokenEdgePlayerController>(C->GetController());
	auto* PS = C->GetPlayerState<AExtractionPlayerState>();
	if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return;
	if (!bFinal && PS->Life == EExtractionLife::Alive && HasRescuer(PS))
	{
		PS->Life = EExtractionLife::Downed;
		PS->DownHealth = GetDefault<UExtractionRules>()->DownHealth;
		PS->BleedAt = GetWorld()->GetTimeSeconds() + GetDefault<UExtractionRules>()->BleedSeconds;
		PS->ExtractAt = 0;
		PS->ForceNetUpdate();
		C->Crouch();
		C->ForceNetUpdate();
		return;
	}
	if (!SettlePlayer(PC, EExtractionOutcome::Dead)) return;
	PS->Life = EExtractionLife::Dead;
	PS->BleedAt = 0;
	PS->DownHealth = 0;
	PS->ForceNetUpdate();
	Super::PlayerDefeated(C, PC, Cast<ABrokenEdgePlayerController>(Attacker));
	RefreshAliveCount();
}

// 服务器检测流血截止时间和救援资格；超时或已无救援者则结束倒地状态。
void AExtractionGameMode::UpdateDowned(double Now)
{
	for (APlayerState* State : GetGameState<AExtractionGameState>()->PlayerArray)
		if (auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->Life == EExtractionLife::Downed
			&& (Now >= PS->BleedAt || !HasRescuer(PS)))
			DefeatCombatant(Cast<ABrokenEdgeCharacter>(PS->GetPawn()), nullptr, true);
}

// 覆写基础复活请求：终态玩家进入观战并销毁旧角色，不生成新的战斗 Pawn。
void AExtractionGameMode::RequestRespawn(ACharacter* Character, AController* Controller)
{
	auto* PC = Cast<AExtractionPlayerController>(Controller);
	const auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life == EExtractionLife::Alive || PS->Life == EExtractionLife::Downed) return;
	PC->BeginExtractionSpectating();
	if (Character) Character->Destroy();
}

// 主动离开按票据与阶段分流：无票据测试角色只清理、未开局返还、已开局按退出结算。
void AExtractionGameMode::LeavePlayer(AController* Player)
{
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return;
	const auto* GS = GetGameState<AExtractionGameState>();
	// Direct-map PIE has no lobby ticket and must never create a settlement.
	if (!PS->ExtractionRaid.IsValid())
	{
		if (PS->Inventory) PS->Inventory->ClearSettled();
	}
	else if (GS && GS->Phase == EExtractionPhase::Waiting)
	{
		auto* Persistence = Store();
		if (!Persistence || !PS->Inventory || !Persistence->AbortRaid(PS->ExtractionRaid)) return;
		PS->Inventory->ClearSettled();
	}
	else if (!SettlePlayer(Player, EExtractionOutcome::Left)) return;
	PS->Life = EExtractionLife::Left;
	PS->ForceNetUpdate();
	if (auto* Character = Cast<ABrokenEdgeCharacter>(Player->GetPawn()); Character && !Character->IsDefeated())
	{
		Character->GetExtractionCombat()->CancelReload();
		Character->Defeated(true);
	}
	RefreshAliveCount();
}

// 断线时尝试记录 Disconnected，再执行离开清理；世界拆卸阶段跳过仓库访问。
void AExtractionGameMode::Logout(AController* Player)
{
	// PIE deinitializes GameInstance subsystems before the net driver destroys its controllers.
	if (!bEndingPlay && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		const auto* GS = GetGameState<AExtractionGameState>();
		if (auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
			PS && (PS->Life == EExtractionLife::Alive || PS->Life == EExtractionLife::Downed) && GS && GS->Phase ==
			EExtractionPhase::Active)
			SettlePlayer(Player, EExtractionOutcome::Disconnected);
		LeavePlayer(Player);
	}
	Super::Logout(Player);
}

// 先标记退出以阻断后续 Tick/回调，清理守卫并解绑会话委托，再交给父类销毁。
void AExtractionGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	RemoveGuards();
	if (auto* Instance = GetGameInstance())
		if (auto* Sessions = Instance->GetSubsystem<UMultiplayerSessionsSubsystem>())
			Sessions->MultiplayerOnEndSessionComplete.RemoveDynamic(this, &ThisClass::SessionEnded);
	Super::EndPlay(Reason);
}
