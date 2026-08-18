// WITH_DEV_AUTOMATION_TESTS alone is still 1 in a Development *game* build, so
// it does not keep these out of a packaged build. The tests drive the editor's
// map utilities, which only exist when UnrealEd is linked.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/SplineComponent.h"
#include "Junctions/TrafficJunction.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"

namespace TrafficJunctionTestHelpers
{
    // Builds a road running from Start to End so that a cross layout can be
    // assembled around the origin.
    ATrafficRoad* SpawnRoad(
        UWorld* World,
        const FVector& Start,
        const FVector& End)
    {
        if (!World)
        {
            return nullptr;
        }

        ATrafficRoad* Road = World->SpawnActor<ATrafficRoad>();

        if (!Road)
        {
            return nullptr;
        }

        Road->SetActorLocation(FVector::ZeroVector);

        USplineComponent* Spline =
            Road->FindComponentByClass<USplineComponent>();

        if (!Spline)
        {
            return Road;
        }

        Spline->ClearSplinePoints(false);

        Spline->AddSplinePoint(
            Start,
            ESplineCoordinateSpace::World,
            false);

        Spline->AddSplinePoint(
            End,
            ESplineCoordinateSpace::World,
            false);

        Spline->SetClosedLoop(false, false);
        Spline->UpdateSpline();

        // Regenerate lanes against the new spline shape.
        Road->OnConstruction(Road->GetActorTransform());

        return Road;
    }

