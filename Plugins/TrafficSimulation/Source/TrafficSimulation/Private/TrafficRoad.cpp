#include "TrafficRoad.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"

ATrafficRoad::ATrafficRoad()
{
    PrimaryActorTick.bCanEverTick = true;

    RoadSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RoadSpline"));
    SetRootComponent(RoadSpline);

    RoadSpline->bDrawDebug = true;
}

void ATrafficRoad::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    EnsureRoadId();
    RoadSpline->SetClosedLoop(bClosedLoop);
    RebuildGeneratedLanes();
    RebuildRoadSurface();
}

void ATrafficRoad::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    DrawDebugLanes();
}

bool ATrafficRoad::ShouldTickIfViewportsOnly() const
{
    return true;
}

void ATrafficRoad::DrawDebugLanes() const
{
    if (!bDrawDebugLanes || !RoadSpline || !GetWorld())
    {
        return;
    }

    const float SplineLength = RoadSpline->GetSplineLength();

    if (SplineLength <= 0.0f)
    {
        return;
    }

    const float SafeSampleSpacing = FMath::Max(DebugSampleSpacingCm, 10.0f);

    const int32 SegmentCount = FMath::Max(
        1,
        FMath::CeilToInt(SplineLength / SafeSampleSpacing));

    for (int32 LaneIndex = 0; LaneIndex < LaneCount; ++LaneIndex)
    {
        const float CentredLaneIndex =
            static_cast<float>(LaneIndex) -
            (static_cast<float>(LaneCount - 1) * 0.5f);

        const float LateralOffset = CentredLaneIndex * LaneWidthCm;

        FVector PreviousLocation =
            RoadSpline->GetLocationAtDistanceAlongSpline(
                0.0f,
                ESplineCoordinateSpace::World);

        PreviousLocation +=
            RoadSpline->GetRightVectorAtDistanceAlongSpline(
                0.0f,
                ESplineCoordinateSpace::World) *
            LateralOffset;

        for (int32 SegmentIndex = 1;
            SegmentIndex <= SegmentCount;
            ++SegmentIndex)
        {
            const float Alpha =
                static_cast<float>(SegmentIndex) /
                static_cast<float>(SegmentCount);

            const float Distance = Alpha * SplineLength;

            FVector CurrentLocation =
                RoadSpline->GetLocationAtDistanceAlongSpline(
                    Distance,
                    ESplineCoordinateSpace::World);

            CurrentLocation +=
                RoadSpline->GetRightVectorAtDistanceAlongSpline(
                    Distance,
                    ESplineCoordinateSpace::World) *
                LateralOffset;

            DrawDebugLine(
                GetWorld(),
                PreviousLocation,
                CurrentLocation,
                FColor::Cyan,
                false,
                0.0f,
                0,
                3.0f);

            PreviousLocation = CurrentLocation;
        }
    }
}

const FGuid& ATrafficRoad::GetRoadId() const
{
    return RoadId;
}

FTrafficLaneHandle ATrafficRoad::GetLaneHandle(int32 LaneIndex) const
{
    FTrafficLaneHandle Handle;

    if (RoadId.IsValid() &&
        LaneIndex >= 0 &&
        LaneIndex < LaneCount)
    {
        Handle.RoadId = RoadId;
        Handle.LaneIndex = LaneIndex;
    }

    return Handle;
}

void ATrafficRoad::EnsureRoadId()
{
    if (!RoadId.IsValid())
    {
        RoadId = FGuid::NewGuid();
    }
}

void ATrafficRoad::PostActorCreated()
{
    Super::PostActorCreated();

    EnsureRoadId();
}

void ATrafficRoad::PostLoad()
{
    Super::PostLoad();

    EnsureRoadId();
}

void ATrafficRoad::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);

    if (bDuplicateForPIE)
    {
        EnsureRoadId();
    }
    else
    {
        RoadId = FGuid::NewGuid();
    }
}

ETrafficLaneDirection ATrafficRoad::DetermineLaneDirection(
    float LateralOffset) const
{
    if (FMath::IsNearlyZero(LateralOffset))
    {
        return ETrafficLaneDirection::Forward;
    }

    const bool bLaneIsOnRight = LateralOffset > 0.0f;

    const bool bTravelsForward =
        DrivingSide == ETrafficDrivingSide::Right
        ? !bLaneIsOnRight
        : bLaneIsOnRight;

    return bTravelsForward
        ? ETrafficLaneDirection::Forward
        : ETrafficLaneDirection::Reverse;
}

