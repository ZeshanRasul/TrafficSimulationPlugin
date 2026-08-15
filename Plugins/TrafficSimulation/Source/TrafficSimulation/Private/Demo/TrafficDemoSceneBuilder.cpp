#include "Demo/TrafficDemoSceneBuilder.h"

#include "Components/SceneComponent.h"
#include "Debug/TrafficDebugOverlay.h"
#include "Demo/TrafficCongestionExperiment.h"
#include "Engine/StaticMesh.h"
#include "Junctions/TrafficJunction.h"
#include "Materials/MaterialInterface.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TrafficLaneFollower.h"

ATrafficDemoSceneBuilder::ATrafficDemoSceneBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    // Defaults are resolved here so a freshly placed builder produces a
    // complete scene without any assets having to be assigned by hand. Every
    // one of these remains overridable in the details panel.

    // A cube rather than a plane: the road surface is extruded along the
    // spline, so a solid shape gives the carriageway real thickness.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
        TEXT("/Engine/BasicShapes/Cube.Cube"));

    if (CubeMeshFinder.Succeeded())
    {
        RoadSurfaceMesh = CubeMeshFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    if (SphereMeshFinder.Succeeded())
    {
        SignalMesh = SphereMeshFinder.Object;
    }

    // Placeholder only. No road surface material exists in the project yet,
    // so this falls back to the engine's plain shape material; point
    // RoadSurfaceMaterial at the real asset once it is saved.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        RoadMaterialFinder(
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    if (RoadMaterialFinder.Succeeded())
    {
        RoadSurfaceMaterial = RoadMaterialFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        RedMaterialFinder(TEXT("/Game/Materials/Red.Red"));

    if (RedMaterialFinder.Succeeded())
    {
        RedSignalMaterial = RedMaterialFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        YellowMaterialFinder(TEXT("/Game/Materials/Yellow.Yellow"));

    if (YellowMaterialFinder.Succeeded())
    {
        YellowSignalMaterial = YellowMaterialFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        GreenMaterialFinder(TEXT("/Game/Materials/Green.Green"));

    if (GreenMaterialFinder.Succeeded())
    {
        GreenSignalMaterial = GreenMaterialFinder.Object;
    }
}

ATrafficRoad* ATrafficDemoSceneBuilder::SpawnRoad(
    const TArray<FVector>& WorldPoints,
    bool bRoadClosedLoop)
{
    UWorld* World = GetWorld();

    if (!World || WorldPoints.Num() < 2)
    {
        return nullptr;
    }

    ATrafficRoad* Road = World->SpawnActor<ATrafficRoad>();

    if (!IsValid(Road))
    {
        return nullptr;
    }

    Road->SetActorLocation(FVector::ZeroVector);
    Road->SetLaneCount(LaneCount);
    Road->SetRoadSurface(RoadSurfaceMesh, RoadSurfaceMaterial);
    Road->SetSplinePoints(WorldPoints, bRoadClosedLoop);

    SpawnedActors.Add(Road);

    return Road;
}

void ATrafficDemoSceneBuilder::SetTotalVehicleCount(int32 NewCount)
{
    TotalVehicleCount = FMath::Max(NewCount, 0);
}

ATrafficRoadNetwork* ATrafficDemoSceneBuilder::GetBuiltNetwork() const
{
    return IsValid(RoadNetwork) ? RoadNetwork : SpawnedNetwork;
}

int32 ATrafficDemoSceneBuilder::GetRoadVehicleCapacity(
    ATrafficRoad* Road) const
{
    if (!IsValid(Road))
    {
        return 0;
    }

    const int32 LaneCountOnRoad = Road->GetLaneCount();

    if (LaneCountOnRoad <= 0)
    {
        return 0;
    }

    const FTrafficLaneHandle LaneHandle = Road->GetLaneHandle(0);

    float LaneLengthCm = 0.0f;

    if (!LaneHandle.IsValid() ||
        !Road->GetLaneLength(LaneHandle, LaneLengthCm))
    {
        return 0;
    }

    const int32 PerLane = FMath::FloorToInt(
        LaneLengthCm / FMath::Max(MinVehicleSpacingCm, 1.0f));

    return FMath::Max(PerLane, 0) * LaneCountOnRoad;
}

ATrafficRoadNetwork* ATrafficDemoSceneBuilder::ResolveNetwork()
{
    if (IsValid(RoadNetwork))
    {
        return RoadNetwork;
    }

    UWorld* World = GetWorld();

    if (!World)
    {
        return nullptr;
    }

    if (!IsValid(SpawnedNetwork))
    {
        SpawnedNetwork = World->SpawnActor<ATrafficRoadNetwork>();

        if (IsValid(SpawnedNetwork))
        {
            SpawnedActors.Add(SpawnedNetwork);
        }
    }

    return SpawnedNetwork;
}

void ATrafficDemoSceneBuilder::SpawnVehiclesOnRoad(
    ATrafficRoad* Road,
    ATrafficRoadNetwork* Network,
    int32 VehicleCount)
{
    if (!IsValid(Road) || !IsValid(VehicleClass) || VehicleCount <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }

    const int32 LaneCountOnRoad = Road->GetLaneCount();

    if (LaneCountOnRoad <= 0)
    {
        return;
    }

    for (int32 VehicleIndex = 0;
        VehicleIndex < VehicleCount;
        ++VehicleIndex)
    {
        const int32 LaneIndexToUse = VehicleIndex % LaneCountOnRoad;

        const FTrafficLaneHandle LaneHandle =
            Road->GetLaneHandle(LaneIndexToUse);

        float LaneLengthCm = 0.0f;

        if (!LaneHandle.IsValid() ||
            !Road->GetLaneLength(LaneHandle, LaneLengthCm) ||
            LaneLengthCm <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        // Spread vehicles evenly along the lane instead of stacking them at
        // distance zero.
        const float StartingDistanceCm =
            LaneLengthCm *
            static_cast<float>(VehicleIndex + 1) /
            static_cast<float>(VehicleCount + 1);

        FTransform SpawnTransform = Road->GetActorTransform();
        FTransform LaneTransform;

        if (Road->EvaluateLaneAtDistance(
            LaneHandle,
            StartingDistanceCm,
            LaneTransform))
        {
            SpawnTransform = LaneTransform;
        }

        ATrafficLaneFollower* NewVehicle =
            World->SpawnActorDeferred<ATrafficLaneFollower>(
                VehicleClass,
                SpawnTransform);

        if (!IsValid(NewVehicle))
        {
            continue;
        }

        const float SpeedCmPerSecond = FMath::FRandRange(
            MinVehicleSpeedCmPerSecond,
            MaxVehicleSpeedCmPerSecond);

        // Configuring before FinishSpawning means BeginPlay sees the real
        // starting lane instead of the class defaults.
        NewVehicle->ConfigureStart(
            Road,
            LaneIndexToUse,
            StartingDistanceCm,
            SpeedCmPerSecond,
            Network);

        NewVehicle->FinishSpawning(SpawnTransform);

        SpawnedActors.Add(NewVehicle);
    }
}

void ATrafficDemoSceneBuilder::ClearDemoScene()
{
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }

    SpawnedActors.Reset();
    SpawnedNetwork = nullptr;
}

void ATrafficDemoSceneBuilder::BuildDemoScene()
{
    ClearDemoScene();

    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }

    ATrafficRoadNetwork* Network = ResolveNetwork();

    if (!IsValid(Network))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s could not resolve a road network."),
            *GetName());

        return;
    }

    Network->MaximumConnectionDistanceCm = ConnectionToleranceCm;
    Network->SetConnectLastRoadToFirst(false);

    const FVector Origin = GetActorLocation();

    // Index order doubles as compass order and as the approach index each
    // junction assigns, which the signal phases key off: 0 and 2 are the
    // opposing pair on one axis, 1 and 3 on the other.
    const FVector Directions[4] =
    {
        FVector(1.0f, 0.0f, 0.0f),
        FVector(0.0f, 1.0f, 0.0f),
        FVector(-1.0f, 0.0f, 0.0f),
        FVector(0.0f, -1.0f, 0.0f)
    };

    const int32 Columns = FMath::Max(GridColumns, 1);
    const int32 Rows = FMath::Max(GridRows, 1);

    // Held back from each junction centre so the junction is a box with real
    // area rather than a single point every approach converges on.
    const float SafeInsetCm = FMath::Clamp(
        JunctionApproachInsetCm,
        0.0f,
        FMath::Max(JunctionRadiusCm - 100.0f, 0.0f));

    auto GetJunctionLocation =
        [&](int32 Column, int32 Row) -> FVector
        {
            return Origin +
                FVector(
                    (Column - (Columns - 1) * 0.5f) * JunctionSpacingCm,
                    (Row - (Rows - 1) * 0.5f) * JunctionSpacingCm,
                    0.0f);
        };

    auto JunctionIndex =
        [&](int32 Column, int32 Row)
        {
            return Row * Columns + Column;
        };

    // Slotted by direction rather than by creation order. Approach index has
    // to mean the same compass direction at every junction, or a signal phase
    // of {0, 2} stops selecting an opposing pair: interior roads are created
    // by whichever junction reaches them first, so appending as we go leaves
    // neighbouring junctions with differently ordered approaches.
    TArray<ATrafficRoad*> ApproachByJunctionAndDirection;
    ApproachByJunctionAndDirection.SetNumZeroed(Columns * Rows * 4);

    auto SetApproach =
        [&](int32 Column, int32 Row, int32 DirectionIndex, ATrafficRoad* Road)
        {
            ApproachByJunctionAndDirection[
                JunctionIndex(Column, Row) * 4 + DirectionIndex] = Road;
        };

    // Outer ends of the stub roads, which the perimeter ring will join up.
    TArray<TPair<FVector, ATrafficRoad*>> StubEnds;

    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            const FVector JunctionLocation =
                GetJunctionLocation(Column, Row);

            for (int32 DirectionIndex = 0;
                DirectionIndex < 4;
                ++DirectionIndex)
            {
                const FVector& Direction = Directions[DirectionIndex];

                const int32 NeighbourColumn =
                    Column + FMath::RoundToInt(Direction.X);

                const int32 NeighbourRow =
                    Row + FMath::RoundToInt(Direction.Y);

                const bool bNeighbourExists =
                    NeighbourColumn >= 0 && NeighbourColumn < Columns &&
                    NeighbourRow >= 0 && NeighbourRow < Rows;

                const FVector InnerPoint =
                    JunctionLocation + Direction * SafeInsetCm;

                if (bNeighbourExists)
                {
                    // Interior roads are shared, so only the +X and +Y passes
                    // create one; the neighbour picks up the same road from
                    // its own opposite direction.
                    if (Direction.X < 0.0f || Direction.Y < 0.0f)
                    {
                        continue;
                    }

                    const FVector NeighbourInnerPoint =
                        GetJunctionLocation(NeighbourColumn, NeighbourRow) -
                        Direction * SafeInsetCm;

                    ATrafficRoad* InteriorRoad = SpawnRoad(
                        { InnerPoint, NeighbourInnerPoint },
                        false);

                    if (InteriorRoad)
                    {
                        SetApproach(
                            Column,
                            Row,
                            DirectionIndex,
                            InteriorRoad);

                        // The same road arrives at the neighbour from the
                        // opposite compass direction.
                        SetApproach(
                            NeighbourColumn,
                            NeighbourRow,
                            (DirectionIndex + 2) % 4,
                            InteriorRoad);
                    }

                    continue;
                }

                const FVector OuterPoint =
                    JunctionLocation + Direction * SpurLengthCm;

                ATrafficRoad* Stub = SpawnRoad(
                    { InnerPoint, OuterPoint },
                    false);

                if (Stub)
                {
                    SetApproach(Column, Row, DirectionIndex, Stub);

                    StubEnds.Emplace(OuterPoint, Stub);
                }
            }
        }
    }

    // Interior roads appear against both their junctions; AddRoad ignores
    // repeats.
    for (ATrafficRoad* Road : ApproachByJunctionAndDirection)
    {
        Network->AddRoad(Road);
    }

    // Walking the stub ends by bearing around the grid centre gives their
    // order around the perimeter, for any grid size.
    StubEnds.Sort(
        [Origin](
            const TPair<FVector, ATrafficRoad*>& Left,
            const TPair<FVector, ATrafficRoad*>& Right)
        {
            return FMath::Atan2(
                Left.Key.Y - Origin.Y,
                Left.Key.X - Origin.X) <
                FMath::Atan2(
                    Right.Key.Y - Origin.Y,
                    Right.Key.X - Origin.X);
        });

    for (int32 Index = 0; Index < StubEnds.Num(); ++Index)
    {
        const int32 NextIndex = (Index + 1) % StubEnds.Num();

        if (NextIndex == Index)
        {
            break;
        }

        const FVector& Start = StubEnds[Index].Key;
        const FVector& End = StubEnds[NextIndex].Key;

        // Bowed outward from the grid centre so the ring rounds off rather
        // than cutting straight across its own corners.
        const FVector Midpoint = (Start + End) * 0.5f;

        const FVector OutwardDirection =
            (Midpoint - Origin).GetSafeNormal2D();

        const FVector ControlPoint =
            Midpoint +
            OutwardDirection *
            FVector::Dist(Start, End) * PerimeterBulgeFraction;

        ATrafficRoad* PerimeterLink = SpawnRoad(
            { Start, ControlPoint, End },
            false);

        if (!PerimeterLink)
        {
            continue;
        }

        Network->AddRoad(PerimeterLink);

        Network->ConnectRoads(StubEnds[Index].Value, PerimeterLink);
        Network->ConnectRoads(PerimeterLink, StubEnds[NextIndex].Value);
    }

    TArray<ATrafficJunction*> Junctions;

    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            ATrafficJunction* Junction =
                World->SpawnActor<ATrafficJunction>();

            if (!IsValid(Junction))
            {
                continue;
            }

            Junction->SetActorLocation(GetJunctionLocation(Column, Row));
            Junction->RoadNetwork = Network;
            Junction->SetJunctionRadiusCm(JunctionRadiusCm);

            // Added strictly in direction order, so approach index 0 is
            // always +X, 1 is +Y, 2 is -X and 3 is -Y. That is what lets the
            // signal phases below name opposing pairs as {0, 2} and {1, 3}
            // and have it hold at every junction in the grid.
            for (int32 DirectionIndex = 0;
                DirectionIndex < 4;
                ++DirectionIndex)
            {
                ATrafficRoad* Road = ApproachByJunctionAndDirection[
                    JunctionIndex(Column, Row) * 4 + DirectionIndex];

                if (Road)
                {
                    Junction->ApproachRoads.Add(Road);
                }
            }

            if (bUseTrafficSignals)
            {
                TArray<FTrafficSignalPhase> Phases;

                FTrafficSignalPhase FirstAxis;
                FirstAxis.GreenApproachIndices = { 0, 2 };
                FirstAxis.GreenDurationSeconds = 8.0f;
                FirstAxis.ClearanceDurationSeconds = 2.0f;
                Phases.Add(FirstAxis);

                FTrafficSignalPhase SecondAxis;
                SecondAxis.GreenApproachIndices = { 1, 3 };
                SecondAxis.GreenDurationSeconds = 8.0f;
                SecondAxis.ClearanceDurationSeconds = 2.0f;
                Phases.Add(SecondAxis);

                Junction->ConfigureSignals(true, Phases);
            }

            SpawnedActors.Add(Junction);
            Junctions.Add(Junction);

            // Registers the junction with the network, then builds its
            // connectors; RebuildJunction's own rebuild is what makes the
            // network pick up the approach and departure links.
            Network->AddJunction(Junction);
            Junction->RebuildJunction();

            if (bUseTrafficSignals)
            {
                // Applied after RebuildJunction so the indicator placement
                // pass has real connectors to place lights against.
                Junction->SetSignalVisuals(
                    SignalMesh,
                    RedSignalMaterial,
                    YellowSignalMaterial,
                    GreenSignalMaterial);
            }
        }
    }

    if (IsValid(VehicleClass))
    {
        TArray<ATrafficRoad*> AllRoads;

        for (AActor* Actor : SpawnedActors)
        {
            if (ATrafficRoad* Road = Cast<ATrafficRoad>(Actor))
            {
                AllRoads.Add(Road);
            }
        }

        if (TotalVehicleCount > 0)
        {
            // Share the requested population out across the network in
            // proportion to how much room each road actually has, so no lane
            // is asked to hold more vehicles than fit.
            int32 TotalCapacity = 0;

            for (ATrafficRoad* Road : AllRoads)
            {
                TotalCapacity += GetRoadVehicleCapacity(Road);
            }

            if (TotalCapacity <= 0)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("%s built a network with no room for vehicles."),
                    *GetName());
            }
            else
            {
                if (TotalVehicleCount > TotalCapacity)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT(
                            "%s was asked for %d vehicles but the network "
                            "only holds %d; spawning %d."),
                        *GetName(),
                        TotalVehicleCount,
                        TotalCapacity,
                        TotalCapacity);
                }

                const int32 TargetCount =
                    FMath::Min(TotalVehicleCount, TotalCapacity);

                int32 RemainingToSpawn = TargetCount;
                int32 RemainingCapacity = TotalCapacity;

                for (ATrafficRoad* Road : AllRoads)
                {
                    const int32 RoadCapacity =
                        GetRoadVehicleCapacity(Road);

                    if (RoadCapacity <= 0 || RemainingCapacity <= 0)
                    {
                        continue;
                    }

                    const int32 Share = FMath::Min(
                        RemainingToSpawn,
                        FMath::RoundToInt(
                            static_cast<float>(RemainingToSpawn) *
                            RoadCapacity / RemainingCapacity));

                    SpawnVehiclesOnRoad(Road, Network, Share);

                    RemainingToSpawn -= Share;
                    RemainingCapacity -= RoadCapacity;
                }
            }
        }
        else
        {
            // Stub roads take the spur count, everything else the ring count.
            for (ATrafficRoad* Road : AllRoads)
            {
                const bool bIsStub = StubEnds.ContainsByPredicate(
                    [Road](const TPair<FVector, ATrafficRoad*>& Entry)
                    {
                        return Entry.Value == Road;
                    });

                SpawnVehiclesOnRoad(
                    Road,
                    Network,
                    bIsStub ? VehiclesPerSpur : VehiclesPerRingSegment);
            }
        }
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "%s has no VehicleClass assigned; the scene was built "
                "without vehicles."),
            *GetName());
    }

    UE_LOG(
        LogTemp,
        Display,
        TEXT("%s built a %dx%d grid: %d junctions, %d roads."),
        *GetName(),
        Columns,
        Rows,
        Junctions.Num(),
        Network->GetConnectionCount());

    if (bSpawnDebugOverlay)
    {
        ATrafficDebugOverlay* Overlay =
            World->SpawnActor<ATrafficDebugOverlay>();

        if (IsValid(Overlay))
        {
            Overlay->SetActorLocation(Origin);
            Overlay->RoadNetwork = Network;

            SpawnedActors.Add(Overlay);
        }
    }

    if (bSpawnCongestionExperiment)
    {
        ATrafficCongestionExperiment* Experiment =
            World->SpawnActor<ATrafficCongestionExperiment>();

        if (IsValid(Experiment))
        {
            Experiment->SetActorLocation(Origin);
            Experiment->RoadNetwork = Network;

            // On a grid the experiment drives the centre junction, which is
            // the one whose congestion propagates furthest in every
            // direction.
            Experiment->Junction = Junctions.Num() > 0
                ? Junctions[Junctions.Num() / 2]
                : nullptr;

            SpawnedActors.Add(Experiment);
        }
    }
}
