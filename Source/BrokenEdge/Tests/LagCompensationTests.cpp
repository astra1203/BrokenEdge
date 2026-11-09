#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/Weapon/ExtractionBullet.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/PhysicsAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLagCompensationGeometryTest, "BrokenEdge.LagCompensation.SegmentGeometry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLagCompensationGeometryTest::RunTest(const FString&)
{
	float T = -1;
	TestTrue(TEXT("Sphere front face"), HitRewind::SegmentSphere({-10, 0, 0}, {10, 0, 0}, FVector::ZeroVector, 2, T));
	TestEqual(TEXT("Sphere entry fraction"), T, .4f, 1e-4f);
	TestFalse(TEXT("Sphere miss"), HitRewind::SegmentSphere({-10, 3, 0}, {10, 3, 0}, FVector::ZeroVector, 2, T));
	TestFalse(TEXT("Sphere beyond segment end"),
	          HitRewind::SegmentSphere({-10, 0, 0}, {-5, 0, 0}, FVector::ZeroVector, 2, T));
	TestTrue(TEXT("Start inside sphere"), HitRewind::SegmentSphere({.5, 0, 0}, {10, 0, 0}, FVector::ZeroVector, 2, T));
	TestEqual(TEXT("Start inside sphere hits at zero"), T, 0.f);

	TestTrue(TEXT("Capsule cylinder"), HitRewind::SegmentCapsule({-10, 0, 4}, {10, 0, 4}, 2, 5, T));
	TestEqual(TEXT("Capsule cylinder fraction"), T, .4f, 1e-4f);
	TestTrue(TEXT("Capsule cap"), HitRewind::SegmentCapsule({-10, 0, 6}, {10, 0, 6}, 2, 5, T));
	TestEqual(TEXT("Capsule cap fraction"), T, (10 - FMath::Sqrt(3.f)) / 20, 1e-4f);
	TestFalse(TEXT("Capsule above cap"), HitRewind::SegmentCapsule({-10, 0, 7.5}, {10, 0, 7.5}, 2, 5, T));
	TestTrue(TEXT("Capsule along axis"), HitRewind::SegmentCapsule({0, 0, 20}, {0, 0, -20}, 2, 5, T));
	TestEqual(TEXT("Capsule along axis fraction"), T, (20 - 7) / 40.f, 1e-4f);

	TestTrue(TEXT("Box face"), HitRewind::SegmentBox({-10, 1.5, 0}, {10, 1.5, 0}, {1, 2, 3}, T));
	TestEqual(TEXT("Box face fraction"), T, .45f, 1e-4f);
	TestFalse(TEXT("Box miss"), HitRewind::SegmentBox({-10, 2.5, 0}, {10, 2.5, 0}, {1, 2, 3}, T));
	TestTrue(TEXT("Box parallel inside slab"), HitRewind::SegmentBox({0, -10, 0}, {0, 10, 0}, {1, 2, 3}, T));
	TestEqual(TEXT("Box parallel fraction"), T, .4f, 1e-4f);

	// 骨骼在 (100,0,0) 绕 Z 转 90°，球心在骨骼局部 (0,10,0)，世界坐标应为 (90,0,0)。
	FHitShape Shape;
	Shape.Kind = FHitShape::EKind::Sphere;
	Shape.Body = 0;
	Shape.Local = FTransform(FVector(0, 10, 0));
	Shape.Radius = 5;
	const FTransform Bone(FRotator(0, 90, 0), FVector(100, 0, 0));
	TestTrue(TEXT("Transformed sphere"), HitRewind::SegmentShape(Shape, Bone, {90, -50, 0}, {90, 50, 0}, 0, T));
	TestEqual(TEXT("Transformed sphere fraction"), T, .45f, 1e-4f);
	TestTrue(TEXT("Inflated sphere"), HitRewind::SegmentShape(Shape, Bone, {90, -50, 0}, {90, 50, 0}, 1, T));
	TestEqual(TEXT("Bullet radius inflates shape"), T, .44f, 1e-4f);
	TestFalse(TEXT("Transformed sphere miss"), HitRewind::SegmentShape(Shape, Bone, {110, -50, 0}, {110, 50, 0}, 0, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLagCompensationRewindTest, "BrokenEdge.LagCompensation.PhysicsAssetRewind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLagCompensationRewindTest::RunTest(const FString&)
{
	IConsoleVariable* MaxRewind = IConsoleManager::Get().FindConsoleVariable(TEXT("BrokenEdge.Net.MaxRewindMs"));
	if (!TestNotNull(TEXT("Rewind cap console variable"), MaxRewind)) return false;
	const int32 SavedCap = MaxRewind->GetInt();
	MaxRewind->Set(200, ECVF_SetByCode);

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
	                                                .CreateNavigation(false).CreateAISystem(false)
	                                                .ShouldSimulatePhysics(false).EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ON_SCOPE_EXIT
	{
		MaxRewind->Set(SavedCap, ECVF_SetByCode);
		World->DestroyWorld(false);
	};
	UClass* StingerClass = StaticLoadClass(ABrokenEdgeCharacter::StaticClass(), nullptr,
	                                       TEXT("/Game/Characters/Stinger/Blueprints/BP_StingerCharacter.BP_StingerCharacter_C"));
	if (!TestNotNull(TEXT("Stinger Blueprint loads"), StingerClass)) return false;
	auto* Target = World->SpawnActor<ABrokenEdgeCharacter>(StingerClass, FVector(0, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Stinger spawns"), Target)) return false;
	USkeletalMeshComponent* Mesh = Target->GetMesh();
	if (!TestNotNull(TEXT("Hit shapes come from the mesh physics asset"), Mesh->GetPhysicsAsset())) return false;
	ULagCompensationComponent* Lag = Target->GetLagCompensation();
	Mesh->RefreshBoneTransforms();

	World->TimeSeconds = 10.0;
	Lag->SaveFrame();
	if (!TestTrue(TEXT("Physics asset bodies recorded"), Lag->GetNumShapes() > 0 && Lag->GetNumFrames() == 1))
		return false;
	const FVector HeadOld = Mesh->GetBoneLocation(TEXT("Head"));
	Target->SetActorLocation(Target->GetActorLocation() + FVector(0, 300, 0));
	World->TimeSeconds = 10.1;
	Lag->SaveFrame();
	TestEqual(TEXT("Two frames recorded"), Lag->GetNumFrames(), 2);

	// 从头顶竖直向下穿过旧头部位置：第一处命中必然是头。
	auto Down = [](const FVector& At) { return TPair<FVector, FVector>(At + FVector(0, 0, 60), At - FVector(0, 0, 2)); };
	FRewindHit Hit;
	auto [OldStart, OldEnd] = Down(HeadOld);
	TestTrue(TEXT("Rewound pose is hit where the shooter saw it"), Lag->TraceAt(10.0, OldStart, OldEnd, 0, Hit));
	TestEqual(TEXT("Rewound hit reports the physics body bone"), Hit.Bone, FName(TEXT("Head")));
	TestFalse(TEXT("Current pose has left the old position"), Lag->TraceAt(10.1, OldStart, OldEnd, 0, Hit));
	auto [MidStart, MidEnd] = Down(HeadOld + FVector(0, 150, 0));
	TestTrue(TEXT("Poses interpolate between recorded frames"), Lag->TraceAt(10.05, MidStart, MidEnd, 0, Hit));

	// 旧帧距现在超过上限时只能倒回到上限处（此处即最旧的可用帧之后），旧位置不再可命中。
	World->TimeSeconds = 11.0;
	TestFalse(TEXT("Rewind is capped by BrokenEdge.Net.MaxRewindMs"), Lag->TraceAt(10.0, OldStart, OldEnd, 0, Hit));
	World->TimeSeconds = 10.1;

	// 子弹整合：带倒带的子弹打中射手画面里的旧位置；不带倒带的同一发子弹扫掠当前世界，穿过空处。
	const auto* Source = WeaponMechanics::Find(TEXT("SCARH"));
	if (!TestNotNull(TEXT("SCAR profile"), Source)) return false;
	Lag->BeginPlay();
	ON_SCOPE_EXIT { Lag->EndPlay(EEndPlayReason::RemovedFromWorld); };
	auto Shoot = [&](float Rewind)
	{
		auto* Bullet = World->SpawnActor<AExtractionBullet>(HeadOld + FVector(-600, 0, 0), FRotator::ZeroRotator);
		Bullet->Initialize(*Source, FVector::ForwardVector, 35, 70, TEXT("Ammo76251"), nullptr, nullptr, nullptr);
		Bullet->SetRewind(Rewind);
		Bullet->Tick(1.f / 60);
		return Bullet;
	};
	AExtractionBullet* Rewound = Shoot(.1f);
	TestTrue(TEXT("Rewound bullet stops at the old head"),
	         FMath::Abs(Rewound->GetActorLocation().X - HeadOld.X) < 25);
	// 临时世界没有 BeginPlay，Tick 开关不能代表"仍在飞行"，用位置判断：一帧 1/60 秒约飞 10 米。
	AExtractionBullet* Current = Shoot(0);
	TestTrue(TEXT("Without rewind the bullet passes the vacated position"),
	         Current->GetActorLocation().X > HeadOld.X + 300);
	return true;
}
#endif
