#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Shotgun.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyCombatLifecycleTest, "BrokenEdge.Extraction.CombatMode.LegacyLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FLegacyCombatLifecycleTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("World"), W)) return false;
    auto* C = W->SpawnActor<ABrokenEdgeCharacter>(); C->PostInitializeComponents();
    auto* Combat = C->GetCombat().Get();
    Combat->BeginPlay(); // Initialize the legacy per-weapon reserve map.
    auto* Gun = W->SpawnActor<AShotgun>(); Gun->FireType = EFireType::EFT_Shotgun;
    auto* Capacity = FindFProperty<FIntProperty>(AWeapon::StaticClass(), TEXT("MagCapacity"));
    auto* Type = FindFProperty<FEnumProperty>(AWeapon::StaticClass(), TEXT("WeaponType"));
    if (!TestNotNull(TEXT("Magazine fixture"), Capacity) || !TestNotNull(TEXT("Weapon type fixture"), Type))
    { W->DestroyWorld(false); return false; }
    Capacity->SetPropertyValue_InContainer(Gun, 8);
    Type->GetUnderlyingProperty()->SetIntPropertyValue(Type->ContainerPtrToValuePtr<void>(Gun), int64(EWeaponType::EWT_Shotgun));
    Combat->EquipWeapon(Gun);
    auto ReloadFixture = [&](int32 Ammo, int32 Reserve)
    {
        Combat->FinishReloading();
        Gun->SetExtractionAmmo(8); // Pickup must not auto-start a reload while preparing the fixture.
        Combat->PickupAmmo(EWeaponType::EWT_Shotgun, Reserve - Combat->GetCarriedAmmo());
        Gun->SetExtractionAmmo(Ammo); Combat->Reload();
    };
    ReloadFixture(0, 3);
    Combat->ShotgunReloadOnce();
    TestEqual(TEXT("One shell notify adds one shell"), Gun->GetAmmo(), 1);
    TestEqual(TEXT("One shell consumes one reserve"), Combat->GetCarriedAmmo(), 2);
    Combat->FinishReloading();
    TestEqual(TEXT("Shotgun completion does not bulk fill the magazine"), Gun->GetAmmo(), 1);
    TestEqual(TEXT("Shotgun completion preserves unused reserve"), Combat->GetCarriedAmmo(), 2);
    const int32 FinishedAmmo = Gun->GetAmmo(), FinishedReserve = Combat->GetCarriedAmmo();
    Combat->ShotgunReloadOnce();
    TestEqual(TEXT("Late shell notify after completion does not add ammo"), Gun->GetAmmo(), FinishedAmmo);
    TestEqual(TEXT("Late shell notify after completion does not consume reserve"), Combat->GetCarriedAmmo(), FinishedReserve);
    ReloadFixture(7, 2);
    Combat->ShotgunReloadOnce(); Combat->ShotgunReloadOnce();
    TestEqual(TEXT("Full magazine never consumes an extra shell"), Combat->GetCarriedAmmo(), 1);
    ReloadFixture(7, 0); Combat->ShotgunReloadOnce();
    TestEqual(TEXT("No reserve cannot create a shell"), Gun->GetAmmo(), 7);
    TestEqual(TEXT("Reserve cannot become negative"), Combat->GetCarriedAmmo(), 0);
    ReloadFixture(1, 3); Combat->ShotgunReloadOnce();
    TestTrue(TEXT("Loaded shotgun can interrupt per-shell reload"), Combat->CanFire());
    Combat->ShotgunLocalFire({});
    TestEqual(TEXT("Interrupting shot spends exactly one shell"), Gun->GetAmmo(), 1);
    TestTrue(TEXT("Interrupting shot leaves idle"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    Combat->ShotgunReloadOnce();
    TestEqual(TEXT("Interrupted reload cannot add a late shell"), Gun->GetAmmo(), 1);

    ReloadFixture(1, 3); Combat->Drop();
    TestTrue(TEXT("Drop cancels reload state"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    TestFalse(TEXT("Drop clears local reload prediction"), Combat->IsReloadPredicted());
    Combat->FinishReloading(); Combat->EquipWeapon(Gun);
    auto* Secondary = W->SpawnActor<AWeapon>();
    Capacity->SetPropertyValue_InContainer(Secondary, 30); Secondary->SetExtractionAmmo(10);
    Combat->EquipWeapon(Secondary);
    Combat->SwapWeapons(); Combat->FinishSwapAttachWeapons();
    TestEqual(TEXT("Swap first notify equips secondary"), C->GetEquippedWeapon(), Secondary);
    Combat->FinishSwapAttachWeapons();
    TestEqual(TEXT("Repeated swap notify cannot swap back"), C->GetEquippedWeapon(), Secondary);
    Combat->FinishSwap(); Combat->FinishSwapAttachWeapons();
    TestEqual(TEXT("Late swap notify after completion cannot swap again"), C->GetEquippedWeapon(), Secondary);
    C->bFinishedSwapping = false; // Simulate predicted presentation outliving the authority state.
    Combat->ProcessEvent(Combat->FindFunctionChecked(TEXT("OnRep_CombatState")), nullptr);
    TestTrue(TEXT("Replicated idle clears predicted swap presentation"), C->bFinishedSwapping);
    Combat->PickupAmmo(Secondary->GetWeaponType(), 40 - Combat->GetCarriedAmmo()); Combat->Reload();
    Combat->FinishSwap();
    TestTrue(TEXT("Old swap completion cannot unlock a new reload"), C->GetCombatState() == ECombatState::ECS_Reloading);
    Combat->FireButtonPressed(true);
    TestTrue(TEXT("Legacy fire stays queued during reload"), Combat->IsFireInputHeld());
    // A real defeat entry must invalidate pending actions before dropping weapons.
    C->Defeated(false);
    TestTrue(TEXT("Defeat cancels pending action state"), C->GetCombatState() == ECombatState::ECS_Unoccuiped);
    TestFalse(TEXT("Defeat cancels reload prediction"), Combat->IsReloadPredicted());
    TestFalse(TEXT("Defeat clears held fire"), Combat->IsFireInputHeld());
    TestNull(TEXT("Defeat drops the equipped weapon"), C->GetEquippedWeapon());
    const int32 DroppedAmmo = Secondary->GetAmmo();
    Combat->FinishReloading(); Combat->ShotgunReloadOnce(); Combat->FinishSwapAttachWeapons(); Combat->FinishSwap();
    TestEqual(TEXT("Late notifies after defeat cannot refill dropped weapon"), Secondary->GetAmmo(), DroppedAmmo);
    Combat->EndPlay(EEndPlayReason::Destroyed);
    W->DestroyWorld(false);
    return true;
}
#endif
