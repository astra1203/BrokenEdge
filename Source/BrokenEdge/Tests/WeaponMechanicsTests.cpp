#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"
#include "JsonObjectConverter.h"
#include "BrokenEdge/Weapon/ExtractionBullet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Extraction/Combat/ExtractionWeaponVisuals.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponMechanicsSourceTest, "BrokenEdge.Extraction.WeaponMechanics.SourceAndSampling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponMechanicsSourceTest::RunTest(const FString&)
{
	const auto* AK = WeaponMechanics::Find(TEXT("AK"));
	const auto* Scar = WeaponMechanics::Find(TEXT("SCARH"));
	if (!TestNotNull(TEXT("AKM source profile"), AK) || !TestNotNull(TEXT("SCAR source profile"), Scar)) return false;
	TestEqual(TEXT("AKM interval"), AK->Interval, .1f);
	TestEqual(TEXT("SCAR interval"), Scar->Interval, .1026f);
	TestEqual(TEXT("SCAR velocity in cm/s"), Scar->Speed, 63000.f);
	TestEqual(TEXT("SCAR continuous sequence"), Scar->Continuous[1].Pitch.Num(), 38);
	TestEqual(TEXT("SCAR loop sequence"), Scar->Loop[1].Pitch.Num(), 6);
	TestEqual(TEXT("SCAR zero base ADS spread"), Scar->Spread[1].Base, FVector2D::ZeroVector);
	TestTrue(TEXT("Hip spread grows per shot"), Scar->Spread[0].PerShot.X > 0);
	TestEqual(TEXT("No attenuation before boundary"), Scar->Falloff(4000), 1.f);
	TestEqual(TEXT("Explicit compatibility segment 1"), Scar->Falloff(4001), .9f);
	TestEqual(TEXT("Explicit compatibility segment 2"), Scar->Falloff(7001), .75f);
	TestEqual(TEXT("First boundary left"), Scar->Falloff(3999), 1.f);
	TestEqual(TEXT("Second boundary left"), Scar->Falloff(6999), .9f);
	TestEqual(TEXT("Second boundary inclusive"), Scar->Falloff(7000), .9f);
	TestEqual(TEXT("Partial mouse compensation"), WeaponMechanics::CompensatedDebt(4., -1.), 3.);
	TestEqual(TEXT("Overcompensation never reverses recovery"), WeaponMechanics::CompensatedDebt(4., -8.), 0.);
	TestEqual(TEXT("Same direction is voluntary input"), WeaponMechanics::CompensatedDebt(4., 2.), 4.);
	TestEqual(TEXT("Negative yaw compensation"), WeaponMechanics::CompensatedDebt(-4., 1.), -3.);
	for (uint32 Shot = 0; Shot < 150; ++Shot)
	{
		const auto A = Scar->Recoil(Shot, true, 1, Shot + 27);
		TestTrue(TEXT("Finite looping recoil"), FMath::IsFinite(A.X) && FMath::IsFinite(A.Y));
		TestEqual(TEXT("Repeatable recoil"), A, Scar->Recoil(Shot, true, 1, Shot + 27));
	}
	FVector Mean = FVector::ZeroVector;
	for (uint32 Sample = 0; Sample < 10000; ++Sample)
	{
		const auto V = WeaponMechanics::SpreadDirection(FVector::ForwardVector, FVector2D(2, 3), Sample);
		TestTrue(TEXT("Unit spread direction"), V.IsNormalized());
		TestTrue(TEXT("Elliptical bound"), FMath::Square(V.Y / V.X / FMath::Tan(FMath::DegreesToRadians(2.f)))
		         + FMath::Square(V.Z / V.X / FMath::Tan(FMath::DegreesToRadians(3.f))) <= 1.002);
		Mean += V;
	}
	Mean /= 10000;
	TestTrue(TEXT("No systematic horizontal/vertical bias"), FMath::Abs(Mean.Y) < .001 && FMath::Abs(Mean.Z) < .001);
	TestEqual(TEXT("Zero spread preserves aim"), WeaponMechanics::SpreadDirection(FVector::ForwardVector, {}, 1),
	          FVector::ForwardVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponAssemblyTest, "BrokenEdge.Extraction.WeaponMechanics.AssemblyTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponAssemblyTest::RunTest(const FString&)
{
	FString Error;
	FExtractionContainer Container;
	Container.Id = FGuid::NewGuid();
	Container.Width = 12;
	Container.Height = 12;
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	auto Grip = ExtractionItems::Make(TEXT("VerticalGrip"));
	const FGuid GunId = Gun.Id, GripId = Grip.Id;
	TestTrue(TEXT("Place gun"), ExtractionItems::AutoPlace(Container, Gun));
	TestTrue(TEXT("Place grip"), ExtractionItems::AutoPlace(Container, Grip));
	FExtractionItemRequest Request;
	Request.Op = EExtractionItemOp::Attach;
	Request.Weapon = GunId;
	Request.Item = GripId;
	TestTrue(TEXT("Legacy root attachment remains valid"), ExtractionItems::Configure(Container, Request, Error));
	const auto* Mounted = Container.Find(GunId);
	const auto Stats = WeaponMechanics::ResolveWeaponStats(*Mounted);
	TestTrue(
		TEXT("Mounted config has a distinct version"),
		Stats.IsValid() && Stats.Revision != WeaponMechanics::Find(TEXT("AK"))->Revision);
	FExtractionItem Forged = *Mounted;
	Forged.Attachments[0].Parent = Forged.Attachments[0].Id;
	TestFalse(TEXT("Reject attachment cycle"), ExtractionItems::ValidateItem(Forged, false, Error));
	Forged = *Mounted;
	Forged.Attachments[0].Parent = FGuid::NewGuid();
	TestFalse(TEXT("Reject absent parent"), ExtractionItems::ValidateItem(Forged, false, Error));
	FString Before, After;
	FJsonObjectConverter::UStructToJsonObjectString(Container, Before);
	Request.Op = EExtractionItemOp::Tune;
	Request.MountedAttachment = GripId;
	Request.TuneId = TEXT("unknown");
	Request.TuneValue = 50;
	TestFalse(TEXT("Unknown precision function rejected"), ExtractionItems::Configure(Container, Request, Error));
	FJsonObjectConverter::UStructToJsonObjectString(Container, After);
	TestEqual(TEXT("Rejected transaction preserves all state"), Before, After);
	const auto* TunedDefinition = UExtractionItemDefinition::Get(TEXT("VerticalGrip"));
	if (TestTrue(TEXT("Authored compatibility tuning available"), !TunedDefinition->Tunes.IsEmpty()))
	{
		const auto& Tune = TunedDefinition->Tunes[0];
		Request.TuneId = Tune.Id;
		Request.TuneValue = Tune.Maximum;
		TestTrue(TEXT("Valid tuning applies transactionally"), ExtractionItems::Configure(Container, Request, Error));
		const auto Tuned = WeaponMechanics::ResolveWeaponStats(*Container.Find(GunId));
		TestTrue(TEXT("Tuning changes resolved recoil"), Tuned.RecoilScale < Stats.RecoilScale);
		FJsonObjectConverter::UStructToJsonObjectString(Container, After);
		FExtractionContainer Restored;
		TestTrue(
			TEXT("Tuned assembly survives serialization"),
			FJsonObjectConverter::JsonObjectStringToUStruct(After, &Restored));
		TestEqual(
			TEXT("Restored tuning produces same config"),
			WeaponMechanics::ResolveWeaponStats(*Restored.Find(GunId)).Revision, Tuned.Revision);
		const auto Menu = ExtractionItems::EquipmentActions(Container, GunId, true);
		TestTrue(TEXT("Detailed actions expose reset to default"), Menu.ContainsByPredicate([&](const auto& A)
		{
			return A.Request.Op == EExtractionItemOp::Tune && A.Request.TuneValue == Tune.Default;
		}));
		Request.TuneValue = Tune.Maximum + 1;
		TestFalse(TEXT("Out of range tuning rejected"), ExtractionItems::Configure(Container, Request, Error));
	}
	Request.Op = EExtractionItemOp::Detach;
	TestTrue(TEXT("Detach by stable instance ID"), ExtractionItems::Configure(Container, Request, Error));
	TestNotNull(TEXT("Part is returned, not destroyed"), Container.Find(GripId));
	// Fixture-only authoring; restore the loaded definition and never save a package.
	auto* GripDefinition = const_cast<UExtractionItemDefinition*>(UExtractionItemDefinition::Get(TEXT("VerticalGrip")));
	TGuardValue<TArray<FWeaponAttributeModifier>> RestoreModifiers(GripDefinition->WeaponModifiers,
	                                                               GripDefinition->WeaponModifiers);
	FWeaponAttributeModifier Capacity;
	Capacity.Attribute = TEXT("MagazineCapacity");
	Capacity.Operation = EWeaponModifierOperation::Replace;
	Capacity.Value = 10;
	GripDefinition->WeaponModifiers.Add(Capacity);
	Request.Op = EExtractionItemOp::Attach;
	Request.Item = GripId;
	FJsonObjectConverter::UStructToJsonObjectString(Container, Before);
	TestFalse(TEXT("Loaded magazine cannot shrink"), ExtractionItems::Configure(Container, Request, Error));
	FJsonObjectConverter::UStructToJsonObjectString(Container, After);
	TestEqual(TEXT("Rejected capacity change keeps rounds and part"), Before, After);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponFlightTest, "BrokenEdge.Extraction.WeaponMechanics.FlightAndObstruction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponFlightTest::RunTest(const FString&)
{
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
	                                                          .CreateNavigation(false).CreateAISystem(false).
	                                                          ShouldSimulatePhysics(false).EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
	                                    &Initialization);
	const auto* Source = WeaponMechanics::Find(TEXT("SCARH"));
	if (!Source)
	{
		World->DestroyWorld(false);
		return false;
	}
	FResolvedWeaponStats Profile = *Source;
	FVector Results[3];
	const int32 Rates[] = {30, 60, 120};
	for (int32 I = 0; I < 3; ++I)
	{
		auto* Bullet = World->SpawnActor<AExtractionBullet>(FVector(0, I * 100, 1000), FRotator::ZeroRotator);
		Bullet->Initialize(Profile, FVector::ForwardVector, 35, 70, TEXT("Ammo76251"), nullptr, nullptr, nullptr);
		for (int32 Frame = 0; Frame < Rates[I]; ++Frame) Bullet->Tick(1.f / Rates[I]);
		Results[I] = Bullet->GetActorLocation() - FVector(0, I * 100, 1000);
		TestTrue(TEXT("Flight distance follows initial speed"), FMath::IsNearlyEqual(Results[I].X, 63000., 1.));
		TestTrue(TEXT("Gravity lowers flight"), Results[I].Z < -400 && Results[I].Z > -500);
		Bullet->Destroy();
	}
	TestTrue(TEXT("30/60 FPS identical fixed-step integration"), Results[0].Equals(Results[1], .1));
	TestTrue(TEXT("60/120 FPS identical fixed-step integration"), Results[1].Equals(Results[2], .1));
	auto* Wall = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(2, 100, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(1000, 0, 0));
	auto* Bullet = World->SpawnActor<AExtractionBullet>();
	Bullet->Initialize(Profile, FVector::ForwardVector, 35, 70, TEXT("Ammo76251"), nullptr, nullptr, nullptr);
	Bullet->Tick(.1f);
	TestTrue(TEXT("Sweeps cannot tunnel through a thin near wall"), Bullet->GetActorLocation().X < 1000);
	TestFalse(TEXT("Blocking hit terminates flight"), Bullet->IsActorTickEnabled());
	auto* Weapon = World->SpawnActor<AWeapon>();
	FExtractionAttachment Parent, Child;
	Parent.Id = FGuid::NewGuid();
	Parent.Definition = TEXT("RedDot");
	Child.Id = FGuid::NewGuid();
	Child.Definition = TEXT("VerticalGrip");
	Child.Parent = Parent.Id;
	Weapon->ExtractionParts = {Child.Definition, Parent.Definition};
	Weapon->ExtractionAssembly = {Child, Parent}; // Deliberately reverse topological order.
	Weapon->ExtractionVisuals->TickComponent(0, LEVELTICK_All, nullptr);
	TArray<UStaticMeshComponent*> Components;
	Weapon->GetComponents(Components);
	UStaticMeshComponent* ParentMesh = nullptr;
	UStaticMeshComponent* ChildMesh = nullptr;
	for (auto* Component : Components)
	{
		if (Component->GetStaticMesh() == UExtractionItemDefinition::Get(Parent.Definition)->WorldMesh)
			ParentMesh =
				Component;
		if (Component->GetStaticMesh() == UExtractionItemDefinition::Get(Child.Definition)->WorldMesh)
			ChildMesh =
				Component;
	}
	TestNotNull(TEXT("Parent visual created"), ParentMesh);
	TestNotNull(TEXT("Child visual created"), ChildMesh);
	if (ParentMesh && ChildMesh)
		TestTrue(
			TEXT("Child visual follows parent rather than weapon root"), ChildMesh->GetAttachParent() == ParentMesh);

	World->DestroyWorld(false);
	return true;
}
#endif
