#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/World/ExtractionBreakableGlass.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionGlassTest, "BrokenEdge.Extraction.World.BreakableGlass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionGlassTest::RunTest(const FString&)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
	                                                .CreateNavigation(false).CreateAISystem(false).
	                                                ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
	                                    &Init);
	if (!TestNotNull(TEXT("Temporary world"), World)) return false;
	auto* State = World->SpawnActor<AExtractionGameState>();
	World->SetGameState(State);
	auto* Glass = World->SpawnActor<AExtractionBreakableGlass>();
	Glass->PaneId = FGuid::NewGuid();
	Glass->Pane->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube")));
	Glass->SetActorScale3D(FVector(.02, 1, 1));
	auto* Legacy = World->SpawnActor<AActor>();
	auto* LegacyPane = NewObject<UStaticMeshComponent>(Legacy, TEXT("Frame_StaticGlass"));
	Legacy->SetRootComponent(LegacyPane);
	LegacyPane->SetStaticMesh(Glass->Pane->GetStaticMesh());
	LegacyPane->SetCollisionProfileName(TEXT("BlockAll"));
	LegacyPane->RegisterComponent();
	LegacyPane->SetWorldTransform(Glass->Pane->GetComponentTransform());
	Glass->bReplacesLegacyWindow = true;
	Glass->DispatchBeginPlay();
	TestFalse(TEXT("Construction-script replacement hides only the matching old pane"), LegacyPane->IsVisible());
	TestEqual(TEXT("Old pane cannot leave an invisible collision after breaking"), LegacyPane->GetCollisionEnabled(),
	          ECollisionEnabled::NoCollision);
	FHitResult Hit;
	TestTrue(TEXT("Intact glass blocks a projectile trace"), World->LineTraceSingleByChannel(Hit,
		         FVector(-100, 0, 0), FVector(100, 0, 0), ECC_Visibility));
	FPointDamageEvent Damage;
	Damage.HitInfo = Hit;
	Damage.ShotDirection = FVector::ForwardVector;
	TestEqual(TEXT("Zero damage does not break glass"), Glass->TakeDamage(0, Damage, nullptr, nullptr), 0.f);
	TestFalse(TEXT("Zero damage leaves pane intact"), Glass->IsBroken());
	TestEqual(TEXT("Point damage breaks the pane"), Glass->TakeDamage(1, Damage, nullptr, nullptr), 1.f);
	TestTrue(TEXT("Broken state recorded in match"), State->BrokenGlass.Contains(Glass->PaneId));
	TestFalse(TEXT("Broken glass no longer blocks a trace"), World->LineTraceSingleByChannel(Hit,
		          FVector(-100, 0, 0), FVector(100, 0, 0), ECC_Visibility));
	TestFalse(TEXT("Broken pane is hidden"), Glass->Pane->IsVisible());
	TestTrue(TEXT("Repeated break is idempotent"), Glass->Break(FVector::ZeroVector, FVector::ForwardVector));
	TestEqual(TEXT("One authoritative broken id"), State->BrokenGlass.Num(), 1);
	auto* Streamed = World->SpawnActor<AExtractionBreakableGlass>();
	Streamed->PaneId = Glass->PaneId;
	Streamed->DispatchBeginPlay();
	TestTrue(TEXT("Streamed reload restores match damage"), Streamed->IsBroken());
	State->BrokenGlass.Reset();
	auto* NextMatch = World->SpawnActor<AExtractionBreakableGlass>();
	NextMatch->PaneId = Glass->PaneId;
	NextMatch->DispatchBeginPlay();
	TestFalse(TEXT("A fresh match starts intact"), NextMatch->IsBroken());
	auto* Invalid = World->SpawnActor<AExtractionBreakableGlass>();
	TestFalse(
		TEXT("Unidentified panes cannot corrupt match state"), Invalid->Break(FVector::ZeroVector, FVector::UpVector));
	World->DestroyWorld(false);
	return true;
}
#endif
