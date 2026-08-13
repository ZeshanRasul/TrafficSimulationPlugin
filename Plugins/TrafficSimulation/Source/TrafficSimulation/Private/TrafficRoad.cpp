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
	SetRoadClosedLoop(bClosedLoop);
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
	if (!bDrawDebugLanes || !GetWorld())
	{
		return;
	}

	const FVector HeightOffset =
		FVector::UpVector * DebugHeightOffsetCm;

	for (const FTrafficLane& Lane : GeneratedLanes)
	{
		if (Lane.Samples.Num() < 2)
		{
			continue;
		}

		const FColor LaneColor =
			Lane.Direction == ETrafficLaneDirection::Forward
			? FColor::Green
			: FColor::Orange;

		for (int32 SampleIndex = 1;
			SampleIndex < Lane.Samples.Num();
			++SampleIndex)
		{
			const FTrafficLaneSample& PreviousSample =
				Lane.Samples[SampleIndex - 1];

			const FTrafficLaneSample& CurrentSample =
				Lane.Samples[SampleIndex];

			DrawDebugLine(
				GetWorld(),
				PreviousSample.Location + HeightOffset,
				CurrentSample.Location + HeightOffset,
				LaneColor,
				false,
				0.0f,
				0,
				4.0f);
		}

		if (!bDrawLaneDirections)
		{
			continue;
		}

		for (const FTrafficLaneSample& Sample : Lane.Samples)
		{
			const FVector ArrowStart =
				Sample.Location + HeightOffset;

			const FVector ArrowEnd =
				ArrowStart +
				Sample.Forward * DirectionArrowLengthCm;

			DrawDebugDirectionalArrow(
				GetWorld(),
				ArrowStart,
				ArrowEnd,
				50.0f,
				LaneColor,
				false,
				0.0f,
				0,
				3.0f);
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
		Lane.LateralOffsetCm = LateralOffset;

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
		RoadSpline->IsClosedLoop()
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

		SurfaceComponent->CreationMethod = EComponentCreationMethod::UserConstructionScript;

		// Match mobility before establishing the attachment.
		SurfaceComponent->SetMobility(RoadSpline->Mobility);

		SurfaceComponent->SetupAttachment(RoadSpline);

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

bool ATrafficRoad::EvaluateLaneAtDistance(
	FTrafficLaneHandle LaneHandle,
	float DistanceAlongLaneCm,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	if (!LaneHandle.IsValid() ||
		LaneHandle.RoadId != RoadId ||
		!GeneratedLanes.IsValidIndex(LaneHandle.LaneIndex))
	{
		return false;
	}

	const FTrafficLane& Lane =
		GeneratedLanes[LaneHandle.LaneIndex];

	if (Lane.Samples.Num() < 2 ||
		Lane.LengthCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float SafeDistanceAlongLane = 0.0f;

	if (RoadSpline->IsClosedLoop())
	{
		SafeDistanceAlongLane =
			FMath::Fmod(DistanceAlongLaneCm, Lane.LengthCm);

		if (SafeDistanceAlongLane < 0.0f)
		{
			SafeDistanceAlongLane += Lane.LengthCm;
		}
	}
	else
	{
		SafeDistanceAlongLane = FMath::Clamp(
			DistanceAlongLaneCm,
			0.0f,
			Lane.LengthCm);
	}

	const float DistanceAlongSpline =
		Lane.Direction == ETrafficLaneDirection::Forward
		? SafeDistanceAlongLane
		: Lane.LengthCm - SafeDistanceAlongLane;

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

	const FVector Location =
		SplineLocation +
		SplineRight * Lane.LateralOffsetCm;

	const FVector Forward =
		Lane.Direction == ETrafficLaneDirection::Forward
		? SplineForward
		: -SplineForward;

	const FVector Right =
		Lane.Direction == ETrafficLaneDirection::Forward
		? SplineRight
		: -SplineRight;

	const FVector Up =
		FVector::CrossProduct(Forward, Right).GetSafeNormal();

	const FQuat Rotation =
		FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();

	OutTransform = FTransform(
		Rotation,
		Location,
		FVector::OneVector);

	return true;
}

bool ATrafficRoad::GetLaneLength(
	FTrafficLaneHandle LaneHandle,
	float& OutLengthCm) const
{
	OutLengthCm = 0.0f;

	if (!LaneHandle.IsValid() ||
		LaneHandle.RoadId != RoadId ||
		!GeneratedLanes.IsValidIndex(LaneHandle.LaneIndex))
	{
		return false;
	}

	const FTrafficLane& Lane =
		GeneratedLanes[LaneHandle.LaneIndex];

	if (Lane.LengthCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutLengthCm = Lane.LengthCm;
	return true;
}

bool ATrafficRoad::IsRoadClosedLoop() const
{
	return RoadSpline && RoadSpline->IsClosedLoop();
}

void ATrafficRoad::SetRoadClosedLoop(bool bNewClosedLoop)
{
	if (!RoadSpline)
	{
		return;
	}

	bClosedLoop = bNewClosedLoop;
	RoadSpline->SetClosedLoop(bClosedLoop);

	RebuildGeneratedLanes();
	RebuildRoadSurface();
}

FTrafficRoadEndpointHandle ATrafficRoad::GetRoadEndpointHandle(
	ETrafficRoadEndpoint Endpoint) const
{
	FTrafficRoadEndpointHandle Handle;

	if (RoadId.IsValid())
	{
		Handle.RoadId = RoadId;
		Handle.Endpoint = Endpoint;
	}

	return Handle;
}

FTrafficLaneEndpointHandle ATrafficRoad::GetLaneEndpointHandle(
	int32 LaneIndex,
	ETrafficLaneEndpoint Endpoint) const
{
	FTrafficLaneEndpointHandle Handle;

	Handle.Lane = GetLaneHandle(LaneIndex);

	if (Handle.Lane.IsValid())
	{
		Handle.Endpoint = Endpoint;
	}

	return Handle;
}

bool ATrafficRoad::EvaluateRoadEndpoint(
	FTrafficRoadEndpointHandle EndpointHandle,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	if (!RoadSpline ||
		!EndpointHandle.IsValid() ||
		EndpointHandle.RoadId != RoadId)
	{
		return false;
	}

	const float DistanceAlongSpline =
		EndpointHandle.Endpoint ==
		ETrafficRoadEndpoint::Start
		? 0.0f
		: RoadSpline->GetSplineLength();

	OutTransform =
		RoadSpline->GetTransformAtDistanceAlongSpline(
			DistanceAlongSpline,
			ESplineCoordinateSpace::World,
			true);

	return true;
}

bool ATrafficRoad::EvaluateLaneEndpoint(
	FTrafficLaneEndpointHandle EndpointHandle,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	if (!EndpointHandle.IsValid() ||
		EndpointHandle.Lane.RoadId != RoadId)
	{
		return false;
	}

	float LaneLengthCm = 0.0f;

	if (!GetLaneLength(
		EndpointHandle.Lane,
		LaneLengthCm))
	{
		return false;
	}

	const float DistanceAlongLane =
		EndpointHandle.Endpoint ==
		ETrafficLaneEndpoint::Entry
		? 0.0f
		: LaneLengthCm;

	return EvaluateLaneAtDistance(
		EndpointHandle.Lane,
		DistanceAlongLane,
		OutTransform);
}

int32 ATrafficRoad::GetLaneCount() const
{
	return LaneCount;
}
