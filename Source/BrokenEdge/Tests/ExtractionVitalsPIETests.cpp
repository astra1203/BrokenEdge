#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Health/ExtractionHealth.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"

class FExtractionVitalsPIESequence : public IAutomationLatentCommand
{
public:
    explicit FExtractionVitalsPIESequence(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 90) { Test->AddError(TEXT("Vitals PIE timed out")); return true; }
        auto* W = GEditor ? GEditor->PlayWorld.Get() : nullptr;
        auto* PC = W ? Cast<AExtractionPlayerController>(W->GetFirstPlayerController()) : nullptr;
        auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
        if (!C || !PC->Interaction() || !PC->Interaction()->CanAct() || !C->GetEquippedWeapon()) return false;
        auto* Health = C->GetExtractionHealth();
        auto* PS = C->GetPlayerState<AExtractionPlayerState>();
        auto* Inventory = PC->Interaction()->Inventory();
        const int32 Squad = PS->ExtractionSquad;
        int32 Events = 0;
        const auto Handle = C->OnVitalsChanged.AddLambda([&](const FExtractionVitalsChange&) { ++Events; });
        auto Reset = [&]
        {
            Health->Revived(); Health->ClearStim();
            C->ApplyVitalsChange({C->GetMaxHealth() - C->GetHealth(), -C->GetShield(), EExtractionVitalsReason::Restore});
            Events = 0; C->TestDamageCalculations = 0;
        };
        Reset();
        Health->Explosion(20, nullptr, nullptr);
        Test->TestEqual(TEXT("Explosion calculates mode damage once"), C->TestDamageCalculations, 1);
        Test->TestEqual(TEXT("Explosion applies once"), C->GetHealth(), 80.f);
        Test->TestEqual(TEXT("Explosion notifies once"), Events, 1);

        auto Armor = ExtractionItems::Make(TEXT("Vest2")); Armor.Slot = 3; Armor.Durability = 3;
        Test->TestTrue(TEXT("Seed breaking armor with transaction"), Inventory->Mutate(
            [&](FExtractionContainer& Candidate, FString&)
            {
                Candidate.Items.RemoveAll([](const FExtractionItem& Item) { return Item.Slot == 3; });
                Candidate.Items.Add(Armor); return true;
            }, INDEX_NONE, false).Ok());
        Reset(); PS->ExtractionSquad = 123;
        Health->Bullet(20, TEXT("spine_01"), TEXT("Ammo"), PC, nullptr);
        Test->TestEqual(TEXT("Friendly damage calculates once"), C->TestDamageCalculations, 1);
        Test->TestEqual(TEXT("Friendly damage cannot hurt health"), C->GetHealth(), 100.f);
        Test->TestEqual(TEXT("Friendly damage does not notify"), Events, 0);
        const auto* Equipped = Inventory->GetContents().Find(Armor.Id);
        if (Test->TestNotNull(TEXT("Armor remains equipped"), Equipped))
            Test->TestEqual(TEXT("Friendly hit cannot wear armor"), Equipped->Durability, 3.f);
        PS->ExtractionSquad = Squad;
        Reset();
        Health->Bullet(20, TEXT("spine_01"), TEXT("Ammo"), nullptr, nullptr);
        Test->TestEqual(TEXT("Bullet calculates mode damage once"), C->TestDamageCalculations, 1);
        Test->TestEqual(TEXT("Breaking armor absorbs only remaining durability"), C->GetHealth(), 83.f);
        Test->TestEqual(TEXT("Bullet notifies once"), Events, 1);
        Equipped = Inventory->GetContents().Find(Armor.Id);
        if (Test->TestNotNull(TEXT("Broken armor retained"), Equipped))
            Test->TestEqual(TEXT("Broken armor has zero durability"), Equipped->Durability, 0.f);

        Reset();
        C->ApplyVitalsChange({-70, 0, EExtractionVitalsReason::Restore}); Events = 0;
        const auto* Medkit = UExtractionItemDefinition::Get(TEXT("Medkit"));
        Test->TestTrue(TEXT("Medical treatment succeeds"), Health->Treat(Medkit));
        auto* Buff = C->FindComponentByClass<UBuffComponent>();
        Buff->BuffHeal(4, 1); Buff->TickComponent(.5f, LEVELTICK_All, nullptr);
        Test->TestEqual(TEXT("Medical and buff each notify once"), Events, 2);
        Test->TestEqual(TEXT("Medical and buff compose"), C->GetHealth(), FMath::Min(100.f, 32.f + Medkit->HealAmount));
        Buff->TickComponent(.5f, LEVELTICK_All, nullptr);

