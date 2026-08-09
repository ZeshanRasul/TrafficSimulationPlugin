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

    RoadSpline->SetClosedLoop(bClosedLoop);
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

