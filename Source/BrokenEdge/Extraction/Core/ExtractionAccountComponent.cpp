#include "BrokenEdge/Extraction/Core/ExtractionAccountComponent.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
// 控制器上的账号桥接组件：客户端保存凭据，服务器绑定档案并复制本人可见的仓库快照。
// RPC 只传操作请求与身份信息，仓库结果始终由服务器 Persistence 计算。

// 默认启用组件复制，每 0.25 秒在服务器刷新绑定与锁定状态。
UExtractionAccountComponent::UExtractionAccountComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .25f;
}

// Profile、Bound、Locked 和 Message 只发给拥有该控制器的客户端，其他玩家不应看到仓库快照。
void UExtractionAccountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Profile, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Bound, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Locked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAccountComponent, Message, COND_OwnerOnly);
}

// 从当前 GameInstance 获取仓库子系统；本组件的服务器操作通过它访问长期状态。
UExtractionPersistence* UExtractionAccountComponent::Store() const
{
	return GetWorld()->GetGameInstance()->GetSubsystem<UExtractionPersistence>();
}

// 服务器复制档案最新快照，并根据准备、旅行、模式切换和存储状态更新锁定标志。
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

// 服务器按需加载仓库；有档案则刷新，没有档案则只发起一次客户端凭据绑定。
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

// 按服务器仓库 ID 和本地档案标签隔离凭据；PIE 追加实例号，避免双客户端共用身份。
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

// 所属客户端读取本地凭据再请求服务器绑定；文件缺失请求新建，文件损坏则提示错误。
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

// 服务器校验请求时机、凭据和在线身份冲突；新凭据需要客户端确认保存后才完成绑定。
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

// 所属客户端保存新凭据，并将保存结果回传；保存失败时不能放行出战。
void UExtractionAccountComponent::ClientCredentials_Implementation(FGuid StoreId, FGuid ProfileId,
                                                                   const FString& Credential)
{
	const FString Path = CredentialPath(StoreId);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const bool Saved = FFileHelper::SaveStringToFile(ProfileId.ToString() + TEXT("\n") + Credential + TEXT("\n"), *Path,
	                                                 FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ServerCredentialSaved(Saved);
}

// 处理凭据落盘确认，再次检查在线冲突，防止确认往返期间另一连接占用同一档案。
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

// 为本次仓库操作生成唯一 ID 并携带当前快照版本，随后调用服务器 RPC。
void UExtractionAccountComponent::RequestMove(FExtractionStorageRequest R)
{
	R.Operation = FGuid::NewGuid();
	R.Revision = Profile.Revision;
	ServerMove(R);
}

// 为交易或任务操作生成唯一 ID，携带版本供服务器检测过期请求。
void UExtractionAccountComponent::RequestEconomy(FExtractionEconomyRequest R)
{
	R.Operation = FGuid::NewGuid();
	R.Revision = Profile.Revision;
	ServerEconomy(R);
}

// 服务器重新计算锁定状态，仅在已绑定且未锁定时执行交易并回传持久化结果。
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

// 服务器校验大厅编辑权限，再请求仓库整理；不直接相信客户端界面是否允许点击。
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
