#include "Demo/TrafficDemoSceneBuilder.h"

#include "Components/SceneComponent.h"
#include "Debug/TrafficDebugOverlay.h"
#include "Engine/StaticMesh.h"
#include "Junctions/TrafficJunction.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TrafficLaneFollower.h"

ATrafficDemoSceneBuilder::ATrafficDemoSceneBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    // A shipped engine asset, so the ring is visible out of the box without
    // requiring a project-specific mesh to be assigned first.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(
        TEXT("/Engine/BasicShapes/Plane.Plane"));

    if (PlaneMeshFinder.Succeeded())
    {
        RoadSurfaceMesh = PlaneMeshFinder.Object;
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
    Network->SetConnectLastRoadToFirst(true);

    const FVector Center = GetActorLocation();

    // Index order doubles as compass order (N, E, S, W) and as the approach
    // index the junction later assigns each spur, which the signal phases
    // below key off of.
    const FVector Directions[4] =
    {
        FVector(1.0f, 0.0f, 0.0f),
        FVector(0.0f, 1.0f, 0.0f),
        FVector(-1.0f, 0.0f, 0.0f),
        FVector(0.0f, -1.0f, 0.0f)
    };

    FVector RingPoints[4];

    for (int32 Index = 0; Index < 4; ++Index)
    {
        RingPoints[Index] = Center + Directions[Index] * SpurLengthCm;
    }

    TArray<ATrafficRoad*> Spurs;
    Spurs.SetNum(4);

    for (int32 Index = 0; Index < 4; ++Index)
    {
        Spurs[Index] = SpawnRoad({ Center, RingPoints[Index] }, false);
    }

    TArray<ATrafficRoad*> Links;
    Links.SetNum(4);

    for (int32 Index = 0; Index < 4; ++Index)
    {
        const int32 NextIndex = (Index + 1) % 4;

        const FVector CornerDirection =
            (Directions[Index] + Directions[NextIndex]).GetSafeNormal();

        const FVector CornerPoint =
            Center + CornerDirection * RingCornerRadiusCm;

        Links[Index] = SpawnRoad(
            { RingPoints[Index], CornerPoint, RingPoints[NextIndex] },
            false);
    }

    // Roads are added in ring order (spur, link, spur, link, ...) so that
    // BuildSimpleConnections' adjacent-pair matching wires the whole loop:
    // every consecutive pair genuinely meets at a shared ring point, and
    // ConnectLastRoadToFirst closes the final link back to the first spur.
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (Spurs[Index])
        {
            Network->AddRoad(Spurs[Index]);
        }

        if (Links[Index])
        {
            Network->AddRoad(Links[Index]);
        }
    }

    Network->BuildSimpleConnections();

    ATrafficJunction* Junction = World->SpawnActor<ATrafficJunction>();

    if (IsValid(Junction))
    {
        Junction->SetActorLocation(Center);
        Junction->RoadNetwork = Network;
        Junction->SetJunctionRadiusCm(JunctionRadiusCm);

        for (ATrafficRoad* Spur : Spurs)
        {
            if (Spur)
            {
                Junction->ApproachRoads.Add(Spur);
            }
        }

        if (bUseTrafficSignals)
        {
            TArray<FTrafficSignalPhase> Phases;

            FTrafficSignalPhase NorthSouth;
            NorthSouth.GreenApproachIndices = { 0, 2 };
            NorthSouth.GreenDurationSeconds = 8.0f;
            NorthSouth.ClearanceDurationSeconds = 2.0f;
            Phases.Add(NorthSouth);

            FTrafficSignalPhase EastWest;
            EastWest.GreenApproachIndices = { 1, 3 };
            EastWest.GreenDurationSeconds = 8.0f;
            EastWest.ClearanceDurationSeconds = 2.0f;
            Phases.Add(EastWest);

            Junction->ConfigureSignals(true, Phases);
        }

        SpawnedActors.Add(Junction);

        // Registers the junction with the network, then builds its
        // connectors; RebuildJunction's own successful rebuild is what makes
        // the network pick up the junction's approach/departure links.
        Network->AddJunction(Junction);
        Junction->RebuildJunction();

        if (bUseTrafficSignals)
        {
            // Applied after RebuildJunction so the indicator placement pass
            // it triggers has real connectors to place lights against.
            Junction->SetSignalVisuals(
                SignalMesh,
                RedSignalMaterial,
                YellowSignalMaterial,
                GreenSignalMaterial);
        }
    }

    if (IsValid(VehicleClass))
    {
        for (ATrafficRoad* Spur : Spurs)
        {
            SpawnVehiclesOnRoad(Spur, Network, VehiclesPerSpur);
        }

        for (ATrafficRoad* Link : Links)
        {
            SpawnVehiclesOnRoad(Link, Network, VehiclesPerRingSegment);
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

    if (bSpawnDebugOverlay)
    {
        ATrafficDebugOverlay* Overlay =
            World->SpawnActor<ATrafficDebugOverlay>();

        if (IsValid(Overlay))
        {
            Overlay->SetActorLocation(Center);
            Overlay->RoadNetwork = Network;

            SpawnedActors.Add(Overlay);
        }
    }
}