    // A four-way cross centred on the origin, each arm 4000 cm long.
    ATrafficJunction* BuildCrossJunction(
        UWorld* World,
        TArray<ATrafficRoad*>& OutRoads)
    {
        OutRoads.Reset();

        const float ArmCm = 4000.0f;

        // Roads point inwards so that each has an endpoint at the origin.
        OutRoads.Add(SpawnRoad(
            World,
            FVector(-ArmCm, 0.0f, 0.0f),
            FVector::ZeroVector));

        OutRoads.Add(SpawnRoad(
            World,
            FVector(ArmCm, 0.0f, 0.0f),
            FVector::ZeroVector));

        OutRoads.Add(SpawnRoad(
            World,
            FVector(0.0f, -ArmCm, 0.0f),
            FVector::ZeroVector));

        OutRoads.Add(SpawnRoad(
            World,
            FVector(0.0f, ArmCm, 0.0f),
            FVector::ZeroVector));

        ATrafficJunction* Junction =
            World->SpawnActor<ATrafficJunction>();

        if (!Junction)
        {
            return nullptr;
        }

        Junction->SetActorLocation(FVector::ZeroVector);

        for (ATrafficRoad* Road : OutRoads)
        {
            if (Road)
            {
                Junction->ApproachRoads.Add(Road);
            }
        }

        Junction->RebuildJunction();

        return Junction;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficJunctionConnectorGenerationTest,
    "TrafficSimulation.Junction.ConnectorGeneration",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficJunctionConnectorGenerationTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    TArray<ATrafficRoad*> Roads;

    ATrafficJunction* Junction =
        TrafficJunctionTestHelpers::BuildCrossJunction(World, Roads);

    TestNotNull(TEXT("Junction was spawned"), Junction);

    if (!Junction)
    {
        return false;
    }

    TestTrue(
        TEXT("Junction generated at least one connector"),
        Junction->GetConnectorCount() > 0);

    TestTrue(
        TEXT("Junction reports its connectors as lanes"),
        Junction->GetLaneCount() == Junction->GetConnectorCount());

    for (int32 ConnectorIndex = 0;
        ConnectorIndex < Junction->GetConnectorCount();
        ++ConnectorIndex)
    {
        const FTrafficLaneHandle Handle =
            Junction->GetLaneHandle(ConnectorIndex);

        TestTrue(
            TEXT("Connector lane handle is valid"),
            Handle.IsValid());

        float LengthCm = 0.0f;

        TestTrue(
            TEXT("Connector lane length resolves"),
            Junction->GetLaneLength(Handle, LengthCm));

        TestTrue(
            TEXT("Connector lane has positive length"),
            LengthCm > 0.0f);

        FTrafficConnectorLane Connector;

        TestTrue(
            TEXT("Connector data is retrievable"),
            Junction->GetConnector(ConnectorIndex, Connector));

        TestNotEqual(
            TEXT("Connector does not start and end on the same lane"),
            Connector.SourceExit.Lane,
            Connector.TargetEntry.Lane);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficJunctionArcLengthTest,
    "TrafficSimulation.Junction.ArcLengthParameterisation",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficJunctionArcLengthTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    TArray<ATrafficRoad*> Roads;

    ATrafficJunction* Junction =
        TrafficJunctionTestHelpers::BuildCrossJunction(World, Roads);

    if (!Junction || Junction->GetConnectorCount() == 0)
    {
        AddError(TEXT("Junction produced no connectors to sample."));
        return false;
    }

    const FTrafficLaneHandle Handle = Junction->GetLaneHandle(0);

    float LengthCm = 0.0f;

    if (!Junction->GetLaneLength(Handle, LengthCm) ||
        LengthCm <= 0.0f)
    {
        AddError(TEXT("Connector 0 has no usable length."));
        return false;
    }

    // Walking the connector in equal distance steps must produce equal
    // spatial steps. This is what catches sampling by curve parameter
    // instead of by arc length.
    const int32 StepCount = 16;
    const float StepDistanceCm = LengthCm / static_cast<float>(StepCount);

    TArray<float> StepSizes;
    FTransform PreviousTransform;

    for (int32 StepIndex = 0; StepIndex <= StepCount; ++StepIndex)
    {
        FTransform Transform;

        const float Distance =
            StepDistanceCm * static_cast<float>(StepIndex);

        if (!Junction->EvaluateLaneAtDistance(
            Handle,
            Distance,
            Transform))
        {
            AddError(TEXT("Connector evaluation failed mid-walk."));
            return false;
        }

        if (StepIndex > 0)
        {
            StepSizes.Add(FVector::Distance(
                PreviousTransform.GetLocation(),
                Transform.GetLocation()));
        }

        PreviousTransform = Transform;
    }

    float MinStepCm = TNumericLimits<float>::Max();
    float MaxStepCm = 0.0f;

    for (const float StepSize : StepSizes)
    {
        MinStepCm = FMath::Min(MinStepCm, StepSize);
        MaxStepCm = FMath::Max(MaxStepCm, StepSize);
    }

    // Allow generous tolerance: the polyline approximation is not exact, but
    // a parameter-space bug produces ratios far beyond this.
    TestTrue(
        FString::Printf(
            TEXT("Steps are near-uniform (min %.1f cm, max %.1f cm)"),
            MinStepCm,
            MaxStepCm),
        MaxStepCm <= MinStepCm * 1.25f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficJunctionConflictMatrixTest,
    "TrafficSimulation.Junction.ConflictMatrixSymmetry",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficJunctionConflictMatrixTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    TArray<ATrafficRoad*> Roads;

    ATrafficJunction* Junction =
        TrafficJunctionTestHelpers::BuildCrossJunction(World, Roads);

    if (!Junction || Junction->GetConnectorCount() == 0)
    {
        AddError(TEXT("Junction produced no connectors to test."));
        return false;
    }

    const int32 ConnectorCount = Junction->GetConnectorCount();

    bool bSymmetric = true;
    bool bNoSelfConflict = true;
    bool bDivergingNeverConflict = true;
    bool bMergingAlwaysConflict = true;

    for (int32 First = 0; First < ConnectorCount; ++First)
    {
        FTrafficConnectorLane FirstConnector;

        if (!Junction->GetConnector(First, FirstConnector))
        {
            continue;
        }

        if (FirstConnector.ConflictsWith(First))
        {
            bNoSelfConflict = false;
        }

        for (int32 Second = 0; Second < ConnectorCount; ++Second)
        {
            if (First == Second)
            {
                continue;
            }

            FTrafficConnectorLane SecondConnector;

            if (!Junction->GetConnector(Second, SecondConnector))
            {
                continue;
            }

            const bool bFirstListsSecond =
                FirstConnector.ConflictsWith(Second);

            const bool bSecondListsFirst =
                SecondConnector.ConflictsWith(First);

            if (bFirstListsSecond != bSecondListsFirst)
            {
                bSymmetric = false;
            }

            const bool bSharesSource =
                FirstConnector.SourceExit.Lane ==
                SecondConnector.SourceExit.Lane;

            const bool bSharesTarget =
                FirstConnector.TargetEntry.Lane ==
                SecondConnector.TargetEntry.Lane;

            if (bSharesSource && bFirstListsSecond)
            {
                bDivergingNeverConflict = false;
            }

            if (!bSharesSource && bSharesTarget && !bFirstListsSecond)
            {
                bMergingAlwaysConflict = false;
            }
        }
    }

    TestTrue(TEXT("Conflict matrix is symmetric"), bSymmetric);

    TestTrue(
        TEXT("No connector conflicts with itself"),
        bNoSelfConflict);

    TestTrue(
        TEXT("Connectors sharing a source lane never conflict"),
        bDivergingNeverConflict);

    TestTrue(
        TEXT("Connectors merging into one lane always conflict"),
        bMergingAlwaysConflict);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrafficJunctionArbitrationTest,
    "TrafficSimulation.Junction.ArbitrationIsFifoAndDeadlockFree",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

    bool FTrafficJunctionArbitrationTest::RunTest(
        const FString& Parameters)
{
    UWorld* World =
        FAutomationEditorCommonUtils::CreateNewMap();

    TestNotNull(TEXT("Test world exists"), World);

    if (!World)
    {
        return false;
    }

    TArray<ATrafficRoad*> Roads;

    ATrafficJunction* Junction =
        TrafficJunctionTestHelpers::BuildCrossJunction(World, Roads);

    if (!Junction || Junction->GetConnectorCount() == 0)
    {
        AddError(TEXT("Junction produced no connectors to test."));
        return false;
    }

    // Find a conflicting connector pair to arbitrate between.
    int32 FirstConnectorIndex = INDEX_NONE;
    int32 SecondConnectorIndex = INDEX_NONE;

    for (int32 Index = 0;
        Index < Junction->GetConnectorCount();
        ++Index)
    {
        FTrafficConnectorLane Connector;

        if (Junction->GetConnector(Index, Connector) &&
            Connector.Conflicts.Num() > 0)
        {
            FirstConnectorIndex = Index;

            SecondConnectorIndex =
                Connector.Conflicts[0].OtherConnectorIndex;

            break;
        }
    }

    if (FirstConnectorIndex == INDEX_NONE)
    {
        AddError(TEXT("No conflicting connector pair was generated."));
        return false;
    }

    AActor* FirstVehicle = World->SpawnActor<AActor>();
    AActor* SecondVehicle = World->SpawnActor<AActor>();

    TestNotNull(TEXT("First vehicle spawned"), FirstVehicle);
    TestNotNull(TEXT("Second vehicle spawned"), SecondVehicle);

    if (!FirstVehicle || !SecondVehicle)
    {
        return false;
    }

    // The earlier arrival takes the ticket and therefore the right of way.
    const bool bFirstGranted =
        Junction->RequestEntry(FirstVehicle, FirstConnectorIndex);

    const bool bSecondGranted =
        Junction->RequestEntry(SecondVehicle, SecondConnectorIndex);

    TestTrue(
        TEXT("First arrival is granted entry"),
        bFirstGranted);

    TestFalse(
        TEXT("Conflicting later arrival is refused"),
        bSecondGranted);

    // Repeated polling must not flip the decision.
    TestFalse(
        TEXT("Refused vehicle stays refused while the box is occupied"),
        Junction->RequestEntry(SecondVehicle, SecondConnectorIndex));

    Junction->ReleaseEntry(FirstVehicle);

    TestTrue(
        TEXT("Waiting vehicle proceeds once the box clears"),
        Junction->RequestEntry(SecondVehicle, SecondConnectorIndex));

    Junction->ReleaseEntry(SecondVehicle);

    // Everything queued at once must still drain: exactly one vehicle holds a
    // grant at a time among mutually conflicting movements, and repeatedly
    // releasing the holder always lets another through.
    TArray<AActor*> Vehicles;
    TArray<int32> ConnectorIndices;

    FTrafficConnectorLane SeedConnector;
    Junction->GetConnector(FirstConnectorIndex, SeedConnector);

    Vehicles.Add(World->SpawnActor<AActor>());
    ConnectorIndices.Add(FirstConnectorIndex);

    for (const FTrafficConnectorConflict& Conflict : SeedConnector.Conflicts)
    {
        AActor* Vehicle = World->SpawnActor<AActor>();

        if (Vehicle)
        {
            Vehicles.Add(Vehicle);
            ConnectorIndices.Add(Conflict.OtherConnectorIndex);
        }
    }

    int32 GrantedCount = 0;
    int32 Iterations = 0;
    const int32 MaximumIterations = Vehicles.Num() * 4 + 8;

    while (GrantedCount < Vehicles.Num() &&
        Iterations < MaximumIterations)
    {
        ++Iterations;

        bool bProgressed = false;

        for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
        {
            if (!IsValid(Vehicles[Index]))
            {
                continue;
            }

            if (Junction->RequestEntry(
                Vehicles[Index],
                ConnectorIndices[Index]))
            {
                Junction->ReleaseEntry(Vehicles[Index]);
                Vehicles[Index] = nullptr;

                ++GrantedCount;
                bProgressed = true;
                break;
            }
        }

        if (!bProgressed)
        {
            break;
        }
    }

    TestEqual(
        TEXT("Every queued vehicle eventually gets through"),
        GrantedCount,
        ConnectorIndices.Num());

    return true;
}

#endif