        // A real second controller/state lets the normal game mode choose downing over final death.
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* MatePC = W->SpawnActor<AExtractionPlayerController>();
        auto* Mate = W->SpawnActor<ABrokenEdgeCharacter>(C->GetActorLocation() + FVector(0, 110, 0), FRotator::ZeroRotator, Spawn);
        MatePC->Possess(Mate);
        auto* MatePS = MatePC->GetPlayerState<AExtractionPlayerState>();
        if (Test->TestNotNull(TEXT("Rescuer player state"), MatePS))
        {
            MatePS->Life = EExtractionLife::Alive; MatePS->ExtractionSquad = 123; PS->ExtractionSquad = 123;
            Mate->GetCharacterMovement()->StopMovementImmediately();
            Mate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Reset();
            Health->Explosion(200, nullptr, nullptr);
            Test->TestTrue(TEXT("Lethal hit with rescuer enters downed state"), PS->Life == EExtractionLife::Downed);
            Test->TestEqual(TEXT("Downing notifies once"), Events, 1);
            Test->TestEqual(TEXT("Downing calculates once"), C->TestDamageCalculations, 1);
            Events = 0; C->TestDamageCalculations = 0;
            const float DownBefore = PS->DownHealth;
            Health->Explosion(20, nullptr, nullptr);
            Test->TestEqual(TEXT("Downed hit uses downed pool"), PS->DownHealth, DownBefore - 20);
            Test->TestEqual(TEXT("Downed hit calculates once"), C->TestDamageCalculations, 1);
            Test->TestEqual(TEXT("Downed hit notifies once"), Events, 1);
            Test->TestFalse(TEXT("Medical preflight rejects downed before consuming"), Health->Needs(EExtractionMedicalEffect::Heal));
            Test->TestFalse(TEXT("Medical cannot consume on rejected downed healing"), Health->Treat(Medkit));
            Test->TestFalse(TEXT("Buff cannot revive"), C->ApplyVitalsChange({10}).Applied);
            Events = 0;
            auto* Rescue = Mate->GetExtractionAbilities();
            Rescue->Request(EExtractionAction::Revive, C);
            Test->TestTrue(TEXT("Real rescue request accepted"), Rescue->Action == EExtractionAction::Revive);
            Rescue->ActionEnd = W->GetTimeSeconds();
            Rescue->TickComponent(.05f, LEVELTICK_All, nullptr);
            Test->TestTrue(TEXT("Rescue restores alive state"), PS->Life == EExtractionLife::Alive);
            Test->TestEqual(TEXT("Rescue restores configured health"), C->GetHealth(), GetDefault<UExtractionRules>()->ReviveHealth);
            Test->TestEqual(TEXT("Rescue notifies once"), Events, 1);
            Test->TestTrue(TEXT("Rescue releases action"), Rescue->Action == EExtractionAction::None);
        }
        Mate->Destroy(); MatePC->Destroy(); PS->ExtractionSquad = Squad;
        C->OnVitalsChanged.Remove(Handle);

        // Guard death follows the real defeat path without settling a player profile.
        auto* Guard = W->SpawnActor<ABrokenEdgeCharacter>(C->GetActorLocation() + FVector(0, 300, 0), FRotator::ZeroRotator, Spawn);
        Guard->bExtractionGuard = true;
        Guard->ApplyVitalsChange({1 - Guard->GetHealth(), 0, EExtractionVitalsReason::Restore});
        int32 DeathEvents = 0;
        const auto DeathHandle = Guard->OnVitalsChanged.AddLambda([&](const FExtractionVitalsChange&) { ++DeathEvents; });
        auto* Trauma = Guard->GetExtractionHealth();
        Trauma->Bleeding = true;
        Trauma->TickComponent(.1f, LEVELTICK_All, nullptr);
        Test->TestTrue(TEXT("Bleeding routes through final death"), Guard->IsDefeated());
        Test->TestEqual(TEXT("Lethal bleed notifies once"), DeathEvents, 1);
        Test->TestEqual(TEXT("Bleed does not recalculate weapon damage"), Guard->TestDamageCalculations, 0);
        Trauma->TickComponent(.1f, LEVELTICK_All, nullptr);
        Test->TestEqual(TEXT("Dead guard cannot be killed twice"), DeathEvents, 1);
        Guard->OnVitalsChanged.Remove(DeathHandle); Guard->Destroy();
        auto* PendingDeath = W->SpawnActor<ABrokenEdgeCharacter>();
        PendingDeath->bExtractionGuard = true;
        PendingDeath->ApplyVitalsChange({-100, 0, EExtractionVitalsReason::Restore});
        int32 RetryEvents = 0;
        const auto RetryHandle = PendingDeath->OnVitalsChanged.AddLambda([&](const FExtractionVitalsChange&) { ++RetryEvents; });
        const auto Retry = PendingDeath->ApplyVitalsChange({-1, 0, EExtractionVitalsReason::Bleeding});
        Test->TestTrue(TEXT("Zero HP without completed death can retry defeat"), Retry.Defeated && Retry.Applied);
        Test->TestEqual(TEXT("Life transition without HP delta still notifies once"), RetryEvents, 1);
        PendingDeath->OnVitalsChanged.Remove(RetryHandle); PendingDeath->Destroy();
        return true;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds();
};

class FRestoreExtractionVitalsPIESettings : public IAutomationLatentCommand
{
public:
    FRestoreExtractionVitalsPIESettings()
    {
        const auto* S = GetDefault<ULevelEditorPlaySettings>();
        S->GetPlayNetMode(Mode); S->GetPlayNumberOfClients(Clients);
        S->GetRunUnderOneProcess(OneProcess); SeparateServer = S->bLaunchSeparateServer;
    }
    bool Update() override
    {
        auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
        S->SetPlayNetMode(Mode); S->SetPlayNumberOfClients(Clients);
        S->SetRunUnderOneProcess(OneProcess); S->bLaunchSeparateServer = SeparateServer;
        return true;
    }
private:
    EPlayNetMode Mode = PIE_Standalone;
    int32 Clients = 1;
    bool OneProcess = true, SeparateServer = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionVitalsPIE, "BrokenEdge.Extraction.VitalsPIE.DamageAndTreatment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtractionVitalsPIE::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreExtractionVitalsPIESettings>();
    auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
    S->SetPlayNetMode(PIE_Standalone); S->SetPlayNumberOfClients(1);
    S->SetRunUnderOneProcess(true); S->bLaunchSeparateServer = false;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FExtractionVitalsPIESequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
