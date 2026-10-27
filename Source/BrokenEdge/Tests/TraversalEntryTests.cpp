#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Traversal/TraversalLadderEntry.h"
#include "BrokenEdge/Traversal/TraversalMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraversalEntryCapsuleTest, "BrokenEdge.Traversal.EntryCapsule",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTraversalEntryCapsuleTest::RunTest(const FString& Parameters)
{
	FTraversalLadderEntry Entry;
	TestFalse(TEXT("Unbound entry is disabled"), Entry.OverlapsCapsule(FTransform::Identity, FVector(0, 0, 95), 34,
	                                                                   92));
	Entry.Transform.SetLocation(FVector(0, 0, 31));
	Entry.Extent = FVector(40, 30, 30);
	TestTrue(TEXT("Lower capsule intersects a source box below the capsule centre"),
	         Entry.OverlapsCapsule(FTransform::Identity, FVector(-67, 0, 95), 34, 92));
	TestFalse(TEXT("Outside capsule radius is rejected"),
	          Entry.OverlapsCapsule(FTransform::Identity, FVector(-76, 0, 95), 34, 92));
	TestFalse(TEXT("Rounded corner does not use an inflated AABB approximation"),
	          Entry.OverlapsCapsule(FTransform::Identity, FVector(65, 55, 95), 34, 92));
	TestFalse(TEXT("Another floor cannot trigger the box"),
	          Entry.OverlapsCapsule(FTransform::Identity, FVector(0, 0, 345), 34, 92));
	const FTransform Parent(FRotator(0, 90, 0), FVector(1000, -300, 200), FVector(2, 1, 1));
	TestTrue(TEXT("Actor rotation, translation and source scale are retained"),
	         Entry.OverlapsCapsule(Parent, Parent.TransformPosition(FVector(-50, 0, 95)), 34, 92));
	Entry.Transform.SetRotation(FRotator(30, 0, 0).Quaternion());
	TestFalse(TEXT("Unsupported tilted source trigger fails closed"),
	          Entry.OverlapsCapsule(FTransform::Identity, FVector(0, 0, 95), 34, 92));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraversalFacingTest, "BrokenEdge.Traversal.Facing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTraversalFacingTest::RunTest(const FString& Parameters)
{
	// 正对障碍：朝向与相机一致。
	const FVector Straight = UTraversalMovementComponent::SnapFacingToNormal(
		FVector(1, 0, 0), FVector(-1, 0, 0));
	TestTrue(TEXT("Head-on approach keeps the facing"), Straight.Equals(FVector(1, 0, 0), 1e-3));

	// 斜着撞上箱体：朝向必须对齐表面法线，而不是沿用相机朝向。
	const FVector Camera = FVector(0.5f, 0.5f, 0).GetSafeNormal();
	const FVector Snapped = UTraversalMovementComponent::SnapFacingToNormal(Camera, FVector(-1, 0, 0));
	TestTrue(TEXT("Diagonal approach snaps to the surface normal"), Snapped.Equals(FVector(1, 0, 0), 1e-3));
	TestTrue(TEXT("Snapped facing opposes the impact normal"),
	         FVector::DotProduct(Snapped, FVector(-1, 0, 0)) < -.99f);

	// 未归一化法线也能得到单位朝向。
	const FVector Unnormalized = UTraversalMovementComponent::SnapFacingToNormal(
		FVector(1, 0, 0), FVector(0, -12, 0));
	TestTrue(TEXT("Unnormalised normal is normalised"), Unnormalized.Equals(FVector(0, 1, 0), 1e-3));

	// 顶面/底面命中（法线接近竖直）时退回首选朝向，避免出现零向量朝向。
	const FVector Degenerate = UTraversalMovementComponent::SnapFacingToNormal(
		FVector(0, 1, 0), FVector(0, 0, 1));
	TestTrue(TEXT("Vertical normal falls back to the preferred facing"),
	         Degenerate.Equals(FVector(0, 1, 0), 1e-3));
	return true;
}
#endif
