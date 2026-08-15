#include "Vehicles/TrafficLaneFollower.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Junctions/TrafficJunction.h"
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

void ATrafficLaneFollower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseJunctionReservation();

	Super::EndPlay(EndPlayReason);
}

void ATrafficLaneFollower::ConfigureStart(
	ATrafficRoad* InRoad,
	int32 InLaneIndex,
	float InStartingDistanceCm,
	float InSpeedCmPerSecond,
	ATrafficRoadNetwork* InRoadNetwork)
{
	Road = InRoad;
	LaneIndex = InLaneIndex;
	StartingDistanceCm = InStartingDistanceCm;
	SpeedCmPerSecond = InSpeedCmPerSecond;
	RoadNetwork = InRoadNetwork;
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

	// Vehicles always start on a road; junctions are only ever entered by
	// transitioning onto a connector lane.
	CurrentProvider.SetObject(Road);
	CurrentProvider.SetInterface(Cast<ITrafficLaneProvider>(Road));

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

	CurrentSpeedCmPerSecond = SpeedCmPerSecond;

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

	UpdatePendingSuccessor();
	UpdateSpeed(DeltaSeconds);
	AdvanceAlongLane(DeltaSeconds);

	if (IsActorBeingDestroyed())
	{
		return;
	}

	UpdateTransform();
}

void ATrafficLaneFollower::UpdatePendingSuccessor()
{
	// The choice is made once per lane and then held, so a random pick cannot
	// flicker between successors from frame to frame.
	if (bPendingSuccessorValid || !IsValid(RoadNetwork))
	{
		return;
	}

	// A closed-loop road can still feed a junction from its endpoint, so a
	// registered successor always takes priority over wrapping locally.
	// ChooseNextLane already returns false when none exists.
	FTrafficLaneSuccessor Choice;

	if (!RoadNetwork->ChooseNextLane(LaneHandle, Choice))
	{
		return;
	}

	PendingSuccessor = Choice;
	bPendingSuccessorValid = true;
	bEntryGranted = false;
	PendingJunction = nullptr;
	PendingConnectorIndex = INDEX_NONE;

	if (Choice.EntersJunction())
	{
		PendingJunction =
			RoadNetwork->FindJunction(Choice.JunctionId);

		PendingConnectorIndex = Choice.ConnectorIndex;
	}
}

bool ATrafficLaneFollower::IsYieldingToJunction() const
{
	return IsValid(PendingJunction) &&
		PendingConnectorIndex != INDEX_NONE &&
		!bEntryGranted;
}

void ATrafficLaneFollower::UpdateSpeed(float DeltaSeconds)
{
	bool bMustStop = false;

	if (IsYieldingToJunction() && SpeedCmPerSecond > 0.0f)
	{
		const float DistanceToLaneEndCm =
			LaneLengthCm - DistanceAlongLaneCm;

		// Ask for entry as late as possible while still leaving room to brake,
		// so the junction is not reserved further ahead than necessary.
		const float BrakingDistanceCm =
			FMath::Square(CurrentSpeedCmPerSecond) /
			(2.0f * FMath::Max(BrakingCmPerSecondSquared, 1.0f));

		if (DistanceToLaneEndCm <= BrakingDistanceCm + StopLineBufferCm)
		{
			bEntryGranted = PendingJunction->RequestEntry(
				this,
				PendingConnectorIndex);

			bMustStop = !bEntryGranted;
		}
	}

	const float TargetSpeedCmPerSecond =
		bMustStop ? 0.0f : SpeedCmPerSecond;

	const float RateCmPerSecondSquared =
		TargetSpeedCmPerSecond > CurrentSpeedCmPerSecond
		? AccelerationCmPerSecondSquared
		: BrakingCmPerSecondSquared;

	CurrentSpeedCmPerSecond = FMath::FInterpConstantTo(
		CurrentSpeedCmPerSecond,
		TargetSpeedCmPerSecond,
		DeltaSeconds,
		RateCmPerSecondSquared);
}

