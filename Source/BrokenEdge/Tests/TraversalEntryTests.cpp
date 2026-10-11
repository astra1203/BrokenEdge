#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Traversal/TraversalLadderEntry.h"

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
#endif
