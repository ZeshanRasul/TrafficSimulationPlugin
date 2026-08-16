#include "Demo/TrafficDemoSceneBuilder.h"

#include "Components/SceneComponent.h"
#include "Debug/TrafficDebugOverlay.h"
#include "Demo/TrafficCameraRig.h"
#include "Demo/TrafficCongestionExperiment.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
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

    // The imported road tile if it is present, so the scene comes up with
    // markings; otherwise a cube, which at least gives the carriageway real
    // thickness when extruded along the spline.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> RoadMeshFinder(
        TEXT("/Game/Models/road-straight.road-straight"));

    if (RoadMeshFinder.Succeeded())
    {
        RoadSurfaceMesh = RoadMeshFinder.Object;

        // The imported tile runs long on Y.
        RoadSurfaceForwardAxis = ESplineMeshAxis::Y;
    }
    else
    {
        static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
            TEXT("/Engine/BasicShapes/Cube.Cube"));

        if (CubeMeshFinder.Succeeded())
        {
            RoadSurfaceMesh = CubeMeshFinder.Object;
        }
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    if (SphereMeshFinder.Succeeded())
    {
        SignalMesh = SphereMeshFinder.Object;
    }

    // A four-way crossing, matching the four approaches every junction in the
    // grid has. Falls back to the junction's own default if not found.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CrossroadMeshFinder(
        TEXT("/Game/Models/road-crossroad.road-crossroad"));

    if (CrossroadMeshFinder.Succeeded())
    {
        JunctionSurfaceMesh = CrossroadMeshFinder.Object;
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

    // Building sets are resolved by name so the lists come pre-filled.
    // Anything missing is simply skipped, and both lists stay editable.
    const TCHAR* BuildingNames[] =
    {
        TEXT("b"), TEXT("c"), TEXT("d"), TEXT("e"), TEXT("f"),
        TEXT("g"), TEXT("h"), TEXT("i"), TEXT("j"), TEXT("k"),
        TEXT("l"), TEXT("m"), TEXT("n")
    };

    for (const TCHAR* Name : BuildingNames)
    {
        const FString Path = FString::Printf(
            TEXT("/Game/Models/Buildings/building-%s.building-%s"),
            Name,
            Name);

        if (UStaticMesh* Mesh = Cast<UStaticMesh>(
            StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path)))
        {
            BuildingMeshes.Add(Mesh);
        }
    }

    const TCHAR* SkyscraperNames[] =
    {
        TEXT("a"), TEXT("b"), TEXT("c"), TEXT("d"), TEXT("e")
    };

    for (const TCHAR* Name : SkyscraperNames)
    {
        const FString Path = FString::Printf(
            TEXT("/Game/Models/Buildings/building-skyscraper-%s")
            TEXT(".building-skyscraper-%s"),
            Name,
            Name);

        if (UStaticMesh* Mesh = Cast<UStaticMesh>(
            StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path)))
        {
            SkyscraperMeshes.Add(Mesh);
        }
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

    // Axis first: SetRoadSurface rebuilds the surface, and doing it in this
    // order avoids building it once with the wrong orientation.
    Road->SetRoadSurfaceOrientation(
        RoadSurfaceForwardAxis,
        RoadSurfaceRollDegrees);
    Road->SetRoadSurface(RoadSurfaceMesh, RoadSurfaceMaterial);

    Road->SetSplinePoints(WorldPoints, bRoadClosedLoop);

    RegisterSpawnedActor(Road);

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
            RegisterSpawnedActor(SpawnedNetwork);
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

        RegisterSpawnedActor(NewVehicle);
    }
}

