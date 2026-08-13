#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "TrafficRoad.h"

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficRoadLaneHandleTest,
    "TrafficSimulation.Road.LaneHandleValidation",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficRoadLaneHandleTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    ATrafficRoad* Road =
        World->SpawnActor<ATrafficRoad>();

    TestNotNull(TEXT("Road was spawned"), Road);

    if (!Road)
    {
        return false;
    }

    const FTrafficLaneHandle LaneZero =
        Road->GetLaneHandle(0);

    const FTrafficLaneHandle LaneOne =
        Road->GetLaneHandle(1);

    const FTrafficLaneHandle NegativeLane =
        Road->GetLaneHandle(-1);

    const FTrafficLaneHandle OutOfRangeLane =
        Road->GetLaneHandle(2);

    TestTrue(
        TEXT("Lane zero handle is valid"),
        LaneZero.IsValid());

    TestTrue(
        TEXT("Lane one handle is valid"),
        LaneOne.IsValid());

    TestFalse(
        TEXT("Negative lane index is rejected"),
        NegativeLane.IsValid());

    TestFalse(
        TEXT("Out-of-range lane index is rejected"),
        OutOfRangeLane.IsValid());

    TestNotEqual(
        TEXT("Different lanes have different handles"),
        LaneZero,
        LaneOne);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficRoadOpposingLaneDirectionTest,
    "TrafficSimulation.Road.OpposingLaneDirections",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficRoadOpposingLaneDirectionTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    ATrafficRoad* Road =
        World->SpawnActor<ATrafficRoad>();

    TestNotNull(TEXT("Road was spawned"), Road);

    if (!Road)
    {
        return false;
    }

    const FTrafficLaneHandle ForwardLane =
        Road->GetLaneHandle(0);

    const FTrafficLaneHandle ReverseLane =
        Road->GetLaneHandle(1);

    float LaneLengthCm = 0.0f;

    const bool bLengthResolved =
        Road->GetLaneLength(
            ForwardLane,
            LaneLengthCm);

    TestTrue(
        TEXT("Lane length resolves"),
        bLengthResolved);

    if (!bLengthResolved)
    {
        return false;
    }

    const float EarlierDistanceCm =
        LaneLengthCm * 0.25f;

    const float LaterDistanceCm =
        LaneLengthCm * 0.75f;

    FTransform ForwardEarlierTransform;
    FTransform ForwardLaterTransform;
    FTransform ReverseEarlierTransform;
    FTransform ReverseLaterTransform;

    const bool bForwardEarlierEvaluated =
        Road->EvaluateLaneAtDistance(
            ForwardLane,
            EarlierDistanceCm,
            ForwardEarlierTransform);

    const bool bForwardLaterEvaluated =
        Road->EvaluateLaneAtDistance(
            ForwardLane,
            LaterDistanceCm,
            ForwardLaterTransform);

    const bool bReverseEarlierEvaluated =
        Road->EvaluateLaneAtDistance(
            ReverseLane,
            EarlierDistanceCm,
            ReverseEarlierTransform);

    const bool bReverseLaterEvaluated =
        Road->EvaluateLaneAtDistance(
            ReverseLane,
            LaterDistanceCm,
            ReverseLaterTransform);

    TestTrue(
        TEXT("Forward lane evaluates at both distances"),
        bForwardEarlierEvaluated && bForwardLaterEvaluated);

    TestTrue(
        TEXT("Reverse lane evaluates at both distances"),
        bReverseEarlierEvaluated && bReverseLaterEvaluated);

    if (!bForwardEarlierEvaluated ||
        !bForwardLaterEvaluated ||
        !bReverseEarlierEvaluated ||
        !bReverseLaterEvaluated)
    {
        return false;
    }

    const FVector ForwardDisplacement =
        ForwardLaterTransform.GetLocation() -
        ForwardEarlierTransform.GetLocation();

    const FVector ReverseDisplacement =
        ReverseLaterTransform.GetLocation() -
        ReverseEarlierTransform.GetLocation();

    const FVector ForwardFacing =
        ForwardEarlierTransform.GetUnitAxis(EAxis::X);

    const FVector ReverseFacing =
        ReverseEarlierTransform.GetUnitAxis(EAxis::X);

    TestTrue(
        TEXT("Forward lane moves along its facing direction"),
        FVector::DotProduct(
            ForwardDisplacement.GetSafeNormal(),
            ForwardFacing) > 0.99f);

    TestTrue(
        TEXT("Reverse lane moves along its facing direction"),
        FVector::DotProduct(
            ReverseDisplacement.GetSafeNormal(),
            ReverseFacing) > 0.99f);

    TestTrue(
        TEXT("Lane movement directions oppose each other"),
        FVector::DotProduct(
            ForwardDisplacement.GetSafeNormal(),
            ReverseDisplacement.GetSafeNormal()) < -0.99f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficRoadOpenDistanceClampingTest,
    "TrafficSimulation.Road.OpenDistanceClamping",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficRoadOpenDistanceClampingTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    ATrafficRoad* Road =
        World->SpawnActor<ATrafficRoad>();

    TestNotNull(TEXT("Road was spawned"), Road);

    if (!Road)
    {
        return false;
    }

    const FTrafficLaneHandle LaneHandle =
        Road->GetLaneHandle(0);

    float LaneLengthCm = 0.0f;

    const bool bLengthResolved =
        Road->GetLaneLength(
            LaneHandle,
            LaneLengthCm);

    TestTrue(
        TEXT("Lane length resolves"),
        bLengthResolved);

    if (!bLengthResolved)
    {
        return false;
    }

    FTransform StartTransform;
    FTransform NegativeTransform;
    FTransform EndTransform;
    FTransform BeyondEndTransform;

    const bool bStartEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            0.0f,
            StartTransform);

    const bool bNegativeEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            -500.0f,
            NegativeTransform);

    const bool bEndEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            LaneLengthCm,
            EndTransform);

    const bool bBeyondEndEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            LaneLengthCm + 500.0f,
            BeyondEndTransform);

    TestTrue(
        TEXT("All distances evaluate"),
        bStartEvaluated &&
        bNegativeEvaluated &&
        bEndEvaluated &&
        bBeyondEndEvaluated);

    if (!bStartEvaluated ||
        !bNegativeEvaluated ||
        !bEndEvaluated ||
        !bBeyondEndEvaluated)
    {
        return false;
    }

    TestTrue(
        TEXT("Negative distance clamps to lane start"),
        NegativeTransform.GetLocation().Equals(
            StartTransform.GetLocation(),
            0.1f));

    TestTrue(
        TEXT("Excess distance clamps to lane end"),
        BeyondEndTransform.GetLocation().Equals(
            EndTransform.GetLocation(),
            0.1f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficRoadClosedDistanceWrappingTest,
    "TrafficSimulation.Road.ClosedDistanceWrapping",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficRoadClosedDistanceWrappingTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    ATrafficRoad* Road =
        World->SpawnActor<ATrafficRoad>();

    TestNotNull(TEXT("Road was spawned"), Road);

    if (!Road)
    {
        return false;
    }

    Road->SetRoadClosedLoop(true);

    TestTrue(
        TEXT("Road reports closed-loop state"),
        Road->IsRoadClosedLoop());

    const FTrafficLaneHandle LaneHandle =
        Road->GetLaneHandle(0);

    float LaneLengthCm = 0.0f;

    if (!Road->GetLaneLength(
        LaneHandle,
        LaneLengthCm))
    {
        AddError(TEXT("Could not resolve lane length."));
        return false;
    }

    const float TestDistanceCm =
        LaneLengthCm * 0.25f;

    FTransform ReferenceTransform;
    FTransform PositiveWrappedTransform;
    FTransform NegativeWrappedTransform;

    const bool bReferenceEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            TestDistanceCm,
            ReferenceTransform);

    const bool bPositiveEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            TestDistanceCm + LaneLengthCm,
            PositiveWrappedTransform);

    const bool bNegativeEvaluated =
        Road->EvaluateLaneAtDistance(
            LaneHandle,
            TestDistanceCm - LaneLengthCm,
            NegativeWrappedTransform);

    TestTrue(
        TEXT("All wrapped distances evaluate"),
        bReferenceEvaluated &&
        bPositiveEvaluated &&
        bNegativeEvaluated);

    if (!bReferenceEvaluated ||
        !bPositiveEvaluated ||
        !bNegativeEvaluated)
    {
        return false;
    }

    TestTrue(
        TEXT("Distance beyond length wraps"),
        PositiveWrappedTransform.GetLocation().Equals(
            ReferenceTransform.GetLocation(),
            0.1f));

    TestTrue(
        TEXT("Negative distance wraps"),
        NegativeWrappedTransform.GetLocation().Equals(
            ReferenceTransform.GetLocation(),
            0.1f));

    return true;
}

#endif