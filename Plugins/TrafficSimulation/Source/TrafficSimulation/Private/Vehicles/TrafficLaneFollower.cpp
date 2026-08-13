#include "Vehicles/TrafficLaneFollower.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TrafficRoad.h"
#include "RoadNetwork/TrafficRoadNetwork.h"


ATrafficLaneFollower::ATrafficLaneFollower()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot =
		CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));

	SetRootComponent(SceneRoot);

	VehicleMesh =
		CreateDefaultSubobject<UStaticMeshComponent>(
			TEXT("VehicleMesh"));

	VehicleMesh->SetupAttachment(SceneRoot);
	VehicleMesh->SetCollisionEnabled(
		ECollisionEnabled::NoCollision);
}

void ATrafficLaneFollower::BeginPlay()
{
	Super::BeginPlay();

	bLaneInitialized = InitializeLane();

	if (!bLaneInitialized)
	{
		SetActorTickEnabled(false);
		return;
	}

	UpdateTransform();
}

bool ATrafficLaneFollower::InitializeLane()
{
	if (!IsValid(Road))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s has no TrafficRoad assigned."),
			*GetName());

		return false;
	}

	LaneHandle = Road->GetLaneHandle(LaneIndex);

	if (!LaneHandle.IsValid())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s has invalid lane index %d."),
			*GetName(),
			LaneIndex);

		return false;
	}

	if (!Road->GetLaneLength(
		LaneHandle,
		LaneLengthCm))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s could not resolve its lane length."),
			*GetName());

		return false;
	}

	DistanceAlongLaneCm = StartingDistanceCm;

	if (Road->IsRoadClosedLoop() ||
		OpenRoadEndBehavior == ETrafficLaneEndBehavior::Loop)
	{
		DistanceAlongLaneCm =
			FMath::Fmod(DistanceAlongLaneCm, LaneLengthCm);

		if (DistanceAlongLaneCm < 0.0f)
		{
			DistanceAlongLaneCm += LaneLengthCm;
		}
	}
	else
	{
		DistanceAlongLaneCm = FMath::Clamp(
			DistanceAlongLaneCm,
			0.0f,
			LaneLengthCm);
	}

	return true;
}

void ATrafficLaneFollower::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bLaneInitialized)
	{
		return;
	}

	AdvanceAlongLane(DeltaSeconds);

	if (IsActorBeingDestroyed())
	{
		return;
	}

	UpdateTransform();
}

void ATrafficLaneFollower::AdvanceAlongLane(
	float DeltaSeconds)
{
	DistanceAlongLaneCm +=
		SpeedCmPerSecond * DeltaSeconds;

	// Closed roads always wrap locally.
	if (Road->IsRoadClosedLoop())
	{
		DistanceAlongLaneCm =
			FMath::Fmod(
				DistanceAlongLaneCm,
				LaneLengthCm);

		if (DistanceAlongLaneCm < 0.0f)
		{
			DistanceAlongLaneCm += LaneLengthCm;
		}

		return;
	}

	if (SpeedCmPerSecond >= 0.0f)
	{
		if (DistanceAlongLaneCm < LaneLengthCm)
		{
			return;
		}

		const float OverflowDistanceCm =
			DistanceAlongLaneCm - LaneLengthCm;

		// Network connections take priority over fallback behaviour.
		if (TryTransitionToNextLane(
			OverflowDistanceCm))
		{
			return;
		}
	}
	else
	{
		// Connections currently model Exit -> Entry travel only.
		if (DistanceAlongLaneCm > 0.0f)
		{
			return;
		}
	}

	// The open lane has ended without a valid network connection.
	switch (OpenRoadEndBehavior)
	{
	case ETrafficLaneEndBehavior::Loop:
	{
		DistanceAlongLaneCm =
			FMath::Fmod(
				DistanceAlongLaneCm,
				LaneLengthCm);

		if (DistanceAlongLaneCm < 0.0f)
		{
			DistanceAlongLaneCm += LaneLengthCm;
		}

		break;
	}

	case ETrafficLaneEndBehavior::Destroy:
	{
		Destroy();
		break;
	}

	case ETrafficLaneEndBehavior::Stop:
	default:
	{
		DistanceAlongLaneCm = FMath::Clamp(
			DistanceAlongLaneCm,
			0.0f,
			LaneLengthCm);

		SpeedCmPerSecond = 0.0f;
		break;
	}
	}
}

void ATrafficLaneFollower::UpdateTransform()
{
	FTransform LaneTransform;

	if (!Road->EvaluateLaneAtDistance(
		LaneHandle,
		DistanceAlongLaneCm,
		LaneTransform))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s failed to evaluate its lane."),
			*GetName());

		bLaneInitialized = false;
		SetActorTickEnabled(false);
		return;
	}

	const FVector HeightOffset =
		LaneTransform.GetUnitAxis(EAxis::Z) *
		HeightOffsetCm;

	LaneTransform.AddToTranslation(HeightOffset);

	SetActorTransform(
		LaneTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

bool ATrafficLaneFollower::TryTransitionToNextLane(
	float OverflowDistanceCm)
{
	if (!IsValid(RoadNetwork))
	{
		return false;
	}

	FTrafficLaneHandle NextLaneHandle;

	if (!RoadNetwork->FindNextLane(
		LaneHandle,
		NextLaneHandle))
	{
		return false;
	}

	ATrafficRoad* NextRoad =
		RoadNetwork->FindRoad(
			NextLaneHandle.RoadId);

	if (!IsValid(NextRoad))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"%s found the next lane but could "
				"not resolve its road."),
			*GetName());

		return false;
	}

	float NextLaneLengthCm = 0.0f;

	if (!NextRoad->GetLaneLength(
		NextLaneHandle,
		NextLaneLengthCm))
	{
		return false;
	}

	Road = NextRoad;
	LaneHandle = NextLaneHandle;
	LaneIndex = NextLaneHandle.LaneIndex;
	LaneLengthCm = NextLaneLengthCm;

	DistanceAlongLaneCm = FMath::Clamp(
		OverflowDistanceCm,
		0.0f,
		LaneLengthCm);

	return true;
}