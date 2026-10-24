#include "LobbyShowcase.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"
#include "BrokenEdge/Weapon/Weapon.h"

ALobbyShowcase::ALobbyShowcase()
{
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(
		TEXT("/Game/Characters/Stinger/Meshes/Gameplay/Stinger_3P_Textured/SkeletalMeshes/Stinger_3P_Textured"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Animation(
		TEXT("/Game/Characters/Stinger/Animations/Corrected/MappedV4/ac5ff8b5/M_Roy_016_Show_idle"));
	Idle = Animation.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> RifleAnimation(
		TEXT("/Game/Characters/Stinger/Animations/ThirdPerson/Idle_Rifle_Hip_Retargeted_Stinger"));
	RifleIdle = RifleAnimation.Object;
	for (int32 I = 0; I < 3; ++I)
	{
		auto* Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("Member%d"), I));
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetSkeletalMeshAsset(Body.Object);
		Mesh->SetRelativeLocation(I == 0 ? FVector::ZeroVector : FVector(-55, I == 1 ? -105 : 105, 0));
		Mesh->SetRelativeRotation(FRotator(0, -90, 0));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibleInSceneCaptureOnly(true);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Members.Add(Mesh);
	}
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortraitCapture"));
	Equipment = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeldEquipment"));
	Equipment->SetupAttachment(Members[0],TEXT("RightHandSocket"));
	Equipment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Equipment->SetVisibleInSceneCaptureOnly(true);
	Capture->SetupAttachment(RootComponent);
	Capture->SetRelativeLocation(FVector(600, 0, 110));
	Capture->SetRelativeRotation(FRotator(-3, 180, 0));
	Capture->FOVAngle = 38;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = 8;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Studio(
		TEXT("/Game/Extraction/UI/M_LobbyStudio.M_LobbyStudio"));
	for (int32 I = 0; I < 2; ++I)
	{
		auto* Surface = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("StudioSurface%d"), I));
		Surface->SetupAttachment(RootComponent);
		Surface->SetStaticMesh(Cube.Object);
		if (Studio.Succeeded()) Surface->SetMaterial(0, Studio.Object);
		Surface->SetRelativeLocation(I == 0 ? FVector(-100, 0, -6) : FVector(-250, 0, 160));
		Surface->SetRelativeScale3D(I == 0 ? FVector(9, 9, .1f) : FVector(.1f, 9, 4));
		Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Surface->SetVisibleInSceneCaptureOnly(true);
	}
	for (int32 I = 0; I < 2; ++I)
	{
		auto* Light = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("StudioLight%d"), I));
		Light->SetupAttachment(RootComponent);
		Light->SetRelativeLocation(FVector(200, I == 0 ? -180 : 180, 220));
		Light->SetIntensity(35000);
		Light->SetAttenuationRadius(800);
		Light->SetLightColor(I == 0 ? FLinearColor(.75f, .88f, 1) : FLinearColor(.35f, 1, .8f));
		Light->SetCastShadows(I == 0);
	}
}

void ALobbyShowcase::BeginPlay()
{
	Super::BeginPlay();
	Portrait = NewObject<UTextureRenderTarget2D>(this);
	Portrait->ClearColor = FLinearColor(.012f, .02f, .022f);
	Portrait->InitAutoFormat(1600, 880);
	Capture->TextureTarget = Portrait;
	for (const auto& Mesh : Members)
	{
		Capture->ShowOnlyComponent(Mesh);
		Mesh->PlayAnimation(Idle, true);
	}
	Capture->ShowOnlyActorComponents(this);
	SetSquadSize(1);
}

void ALobbyShowcase::SetSquadSize(int32 Count)
{
	for (int32 I = 0; I < Members.Num(); ++I) Members[I]->SetVisibility(I < FMath::Clamp(Count, 1, 3));
}

void ALobbyShowcase::SetHeldWeapon(FName Definition)
{
	if (HeldDefinition == Definition) return;
	HeldDefinition = Definition;
	const auto* Item = UExtractionItemDefinition::Get(Definition);
	const auto* Weapon = Item ? Item->WeaponClass.GetDefaultObject() : nullptr;
	Equipment->SetSkeletalMeshAsset(Weapon ? Weapon->GetWeaponMesh()->GetSkeletalMeshAsset() : nullptr);
	Equipment->SetVisibility(Weapon != nullptr);
	Members[0]->PlayAnimation(Weapon && RifleIdle ? RifleIdle : Idle, true);
}