void ATrafficRoad::RebuildGeneratedLanes()
{
    GeneratedLanes.Reset();

    if (!RoadSpline || !RoadId.IsValid() || LaneCount <= 0)
    {
        return;
    }

    const float SplineLength = RoadSpline->GetSplineLength();

    if (SplineLength <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float SafeSampleSpacing =
        FMath::Max(LaneGenerationSettings.SampleSpacingCm, 10.0f);

    const int32 SegmentCount = FMath::Max(
        1,
        FMath::CeilToInt(SplineLength / SafeSampleSpacing));

    const int32 SampleCount = SegmentCount + 1;

    GeneratedLanes.Reserve(LaneCount);

    for (int32 LaneIndex = 0; LaneIndex < LaneCount; ++LaneIndex)
    {
        const float CentredLaneIndex =
            static_cast<float>(LaneIndex) -
            static_cast<float>(LaneCount - 1) * 0.5f;

        const float LateralOffset =
            CentredLaneIndex * LaneWidthCm;

        const ETrafficLaneDirection Direction =
            DetermineLaneDirection(LateralOffset);

        FTrafficLane& Lane = GeneratedLanes.AddDefaulted_GetRef();

        Lane.Handle = GetLaneHandle(LaneIndex);
        Lane.Direction = Direction;
        Lane.WidthCm = LaneWidthCm;
        Lane.LengthCm = SplineLength;
        Lane.Samples.Reserve(SampleCount);

        for (int32 SampleIndex = 0;
            SampleIndex < SampleCount;
            ++SampleIndex)
        {
            const float Alpha =
                static_cast<float>(SampleIndex) /
                static_cast<float>(SegmentCount);

            const float DistanceAlongLane =
                Alpha * SplineLength;

            const float DistanceAlongSpline =
                Direction == ETrafficLaneDirection::Forward
                ? DistanceAlongLane
                : SplineLength - DistanceAlongLane;

            const FVector SplineLocation =
                RoadSpline->GetLocationAtDistanceAlongSpline(
                    DistanceAlongSpline,
                    ESplineCoordinateSpace::World);

            const FVector SplineForward =
                RoadSpline->GetDirectionAtDistanceAlongSpline(
                    DistanceAlongSpline,
                    ESplineCoordinateSpace::World);

            const FVector SplineRight =
                RoadSpline->GetRightVectorAtDistanceAlongSpline(
                    DistanceAlongSpline,
                    ESplineCoordinateSpace::World);

            FTrafficLaneSample& Sample =
                Lane.Samples.AddDefaulted_GetRef();

            Sample.Location =
                SplineLocation + SplineRight * LateralOffset;

            Sample.Forward =
                Direction == ETrafficLaneDirection::Forward
                ? SplineForward
                : -SplineForward;

            Sample.Right =
                Direction == ETrafficLaneDirection::Forward
                ? SplineRight
                : -SplineRight;

            Sample.DistanceAlongLaneCm = DistanceAlongLane;
        }
    }
}

void ATrafficRoad::ClearRoadSurface()
{
    for (USplineMeshComponent* SurfaceComponent :
        RoadSurfaceComponents)
    {
        if (IsValid(SurfaceComponent))
        {
            SurfaceComponent->DestroyComponent();
        }
    }

    RoadSurfaceComponents.Reset();
}

void ATrafficRoad::RebuildRoadSurface()
{
    ClearRoadSurface();

    if (!RoadSpline || !RoadSurfaceMesh || LaneCount <= 0)
    {
        return;
    }

    const int32 SplinePointCount =
        RoadSpline->GetNumberOfSplinePoints();

    if (SplinePointCount < 2)
    {
        return;
    }

    const int32 SegmentCount =
        bClosedLoop
        ? SplinePointCount
        : SplinePointCount - 1;

    const float RoadWidthCm =
        static_cast<float>(LaneCount) * LaneWidthCm;

    const FVector MeshSize =
        RoadSurfaceMesh->GetBounds().BoxExtent * 2.0f;

    if (MeshSize.Y <= KINDA_SMALL_NUMBER ||
        MeshSize.Z <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector2D SurfaceScale(
        RoadWidthCm / MeshSize.Y,
        RoadThicknessCm / MeshSize.Z);

    RoadSurfaceComponents.Reserve(SegmentCount);

    for (int32 SegmentIndex = 0;
        SegmentIndex < SegmentCount;
        ++SegmentIndex)
    {
        const int32 StartPointIndex = SegmentIndex;

        const int32 EndPointIndex =
            (SegmentIndex + 1) % SplinePointCount;

        const FVector StartPosition =
            RoadSpline->GetLocationAtSplinePoint(
                StartPointIndex,
                ESplineCoordinateSpace::Local);

        const FVector StartTangent =
            RoadSpline->GetTangentAtSplinePoint(
                StartPointIndex,
                ESplineCoordinateSpace::Local);

        const FVector EndPosition =
            RoadSpline->GetLocationAtSplinePoint(
                EndPointIndex,
                ESplineCoordinateSpace::Local);

        const FVector EndTangent =
            RoadSpline->GetTangentAtSplinePoint(
                EndPointIndex,
                ESplineCoordinateSpace::Local);

        USplineMeshComponent* SurfaceComponent =
            NewObject<USplineMeshComponent>(this);

        if (!SurfaceComponent)
        {
            continue;
        }

        SurfaceComponent->SetFlags(RF_Transactional);

        SurfaceComponent->SetFlags(RF_Transactional);

        // Match mobility before establishing the attachment.
        SurfaceComponent->SetMobility(RoadSpline->Mobility);

        SurfaceComponent->SetupAttachment(RoadSpline);

        AddInstanceComponent(SurfaceComponent);

        SurfaceComponent->SetStaticMesh(RoadSurfaceMesh);        SurfaceComponent->SetForwardAxis(
            ESplineMeshAxis::X,
            false);

        SurfaceComponent->SetStartAndEnd(
            StartPosition,
            StartTangent,
            EndPosition,
            EndTangent,
            false);

        SurfaceComponent->SetStartScale(
            SurfaceScale,
            false);

        SurfaceComponent->SetEndScale(
            SurfaceScale,
            false);

        if (RoadSurfaceMaterial)
        {
            SurfaceComponent->SetMaterial(
                0,
                RoadSurfaceMaterial);
        }

        SurfaceComponent->SetCollisionEnabled(
            ECollisionEnabled::NoCollision);

        SurfaceComponent->RegisterComponent();
        SurfaceComponent->UpdateMesh();

        RoadSurfaceComponents.Add(SurfaceComponent);
    }
}