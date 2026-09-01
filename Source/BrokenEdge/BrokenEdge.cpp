// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrokenEdge.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	const TCHAR* CollisionEnabledToString(const ECollisionEnabled::Type CollisionEnabled)
	{
		switch (CollisionEnabled)
		{
		case ECollisionEnabled::NoCollision:
			return TEXT("NoCollision");
		case ECollisionEnabled::QueryOnly:
			return TEXT("QueryOnly");
		case ECollisionEnabled::PhysicsOnly:
			return TEXT("PhysicsOnly");
		case ECollisionEnabled::QueryAndPhysics:
			return TEXT("QueryAndPhysics");
		case ECollisionEnabled::ProbeOnly:
			return TEXT("ProbeOnly");
		case ECollisionEnabled::QueryAndProbe:
			return TEXT("QueryAndProbe");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* CollisionResponseToString(const ECollisionResponse CollisionResponse)
	{
		switch (CollisionResponse)
		{
		case ECR_Ignore:
			return TEXT("Ignore");
		case ECR_Overlap:
			return TEXT("Overlap");
		case ECR_Block:
			return TEXT("Block");
		default:
			return TEXT("Unknown");
		}
	}

	FString CollisionChannelToString(const UCollisionProfile* CollisionProfile, const int32 ChannelIndex)
	{
		if (CollisionProfile)
		{
			const FName ChannelName = CollisionProfile->ReturnChannelNameFromContainerIndex(ChannelIndex);
			if (!ChannelName.IsNone())
			{
				return ChannelName.ToString();
			}
		}

		return FString::Printf(TEXT("Channel%d"), ChannelIndex);
	}
}

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, BrokenEdge, "BrokenEdge" );

FString PrintNetMode()
{
	if (GWorld == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NetMode] World=null | Mode=Unavailable"));
		return FString("Unavailable");
	}

	const TCHAR* NetModeText = TEXT("Unknown");
	switch (GWorld->GetNetMode())
	{
	case NM_Standalone:
		NetModeText = TEXT("Standalone");
		break;
	case NM_DedicatedServer:
		NetModeText = TEXT("DedicatedServer");
		break;
	case NM_ListenServer:
		NetModeText = TEXT("ListenServer");
		break;
	case NM_Client:
		NetModeText = TEXT("Client");
		break;
	default:
		break;
	}

	return NetModeText;
	// UE_LOG(LogTemp, Warning, TEXT("[NetMode] World=%s | Mode=%s"), *GetNameSafe(GWorld), NetModeText);
}

void PrintCollisionPresetSettings(const UPrimitiveComponent* Component)
{
	if (!IsValid(Component))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CollisionPreset] Component is null or invalid."));
		return;
	}

	const UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
	const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
	const FCollisionResponseContainer& Responses = Component->GetCollisionResponseToChannels();

	FString Settings = FString::Printf(
		TEXT("[CollisionPreset]\n")
		TEXT("Component: %s\n")
		TEXT("Owner: %s\n")
		TEXT("ProfileName: %s\n")
		TEXT("CollisionEnabled: %s\n")
		TEXT("ObjectType: %s\n")
		TEXT("GenerateOverlapEvents: %s\n")
		TEXT("SimulationGeneratesHitEvents: %s\n")
		TEXT("Responses:"),
		*Component->GetPathName(),
		*GetNameSafe(Component->GetOwner()),
		*Component->GetCollisionProfileName().ToString(),
		CollisionEnabledToString(Component->GetCollisionEnabled()),
		*CollisionChannelToString(CollisionProfile, static_cast<int32>(ObjectType)),
		Component->GetGenerateOverlapEvents() ? TEXT("true") : TEXT("false"),
		Component->BodyInstance.bNotifyRigidBodyCollision ? TEXT("true") : TEXT("false"));

	for (int32 ChannelIndex = 0; ChannelIndex < UE_ARRAY_COUNT(Responses.EnumArray); ++ChannelIndex)
	{
		const FName ChannelName = CollisionProfile
			? CollisionProfile->ReturnChannelNameFromContainerIndex(ChannelIndex)
			: NAME_None;

		// 未注册的隐藏通道不会出现在编辑器的 Collision Responses 面板中。
		if (ChannelName.IsNone())
		{
			continue;
		}

		const ECollisionResponse Response = Responses.GetResponse(
			static_cast<ECollisionChannel>(ChannelIndex));
		Settings += FString::Printf(
			TEXT("\n  %s: %s"),
			*ChannelName.ToString(),
			CollisionResponseToString(Response));
	}

	UE_LOG(LogTemp, Log, TEXT("Oreki %s"), *Settings);
}