bool ATrafficDemoSceneBuilder::SpawnBuildingOnPlot(
    const FVector& PlotCentre,
    float NormalisedDistanceFromCentre,
    float PlotSizeCm)
{
    UWorld* World = GetWorld();

    if (!World || SpawnedBuildingCount >= MaxBuildings)
    {
        return false;
    }

    // Towers cluster in the middle and thin out towards the edge, so the
    // skyline has a centre rather than being uniformly tall.
    const float TowerChance =
        SkyscraperCentreChance *
        (1.0f - FMath::Clamp(NormalisedDistanceFromCentre, 0.0f, 1.0f));

    const bool bUseTower =
        SkyscraperMeshes.Num() > 0 &&
        FMath::FRand() < TowerChance;

    const TArray<TObjectPtr<UStaticMesh>>& Pool =
        bUseTower ? SkyscraperMeshes : BuildingMeshes;

    if (Pool.Num() == 0)
    {
        return false;
    }

    UStaticMesh* Mesh = Pool[FMath::RandRange(0, Pool.Num() - 1)];

    if (!Mesh)
    {
        return false;
    }

    AStaticMeshActor* Building = World->SpawnActor<AStaticMeshActor>();

    if (!IsValid(Building))
    {
        return false;
    }

    UStaticMeshComponent* MeshComponent =
        Building->GetStaticMeshComponent();

    if (!MeshComponent)
    {
        Building->Destroy();
        return false;
    }

    // Movable avoids the static-mobility warnings that come from positioning
    // an actor immediately after spawning it.
    MeshComponent->SetMobility(EComponentMobility::Movable);
    MeshComponent->SetStaticMesh(Mesh);

    const FVector MeshSize = Mesh->GetBounds().BoxExtent * 2.0f;

    const float FootprintCm =
        FMath::Max(FMath::Max(MeshSize.X, MeshSize.Y), 1.0f);

    // Scaled from the mesh's own footprint, so a swapped-in asset set lands
    // at the right size without the plot layout being retuned.
    const float PlanScale =
        PlotSizeCm * BuildingPlotFillFraction / FootprintCm;

    const float HeightScale =
        PlanScale *
        (1.0f + FMath::FRandRange(
            -BuildingHeightVariation,
            BuildingHeightVariation));

    MeshComponent->SetWorldScale3D(
        FVector(PlanScale, PlanScale, FMath::Max(HeightScale, 0.05f)));

    // Quarter turns keep the blocks reading as a grid; a small jitter stops
    // them looking stamped.
    const float YawDegrees =
        90.0f * FMath::RandRange(0, 3) + FMath::FRandRange(-4.0f, 4.0f);

    Building->SetActorLocation(
        FVector(
            PlotCentre.X,
            PlotCentre.Y,
            PlotCentre.Z - Mesh->GetBounds().Origin.Z * PlanScale +
                MeshSize.Z * 0.5f * FMath::Max(HeightScale, 0.05f)));

    Building->SetActorRotation(FRotator(0.0f, YawDegrees, 0.0f));

    RegisterSpawnedActor(Building);
    ++SpawnedBuildingCount;

    return true;
}