void ATrafficLaneFollower::AdvanceAlongLane(
	float DeltaSeconds)
{
	DistanceAlongLaneCm +=
		CurrentSpeedCmPerSecond * DeltaSeconds;

	const bool bProviderIsClosedLoop =
		CurrentProvider.GetInterface() &&
		CurrentProvider->IsRoadClosedLoop();

	// Held short of a junction that has not cleared this vehicle yet.
	if (IsYieldingToJunction())
	{
		const float StopDistanceCm =
			FMath::Max(0.0f, LaneLengthCm - StopLineBufferCm);

		if (DistanceAlongLaneCm >= StopDistanceCm)
		{
			DistanceAlongLaneCm = StopDistanceCm;
			CurrentSpeedCmPerSecond = 0.0f;
		}

		return;
	}

	if (CurrentSpeedCmPerSecond >= 0.0f)
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

	// No successor was available. A closed-loop road wraps on itself, taking
	// priority over the open-road-end fallback below.
	if (bProviderIsClosedLoop)
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
		ReleaseJunctionReservation();
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
		CurrentSpeedCmPerSecond = 0.0f;
		break;
	}
	}
}

void ATrafficLaneFollower::UpdateTransform()
{
	FTransform LaneTransform;

	if (!CurrentProvider.GetInterface() ||
		!CurrentProvider->EvaluateLaneAtDistance(
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

void ATrafficLaneFollower::ReleaseJunctionReservation()
{
	if (IsValid(ActiveJunction))
	{
		ActiveJunction->ReleaseEntry(this);
	}

	if (IsValid(PendingJunction))
	{
		PendingJunction->ReleaseEntry(this);
	}

	ActiveJunction = nullptr;
	PendingJunction = nullptr;
	PendingConnectorIndex = INDEX_NONE;
	bEntryGranted = false;
}

bool ATrafficLaneFollower::TryTransitionToNextLane(
	float OverflowDistanceCm)
{
	if (!IsValid(RoadNetwork) || !bPendingSuccessorValid)
	{
		return false;
	}

	// Entering a junction requires an explicit grant.
	if (IsValid(PendingJunction) && !bEntryGranted)
	{
		return false;
	}

	TScriptInterface<ITrafficLaneProvider> NextProvider =
		RoadNetwork->FindLaneProvider(PendingSuccessor.Lane.RoadId);

	if (!NextProvider.GetInterface())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"%s found the next lane but could "
				"not resolve its owner."),
			*GetName());

		return false;
	}

	float NextLaneLengthCm = 0.0f;

	if (!NextProvider->GetLaneLength(
		PendingSuccessor.Lane,
		NextLaneLengthCm))
	{
		return false;
	}

	// Leaving the junction box frees the conflicting movements behind us.
	if (IsValid(ActiveJunction))
	{
		ActiveJunction->ReleaseEntry(this);
	}

	ActiveJunction = PendingJunction;

	CurrentProvider = NextProvider;
	LaneHandle = PendingSuccessor.Lane;
	LaneIndex = PendingSuccessor.Lane.LaneIndex;
	LaneLengthCm = NextLaneLengthCm;

	// Keep the editor-facing Road pointer meaningful whenever the vehicle is
	// on an actual road rather than inside a junction.
	if (ATrafficRoad* NextRoad =
		Cast<ATrafficRoad>(NextProvider.GetObject()))
	{
		Road = NextRoad;
	}

	DistanceAlongLaneCm = FMath::Clamp(
		OverflowDistanceCm,
		0.0f,
		LaneLengthCm);

	// Force a fresh successor decision for the lane just entered.
	bPendingSuccessorValid = false;
	PendingJunction = nullptr;
	PendingConnectorIndex = INDEX_NONE;
	bEntryGranted = false;

	return true;
}
