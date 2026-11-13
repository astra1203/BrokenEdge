#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/HitScanWeapon.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

struct FExtractionCombatTestAccess
{
    static void SetState(UExtractionCombatComponent* Combat, ECombatState State) { Combat->SetActionState(State); }
};

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
    FExtractionCombatTestAccess::SetState(C->GetExtractionCombat(), ECombatState::ECS_Reloading);
    W->SetGameState(nullptr);
    TestTrue(TEXT("Extraction reload does not write legacy state"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    W->SetGameState(GS);
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
    W->SetGameState(GS);
    TestTrue(TEXT("Legacy reload does not write extraction state"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    W->SetGameState(nullptr);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatModeCadenceTest, "BrokenEdge.Extraction.CombatMode.PredictionAndCadenceIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCombatModeCadenceTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("World"), W)) return false;
    auto* C = W->SpawnActor<ABrokenEdgeCharacter>(); C->PostInitializeComponents();
    auto* Combat = C->GetCombat().Get();
    auto* Extraction = C->GetExtractionCombat();
    // This temporary world does not dispatch play; pair the component lifecycle explicitly.
    Extraction->BeginPlay();
    auto* Weapon = W->SpawnActor<AHitScanWeapon>();
    auto* Capacity = FindFProperty<FIntProperty>(AWeapon::StaticClass(), TEXT("MagCapacity"));
    if (!TestNotNull(TEXT("Magazine fixture property"), Capacity)) { W->DestroyWorld(false); return false; }
    Capacity->SetPropertyValue_InContainer(Weapon, 30); Weapon->SetExtractionAmmo(5);
    Combat->EquipWeapon(Weapon);
    Combat->StartFireTimer();
    TestFalse(TEXT("Legacy cadence starts locked"), Combat->CanFire());
    Combat->SetReloadPrediction(true);
    auto* GS = W->SpawnActor<AExtractionGameState>(); GS->Phase = EExtractionPhase::Active;
    auto SetExtractionMode = [&](bool Enabled)
    {
        // Guards enable extraction even without a GameState; change both fixture inputs.
        C->bExtractionGuard = Enabled; W->SetGameState(Enabled ? GS : nullptr);
    };
    SetExtractionMode(true);
    TestFalse(TEXT("Extraction does not inherit legacy reload prediction"), Combat->IsReloadPredicted());
    TestTrue(TEXT("Extraction does not inherit legacy cooldown"), Combat->CanFire());
    Combat->StartFireTimer();
    TestFalse(TEXT("Extraction starts its own cooldown"), Extraction->IsFireCooldownReady());
    Combat->FireTimerFinished();
    TestFalse(TEXT("Legacy callback cannot release extraction cooldown"), Combat->CanFire());
    Combat->SetReloadPrediction(true);
    SetExtractionMode(false);
    TestTrue(TEXT("Legacy prediction survives extraction changes"), Combat->IsReloadPredicted());
    Combat->SetReloadPrediction(false);
    TestTrue(TEXT("Legacy gate is independent of extraction cooldown and reload"), Combat->CanFire());
    SetExtractionMode(true);
    TestTrue(TEXT("Clearing legacy prediction leaves extraction prediction intact"), Combat->IsReloadPredicted());
    Extraction->CancelReload();
    TestFalse(TEXT("Extraction cancellation clears its own prediction"), Combat->IsReloadPredicted());
    auto AdvanceTimers = [&]
    {
        TGuardValue<uint64> RestoreFrame(GFrameCounter, GFrameCounter);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            ++GFrameCounter; W->TimeSeconds += 1; W->GetTimerManager().Tick(1.f);
        }
    };
    AdvanceTimers();
    TestTrue(TEXT("Extraction cadence unlocks after its timer"), Combat->CanFire());
    TestEqual(TEXT("Released input does not fire on cooldown completion"), Weapon->GetAmmo(), 5);

    Combat->StartFireTimer();
    SetExtractionMode(false); Combat->Drop();
    auto* Replacement = W->SpawnActor<AHitScanWeapon>();
    Capacity->SetPropertyValue_InContainer(Replacement, 30); Replacement->SetExtractionAmmo(0);
    Combat->EquipWeapon(Replacement); Combat->SyncReserveFromInventory(40); SetExtractionMode(true);
    AdvanceTimers();
    TestTrue(TEXT("Replacement does not inherit expired weapon's auto-reload"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    TestFalse(TEXT("Replacement has no stale reload prediction"), Combat->IsReloadPredicted());
    TestTrue(TEXT("Expired weapon releases local cadence"), Extraction->IsFireCooldownReady());
    Combat->StartFireTimer(); Combat->SetReloadPrediction(true);
    Extraction->EndPlay(EEndPlayReason::Destroyed);
    AdvanceTimers();
    TestFalse(TEXT("EndPlay clears prediction"), Extraction->IsReloadPredicted());
    TestTrue(TEXT("EndPlay timer cannot auto-reload"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    W->DestroyWorld(false);
    return true;
}
#endif