void ATrafficDemoSceneBuilder::SpawnBuildings(
    const FVector& Origin,
    int32 Columns,
    int32 Rows,
    const FBox& NetworkBounds)
{
    SpawnedBuildingCount = 0;

    if (BuildingMeshes.Num() == 0 && SkyscraperMeshes.Num() == 0)
    {
        return;
    }

    const float RequestedPlotCm = FMath::Max(BuildingPlotSizeCm, 100.0f);

    const float GridHalfXCm = (Columns - 1) * 0.5f * JunctionSpacingCm;
    const float GridHalfYCm = (Rows - 1) * 0.5f * JunctionSpacingCm;

    const float GridRadiusCm =
        FMath::Max(
            FMath::Max(GridHalfXCm, GridHalfYCm),
            JunctionSpacingCm * 0.5f);

    if (bSpawnInteriorBuildings)
    {
        // Everything the ring encloses is available, not just the gaps
        // between junctions. Filling block by block leaves the strip between
        // the outermost junctions and the ring empty, along with the corners.
        // The stub ends mark where the ring runs, and it only ever bows
        // further out from there, so a box inset from them stays clear of it.
        const float RegionHalfXCm =
            GridHalfXCm + SpurLengthCm - BuildingRoadClearanceCm;

        const float RegionHalfYCm =
            GridHalfYCm + SpurLengthCm - BuildingRoadClearanceCm;

        if (RegionHalfXCm > 0.0f && RegionHalfYCm > 0.0f)
        {
            const int32 PlotsX = FMath::Max(
                FMath::RoundToInt(RegionHalfXCm * 2.0f / RequestedPlotCm),
                1);

            const int32 PlotsY = FMath::Max(
                FMath::RoundToInt(RegionHalfYCm * 2.0f / RequestedPlotCm),
                1);

            const float PlotXCm = RegionHalfXCm * 2.0f / PlotsX;
            const float PlotYCm = RegionHalfYCm * 2.0f / PlotsY;

            // The roads run along the junction rows and columns, so a plot is
            // on one exactly when it shares either coordinate with them.
            auto IsOnRoad =
                [&](const FVector& Point)
                {
                    for (int32 Column = 0; Column < Columns; ++Column)
                    {
                        const float RoadX =
                            Origin.X +
                            (Column - (Columns - 1) * 0.5f) *
                                JunctionSpacingCm;

                        if (FMath::Abs(Point.X - RoadX) <
                            BuildingRoadClearanceCm)
                        {
                            return true;
                        }
                    }

                    for (int32 Row = 0; Row < Rows; ++Row)
                    {
                        const float RoadY =
                            Origin.Y +
                            (Row - (Rows - 1) * 0.5f) * JunctionSpacingCm;

                        if (FMath::Abs(Point.Y - RoadY) <
                            BuildingRoadClearanceCm)
                        {
                            return true;
                        }
                    }

                    return false;
                };

            for (int32 PlotY = 0; PlotY < PlotsY; ++PlotY)
            {
                for (int32 PlotX = 0; PlotX < PlotsX; ++PlotX)
                {
                    const FVector PlotCentre(
                        Origin.X - RegionHalfXCm +
                            (PlotX + 0.5f) * PlotXCm,
                        Origin.Y - RegionHalfYCm +
                            (PlotY + 0.5f) * PlotYCm,
                        Origin.Z);

                    if (IsOnRoad(PlotCentre))
                    {
                        continue;
                    }

                    const float DistanceFromCentre =
                        FVector::Dist2D(PlotCentre, Origin);

                    SpawnBuildingOnPlot(
                        PlotCentre,
                        DistanceFromCentre / GridRadiusCm,
                        FMath::Min(PlotXCm, PlotYCm));
                }
            }
        }
    }

    if (!bSpawnExteriorBuildings || !NetworkBounds.IsValid)
    {
        return;
    }

    // Measured from where the roads actually are. The perimeter is a rounded
    // rectangle whose corners reach further than any circle centred on the
    // origin would suggest, so a radius test leaves buildings on the ring.
    const FBox ExclusionBounds =
        NetworkBounds.ExpandBy(
            FVector(
                BuildingRoadClearanceCm,
                BuildingRoadClearanceCm,
                0.0f));

    const FBox OuterBounds =
        ExclusionBounds.ExpandBy(
            FVector(
                RequestedPlotCm * ExteriorBuildingDepth,
                RequestedPlotCm * ExteriorBuildingDepth,
                0.0f));

    const int32 PlotsX = FMath::Max(
        FMath::CeilToInt(OuterBounds.GetSize().X / RequestedPlotCm),
        1);

    const int32 PlotsY = FMath::Max(
        FMath::CeilToInt(OuterBounds.GetSize().Y / RequestedPlotCm),
        1);

    for (int32 PlotY = 0; PlotY < PlotsY; ++PlotY)
    {
        for (int32 PlotX = 0; PlotX < PlotsX; ++PlotX)
        {
            const FVector PlotCentre(
                OuterBounds.Min.X + (PlotX + 0.5f) * RequestedPlotCm,
                OuterBounds.Min.Y + (PlotY + 0.5f) * RequestedPlotCm,
                Origin.Z);

            // Anything overlapping the roads, rather than merely near the
            // origin, is skipped.
            if (PlotCentre.X > ExclusionBounds.Min.X &&
                PlotCentre.X < ExclusionBounds.Max.X &&
                PlotCentre.Y > ExclusionBounds.Min.Y &&
                PlotCentre.Y < ExclusionBounds.Max.Y)
            {
                continue;
            }

            const float DistanceFromCentre =
                FVector::Dist2D(PlotCentre, Origin);

            SpawnBuildingOnPlot(
                PlotCentre,
                DistanceFromCentre / GridRadiusCm,
                RequestedPlotCm);
        }
    }
}

