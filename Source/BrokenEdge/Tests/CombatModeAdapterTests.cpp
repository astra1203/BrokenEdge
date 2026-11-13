#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatModeAdapterTest, "BrokenEdge.Extraction.CombatMode.DispatchAndOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCombatModeAdapterTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("World"), W)) return false;
    auto* C = W->SpawnActor<ABrokenEdgeCharacter>();
    // Unstarted worlds do not run this lifecycle hook; initialize the real component back-references.
    C->PostInitializeComponents();
    auto* Combat = C->GetCombat().Get();
    Combat->FireButtonPressed(true);
    TestTrue(TEXT("Legacy mode owns its held input even without a weapon"), Combat->IsFireInputHeld());
    Combat->SetReloadPrediction(true);
    Combat->FinishReloading();
    TestFalse(TEXT("Legacy animation completion clears prediction"), Combat->IsReloadPredicted());

    auto* GS = W->SpawnActor<AExtractionGameState>(); GS->Phase = EExtractionPhase::Active;
    W->SetGameState(GS);
    TestFalse(TEXT("Extraction never inherits legacy held input"), Combat->IsFireInputHeld());
    Combat->FireButtonPressed(true);
    TestFalse(TEXT("Extraction rejects held fire without a usable weapon"), Combat->IsFireInputHeld());
    Combat->SetReloadPrediction(true);
    Combat->PublishExtractionCombatState(ECombatState::ECS_Reloading);
    Combat->FinishReloading();
    TestTrue(TEXT("Extraction ignores animation reload completion"), Combat->IsReloadPredicted());
    TestTrue(TEXT("Animation notify cannot finish authoritative reload"), C->GetCombatState() == ECombatState::ECS_Reloading);
    C->GetExtractionCombat()->CancelReload();
    TestFalse(TEXT("Extraction cancellation clears prediction"), Combat->IsReloadPredicted());
    TestTrue(TEXT("Extraction cancellation restores idle"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    Combat->SyncReserveFromInventory(37);
    TestEqual(TEXT("Reserve is an inventory snapshot"), Combat->GetCarriedAmmo(), 37);
    Combat->CancelFireInput(); Combat->CancelFireInput();
    W->SetGameState(nullptr);
    TestFalse(TEXT("Extraction cancellation clears stale legacy input without recursion"), Combat->IsFireInputHeld());
    Combat->FireButtonPressed(true); Combat->CancelFireInput();
    TestFalse(TEXT("Legacy cancellation remains independent"), Combat->IsFireInputHeld());
    auto* Weapon = W->SpawnActor<AWeapon>();
    auto* Capacity = FindFProperty<FIntProperty>(AWeapon::StaticClass(), TEXT("MagCapacity"));
    if (!TestNotNull(TEXT("Legacy magazine fixture property"), Capacity)) { W->DestroyWorld(false); return false; }
    Capacity->SetPropertyValue_InContainer(Weapon, 30);
    Weapon->SetExtractionAmmo(0);
    Combat->EquipWeapon(Weapon);
    Combat->SyncReserveFromInventory(40);
    Combat->Reload();
    TestTrue(TEXT("Legacy empty magazine starts reload"), C->GetCombatState() == ECombatState::ECS_Reloading);
    Combat->FinishReloading();
    TestEqual(TEXT("Legacy notify fills magazine once"), Weapon->GetAmmo(), Weapon->GetMagCapacity());
    TestEqual(TEXT("Legacy reload conserves reserve"), Combat->GetCarriedAmmo(), 40 - Weapon->GetMagCapacity());
    const int32 Reserve = Combat->GetCarriedAmmo();
    Combat->FinishReloading();
    TestEqual(TEXT("Repeated legacy completion cannot transfer more ammo"), Combat->GetCarriedAmmo(), Reserve);
    Combat->Drop();
    TestNull(TEXT("Legacy drop clears equipped reference"), C->GetEquippedWeapon());
    W->DestroyWorld(false);
    return true;
}
#endif
