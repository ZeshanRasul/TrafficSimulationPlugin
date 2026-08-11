#include "TrafficRoad.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"

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