void ATrafficDemoSceneBuilder::EnsureBuilderId()
{
    if (!BuilderId.IsValid())
    {
        BuilderId = FGuid::NewGuid();
    }
}

void ATrafficDemoSceneBuilder::PostActorCreated()
{
    Super::PostActorCreated();

    EnsureBuilderId();
}

void ATrafficDemoSceneBuilder::PostLoad()
{
    Super::PostLoad();

    EnsureBuilderId();
}

void ATrafficDemoSceneBuilder::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);

    if (!bDuplicateForPIE)
    {
        // A copied builder must own a separate scene, or clearing one would
        // take the original's actors with it.
        BuilderId.Invalidate();
    }

    EnsureBuilderId();
}

FName ATrafficDemoSceneBuilder::GetSceneTag() const
{
    return FName(
        *FString::Printf(
            TEXT("TrafficDemoScene_%s"),
            *BuilderId.ToString(EGuidFormats::Digits)));
}

void ATrafficDemoSceneBuilder::RegisterSpawnedActor(AActor* Actor)
{
    if (!IsValid(Actor))
    {
        return;
    }

    Actor->Tags.AddUnique(GetSceneTag());

    SpawnedActors.Add(Actor);
}

void ATrafficDemoSceneBuilder::ClearDemoScene()
{
    EnsureBuilderId();

    UWorld* World = GetWorld();

    if (!World)
    {
        SpawnedActors.Reset();
        SpawnedNetwork = nullptr;

        return;
    }

    // Driven by the tag rather than the in-memory list, so a scene left over
    // from a previous editor session is still cleared rather than being built
    // on top of.
    const FName SceneTag = GetSceneTag();

    int32 DestroyedCount = 0;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;

        if (IsValid(Actor) && Actor->Tags.Contains(SceneTag))
        {
            Actor->Destroy();
            ++DestroyedCount;
        }
    }

    // Anything spawned this session that somehow missed the tag.
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
            ++DestroyedCount;
        }
    }

    SpawnedActors.Reset();
    SpawnedNetwork = nullptr;

    if (DestroyedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("%s cleared %d actors."),
            *GetName(),
            DestroyedCount);
    }
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

    // Every point any road passes through, used later to keep buildings off
    // the network. Accumulated from the real geometry rather than estimated.
    FBox NetworkBounds(ForceInit);

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

                    NetworkBounds += OuterPoint;
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

        // The control point bows outside the stub ends, so it sets the true
        // outer edge of the network.
        NetworkBounds += ControlPoint;

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

            RegisterSpawnedActor(Junction);
            Junctions.Add(Junction);

            // Registers the junction with the network, then builds its
            // connectors; RebuildJunction's own rebuild is what makes the
            // network pick up the approach and departure links.
            Network->AddJunction(Junction);

            // Set before the rebuild so the crossing is built with the right
            // mesh and material in one pass.
            Junction->SetSurfaceVisuals(
                JunctionSurfaceMesh,
                RoadSurfaceMaterial);

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

    SpawnBuildings(Origin, Columns, Rows, NetworkBounds);

    if (bSpawnCameraRig)
    {
        ATrafficCameraRig* CameraRig =
            World->SpawnActor<ATrafficCameraRig>();

        if (IsValid(CameraRig))
        {
            CameraRig->SetActorLocation(Origin);
            CameraRig->RoadNetwork = Network;

            RegisterSpawnedActor(CameraRig);
        }
    }

    if (bSpawnDebugOverlay)
    {
        ATrafficDebugOverlay* Overlay =
            World->SpawnActor<ATrafficDebugOverlay>();

        if (IsValid(Overlay))
        {
            Overlay->SetActorLocation(Origin);
            Overlay->RoadNetwork = Network;

            RegisterSpawnedActor(Overlay);
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

            RegisterSpawnedActor(Experiment);
        }
    }
}
