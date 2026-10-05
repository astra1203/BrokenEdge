#include "ExtractionAccountComponent.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "ExtractionState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

UExtractionAccountComponent::UExtractionAccountComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .25f;
}

void UExtractionAccountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Profile, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Bound, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Locked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Message, COND_OwnerOnly);
}

UExtractionPersistence* UExtractionAccountComponent::Store() const
{
	return GetWorld()->GetGameInstance()->GetSubsystem<UExtractionPersistence>();
}

void UExtractionAccountComponent::Refresh()
{
	if (!GetOwner()->HasAuthority()) return;
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* PS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	if (!PS) return;
	if (const auto* V = Store()->View(PS->ExtractionProfile)) Profile = *V;
	const auto* Lobby = GetWorld()->GetGameState<ALobbyGameState>();
	Locked = PS->IsLobbyReady() || !Lobby || Lobby->RoomMatchType != TEXT("Extraction")
		|| Lobby->bTravelling || Lobby->bModeUpdating || !Store()->IsAvailable();
	if (!Store()->IsAvailable()) Message = Store()->Error;
	GetOwner()->ForceNetUpdate();
}

void UExtractionAccountComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Type, Tick);
	if (!GetOwner()->HasAuthority()) return;
	const auto* Lobby = GetWorld()->GetGameState<ALobbyGameState>();
	if ((!Lobby || Lobby->RoomMatchType != TEXT("Extraction")) && !GetWorld()->GetGameState<AExtractionGameState>())
		return;
	if (!Store()->EnsureLoaded())
	{
		Message = Store()->Error;
		Locked = true;
		return;
	}
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* PS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	if (!PS) return;
	if (PS->ExtractionProfile.IsValid())
	{
		Bound = true;
		Refresh();
	}
	else if (!Requested)
	{
		Requested = true;
		ClientBind(Store()->StoreId());
	}
}

FString UExtractionAccountComponent::CredentialPath(FGuid Id) const
{
	FString Label = TEXT("Default");
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionProfile="), Label);
	for (TCHAR C : Label) if (!FChar::IsAlnum(C) && C != '-' && C != '_')
	{
		Label = TEXT("Default");
		break;
	}
#if WITH_EDITOR
	if (GetWorld()->WorldType == EWorldType::PIE) Label += FString::Printf(
		TEXT("_PIE%d"), GetWorld()->GetPackage()->GetPIEInstanceID());
#endif
	return FPaths::ProjectSavedDir() / TEXT("Extraction/Clients") / Label / (Id.ToString(EGuidFormats::Digits) +
		TEXT(".credential"));
}

void UExtractionAccountComponent::ClientBind_Implementation(FGuid Id)
{
	FString Text;
	const FString Path = CredentialPath(Id);
	if (!IFileManager::Get().FileExists(*Path))
	{
		ServerBind({}, {});
		return;
	}
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Message = TEXT("无法读取本地档案凭据");
		return;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	FGuid ProfileId;
	if (Lines.Num() != 2 || !FGuid::Parse(Lines[0], ProfileId) || Lines[1].Len() != 64)
	{
		Message = TEXT("本地档案凭据损坏，请恢复凭据文件");
		return;
	}
	ServerBind(ProfileId, Lines[1]);
}

void UExtractionAccountComponent::ServerBind_Implementation(FGuid Id, const FString& Credential)
{
	if (Bound || Credential.Len() > 64) return;
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* PS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	const auto* Lobby = GetWorld()->GetGameState<ALobbyGameState>();
	const auto* Game = GetWorld()->GetGameState<AExtractionGameState>();
	if (!PS || (!Lobby && (!Game || Game->Phase != EExtractionPhase::Waiting))) return;
	FString Issued;
	FGuid Actual;
	if (!Store()->OpenProfile(Id, Credential, Actual, Issued))
	{
		Message = Store()->Error;
		return;
	}
	for (APlayerState* Other : GetWorld()->GetGameState()->PlayerArray)
		if (auto* OP = Cast<ABrokenEdgePlayerState>(Other); OP && OP != PS && OP->ExtractionProfile == Actual && !OP->
			IsInactive())
		{
			Message = TEXT("此档案已在线，请使用独立测试档案");
			return;
		}
	Profile = *Store()->View(Actual);
	if (Issued.IsEmpty())
	{
		PS->ExtractionProfile = Actual;
		Bound = true;
		Message.Reset();
		Refresh();
	}
	else ClientCredentials(Profile.Store, Actual, Issued);
}

void UExtractionAccountComponent::ClientCredentials_Implementation(FGuid StoreId, FGuid ProfileId,
                                                                   const FString& Credential)
{
	const FString Path = CredentialPath(StoreId);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const bool Saved = FFileHelper::SaveStringToFile(ProfileId.ToString() + TEXT("\n") + Credential + TEXT("\n"), *Path,
	                                                 FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ServerCredentialSaved(Saved);
}

void UExtractionAccountComponent::ServerCredentialSaved_Implementation(bool Success)
{
	if (Bound || !Profile.Profile.IsValid()) return;
	if (!Success)
	{
		Message = TEXT("无法保存本地凭据，暂不能出战");
		return;
	}
	// Another connection can bind the newly saved credential before this acknowledgement arrives.
	for (APlayerState* Other : GetWorld()->GetGameState()->PlayerArray)
		if (auto* OP = Cast<ABrokenEdgePlayerState>(Other); OP && OP->GetOwner() != GetOwner()
			&& OP->ExtractionProfile == Profile.Profile && !OP->IsInactive())
		{
			Message = TEXT("此档案已在线，请使用独立测试档案");
			return;
		}
	if (auto* PC = Cast<APlayerController>(GetOwner()))
		if (auto* PS = PC->GetPlayerState<ABrokenEdgePlayerState>())
		{
			PS->ExtractionProfile = Profile.Profile;
			Bound = true;
			Message.Reset();
			Refresh();
		}
}

void UExtractionAccountComponent::RequestMove(FExtractionStorageRequest R)
{
	R.Operation = FGuid::NewGuid();
	R.Revision = Profile.Revision;
	ServerMove(R);
}

void UExtractionAccountComponent::RequestEconomy(FExtractionEconomyRequest R)
{
	R.Operation = FGuid::NewGuid();
	R.Revision = Profile.Revision;
	ServerEconomy(R);
}

void UExtractionAccountComponent::ServerEconomy_Implementation(FExtractionEconomyRequest R)
{
	Refresh();
	if (!Bound || Locked)
	{
		Message = TEXT("仅可在未准备的航天行动大厅交易和办理任务");
		return;
	}
	Message = Store()->Economy(Profile.Profile, R) ? TEXT("操作已保存") : Store()->Error;
	Refresh();
}

void UExtractionAccountComponent::ServerMove_Implementation(FExtractionStorageRequest R)
{
	Refresh();
	if (!Bound || Locked)
	{
		Message = TEXT("取消准备后才能整理仓库");
		return;
	}
	Message = Store()->Move(Profile.Profile, R) ? FString() : Store()->Error;
	Refresh();
}
