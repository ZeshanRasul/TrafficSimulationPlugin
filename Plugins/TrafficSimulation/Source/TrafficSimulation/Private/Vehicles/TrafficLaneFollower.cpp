#include "Vehicles/TrafficLaneFollower.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Junctions/TrafficJunction.h"
#include "Materials/MaterialInterface.h"
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

	// Wheels hang off the body rather than the root, so swapping or rescaling
	// the body carries them with it instead of leaving them behind.
	auto CreateWheel =
		[this](const TCHAR* ComponentName) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* Wheel =
				CreateDefaultSubobject<UStaticMeshComponent>(ComponentName);

			if (Wheel)
			{
				Wheel->SetupAttachment(VehicleMesh);
				Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			return Wheel;
		};

	WheelFrontLeft = CreateWheel(TEXT("WheelFrontLeft"));
	WheelFrontRight = CreateWheel(TEXT("WheelFrontRight"));
	WheelRearLeft = CreateWheel(TEXT("WheelRearLeft"));
	WheelRearRight = CreateWheel(TEXT("WheelRearRight"));

	// Modular kit parts are authored in place, so the pieces line up at the
	// identity transform and only need assigning.
	struct FPartDefault
	{
		UStaticMeshComponent* Component;
		const TCHAR* AssetPath;
	};

	const FPartDefault PartDefaults[] =
	{
		{ VehicleMesh, TEXT("/Game/Models/body.body") },
		{
			WheelFrontLeft,
			TEXT("/Game/Models/wheel-front-left.wheel-front-left")
		},
		{
			WheelFrontRight,
			TEXT("/Game/Models/wheel-front-right.wheel-front-right")
		},
		{
			WheelRearLeft,
			TEXT("/Game/Models/wheel-back-left.wheel-back-left")
		},
		{
			WheelRearRight,
			TEXT("/Game/Models/wheel-back-right.wheel-back-right")
		}
	};

	for (const FPartDefault& Part : PartDefaults)
	{
		if (!Part.Component)
		{
			continue;
		}

		if (UStaticMesh* Mesh = Cast<UStaticMesh>(
			StaticLoadObject(
				UStaticMesh::StaticClass(),
				nullptr,
				Part.AssetPath)))
		{
			Part.Component->SetStaticMesh(Mesh);
		}
	}

	// Weights are a rough town-traffic mix: mostly ordinary cars, a scattering
	// of vans, and the occasional lorry or police car. Speed multipliers make
	// the heavier types hold up the traffic behind them.
	struct FVariantDefault
	{
		const TCHAR* Folder;
		float Weight;
		float SpeedMultiplier;
	};

	const FVariantDefault VariantDefaults[] =
	{
		{ TEXT("Sedan"), 30.0f, 1.00f },
		{ TEXT("Hatchback"), 26.0f, 1.00f },
		{ TEXT("SUV"), 16.0f, 0.97f },
		{ TEXT("Taxi"), 9.0f, 1.02f },
		{ TEXT("Delivery"), 8.0f, 0.88f },
		{ TEXT("Truck"), 5.0f, 0.80f },
		{ TEXT("Police"), 3.0f, 1.05f },
		{ TEXT("GarbageTruck"), 3.0f, 0.72f }
	};

	auto LoadVehiclePart =
		[](const TCHAR* Folder, const TCHAR* PartName) -> UStaticMesh*
		{
			const FString Path = FString::Printf(
				TEXT("/Game/Models/Vehicles/%s/%s.%s"),
				Folder,
				PartName,
				PartName);

			return Cast<UStaticMesh>(
				StaticLoadObject(
					UStaticMesh::StaticClass(),
					nullptr,
					*Path));
		};

	for (const FVariantDefault& Default : VariantDefaults)
	{
		UStaticMesh* Body = LoadVehiclePart(Default.Folder, TEXT("body"));

		// A folder that has not been populated yet simply contributes
		// nothing, and starts appearing once its meshes are imported.
		if (!Body)
		{
			continue;
		}

		FTrafficVehicleVariant& Variant =
			VehicleVariants.AddDefaulted_GetRef();

		Variant.Name = FName(Default.Folder);
		Variant.BodyMesh = Body;
		Variant.SelectionWeight = Default.Weight;
		Variant.SpeedMultiplier = Default.SpeedMultiplier;

		Variant.WheelFrontLeftMesh =
			LoadVehiclePart(Default.Folder, TEXT("wheel-front-left"));

		Variant.WheelFrontRightMesh =
			LoadVehiclePart(Default.Folder, TEXT("wheel-front-right"));

		Variant.WheelRearLeftMesh =
			LoadVehiclePart(Default.Folder, TEXT("wheel-back-left"));

		Variant.WheelRearRightMesh =
			LoadVehiclePart(Default.Folder, TEXT("wheel-back-right"));
	}
}

void ATrafficLaneFollower::BeginPlay()
{
	Super::BeginPlay();

	ApplyMeshVariant();
	ApplyMeshTransform();

	bLaneInitialized = InitializeLane();

	if (!bLaneInitialized)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (IsValid(RoadNetwork))
	{
		RoadNetwork->RegisterVehicle(this);
	}

	UpdateTransform();
}

void ATrafficLaneFollower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseJunctionReservation();

	if (IsValid(RoadNetwork))
	{
		RoadNetwork->UnregisterVehicle(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ATrafficLaneFollower::ApplyMeshVariant()
{
	if (!IsValid(VehicleMesh) || VehicleVariants.Num() == 0)
	{
		return;
	}

	// Weighted draw, so the mix can be made to look like real traffic rather
	// than an even spread of refuse lorries and police cars.
	float TotalWeight = 0.0f;

	for (const FTrafficVehicleVariant& Variant : VehicleVariants)
	{
		if (Variant.IsValid())
		{
			TotalWeight += FMath::Max(Variant.SelectionWeight, 0.0f);
		}
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	float Roll = FMath::FRandRange(0.0f, TotalWeight);

	const FTrafficVehicleVariant* Chosen = nullptr;

	for (const FTrafficVehicleVariant& Variant : VehicleVariants)
	{
		if (!Variant.IsValid())
		{
			continue;
		}

		Roll -= FMath::Max(Variant.SelectionWeight, 0.0f);

		if (Roll <= 0.0f)
		{
			Chosen = &Variant;
			break;
		}
	}

	if (!Chosen)
	{
		return;
	}

	VehicleMesh->SetStaticMesh(Chosen->BodyMesh);

	// Wheels travel with the body: a lorry's wheels do not fit a hatchback,
	// and leaving the previous set in place would show through the new shell.
	const TPair<UStaticMeshComponent*, UStaticMesh*> WheelAssignments[] =
	{
		{ WheelFrontLeft, Chosen->WheelFrontLeftMesh },
		{ WheelFrontRight, Chosen->WheelFrontRightMesh },
		{ WheelRearLeft, Chosen->WheelRearLeftMesh },
		{ WheelRearRight, Chosen->WheelRearRightMesh }
	};

	for (const TPair<UStaticMeshComponent*, UStaticMesh*>& Assignment :
		WheelAssignments)
	{
		if (Assignment.Key && Assignment.Value)
		{
			Assignment.Key->SetStaticMesh(Assignment.Value);
		}
	}

	// Heavier types travel slower, which is what produces overtaking pressure
	// and queues behind them rather than uniformly flowing traffic.
	SpeedCmPerSecond *= FMath::Max(Chosen->SpeedMultiplier, 0.1f);
}

void ATrafficLaneFollower::ApplyMeshTransform()
{
	if (!IsValid(VehicleMesh))
	{
		return;
	}

	// Applies to whatever mesh is in place, whether it came from a variant or
	// straight from the Blueprint. Tying this to variant selection meant a
	// single-mesh vehicle could never be reoriented at all.
	UStaticMesh* CurrentMesh = VehicleMesh->GetStaticMesh();

	if (!CurrentMesh)
	{
		return;
	}

	VehicleMesh->SetRelativeRotation(MeshRotationOffset);

	// Longest horizontal axis is taken as the vehicle's length, whichever way
	// round the asset was authored.
	const FVector MeshSize =
		CurrentMesh->GetBounds().BoxExtent * 2.0f;

	const float LongestAxisCm = FMath::Max(MeshSize.X, MeshSize.Y);

	float FinalScale = MeshScale;

	if (bScaleMeshToVehicleLength && LongestAxisCm > KINDA_SMALL_NUMBER)
	{
		FinalScale *= VehicleLengthCm / LongestAxisCm;
	}

	VehicleMesh->SetRelativeScale3D(FVector(FinalScale));

	if (bGroundMeshToLane)
	{
		// The lowest point of the mesh in its own space, which is only zero
		// if the asset happens to be authored sitting on the origin.
		const float LocalBottomZ =
			CurrentMesh->GetBounds().Origin.Z -
			CurrentMesh->GetBounds().BoxExtent.Z;

		VehicleMesh->SetRelativeLocation(
			FVector(0.0f, 0.0f, -LocalBottomZ * FinalScale));
	}

	// Taken from the mesh rather than imposed on it, so the gap the follower
	// keeps behind a lorry reflects the lorry actually being longer. Confined
	// to play, because this writes to an editable property and doing it on
	// every construction would overwrite the value in the details panel.
	const UWorld* World = GetWorld();

	if (bDeriveVehicleLengthFromMesh &&
		!bScaleMeshToVehicleLength &&
		LongestAxisCm > KINDA_SMALL_NUMBER &&
		World &&
		World->IsGameWorld())
	{
		VehicleLengthCm = LongestAxisCm * FinalScale;
	}
}

void ATrafficLaneFollower::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Runs in the editor too, so orientation and scale can be judged against
	// the viewport instead of by starting a run each time. Variant selection
	// is deliberately excluded: it is random, and rerunning it on every
	// construction would reshuffle the mesh while properties are being edited.
	ApplyMeshTransform();
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

	// Timed so the benchmark can report simulation cost rather than frame
	// time, which says nothing while a frame rate cap is active.
	const double TickStartSeconds = FPlatformTime::Seconds();

	UpdatePendingSuccessor();
	UpdateSpeed(DeltaSeconds);
	AdvanceAlongLane(DeltaSeconds);

	// After movement, so the reported lane and distance match where the
	// vehicle actually ended the frame.
	UpdateDebugState();

	const bool bDestroyed = IsActorBeingDestroyed();

	if (!bDestroyed)
	{
		UpdateTransform();
	}

	if (IsValid(RoadNetwork))
	{
		RoadNetwork->AddSimulationTimeSeconds(
			FPlatformTime::Seconds() - TickStartSeconds);
	}
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

float ATrafficLaneFollower::GetStopLineSetbackCm() const
{
	return StopLineBufferCm + VehicleLengthCm * 0.5f;
}

void ATrafficLaneFollower::UpdateSpeed(float DeltaSeconds)
{
	float TargetSpeedCmPerSecond = SpeedCmPerSecond;

	ETrafficVehicleConstraint Constraint =
		ETrafficVehicleConstraint::FreeFlow;

	if (IsYieldingToJunction() && SpeedCmPerSecond > 0.0f)
	{
		const float DistanceToLaneEndCm =
			LaneLengthCm - DistanceAlongLaneCm;

		// Ask for entry as late as possible while still leaving room to brake,
		// so the junction is not reserved further ahead than necessary. The
		// range must reach at least as far back as the stop line itself,
		// otherwise a vehicle that has already halted there falls outside it
		// and stops asking to proceed.
		const float BrakingDistanceCm =
			FMath::Square(CurrentSpeedCmPerSecond) /
			(2.0f * FMath::Max(BrakingCmPerSecondSquared, 1.0f));

		if (DistanceToLaneEndCm <=
			BrakingDistanceCm + GetStopLineSetbackCm())
		{
			bEntryGranted = PendingJunction->RequestEntry(
				this,
				PendingConnectorIndex);

			if (!bEntryGranted)
			{
				TargetSpeedCmPerSecond = 0.0f;
				Constraint = ETrafficVehicleConstraint::YieldingToJunction;
			}
		}
	}

	DebugState.ForwardGapCm = -1.0f;
	DebugState.Leader = nullptr;

	// Car-following: never let this vehicle close on whatever is ahead of it,
	// whether that is still on this lane or has already crossed onto the
	// successor lane (a queue at a junction spans that boundary).
	if (IsValid(RoadNetwork))
	{
		const FTrafficLaneHandle* NextLaneHandle =
			bPendingSuccessorValid ? &PendingSuccessor.Lane : nullptr;

		float ForwardGapCm = 0.0f;
		ATrafficLaneFollower* Leader = nullptr;

		if (RoadNetwork->FindForwardGapCm(
			this,
			LaneHandle,
			DistanceAlongLaneCm,
			LaneLengthCm,
			NextLaneHandle,
			ForwardGapCm,
			Leader))
		{
			DebugState.ForwardGapCm = ForwardGapCm;
			DebugState.Leader = Leader;

			// Space available to use up before reaching the standstill gap.
			const float UsableGapCm =
				ForwardGapCm - MinFollowingGapCm;

			float FollowingSpeedCmPerSecond = 0.0f;

			if (UsableGapCm > 0.0f)
			{
				const float LeaderSpeedCmPerSecond =
					IsValid(Leader)
					? Leader->GetCurrentSpeedCmPerSecond()
					: 0.0f;

				// Fastest speed from which this vehicle could still pull up
				// short of wherever the leader can stop. Because it accounts
				// for the leader's own speed, a platoon running at a steady
				// speed is not slowed merely for being close, while closing
				// on a stopped queue brakes in good time.
				const float SafeSpeedCmPerSecond = FMath::Sqrt(
					FMath::Square(LeaderSpeedCmPerSecond) +
					2.0f *
					FMath::Max(BrakingCmPerSecondSquared, 1.0f) *
					UsableGapCm);

				// Comfort limit: hold a gap proportional to speed. This is
				// normally the binding constraint, which keeps spacing even
				// and stops the queue surging back and forth.
				const float HeadwaySpeedCmPerSecond =
					UsableGapCm /
					FMath::Max(DesiredTimeHeadwaySeconds, 0.1f);

				FollowingSpeedCmPerSecond = FMath::Min(
					SafeSpeedCmPerSecond,
					HeadwaySpeedCmPerSecond);
			}

			// Whichever constraint bites hardest is the one worth reporting.
			if (FollowingSpeedCmPerSecond < TargetSpeedCmPerSecond)
			{
				TargetSpeedCmPerSecond = FollowingSpeedCmPerSecond;
				Constraint = ETrafficVehicleConstraint::Following;
			}
		}
	}

	const float RateCmPerSecondSquared =
		TargetSpeedCmPerSecond > CurrentSpeedCmPerSecond
		? AccelerationCmPerSecondSquared
		: BrakingCmPerSecondSquared;

	const float PreviousSpeedCmPerSecond = CurrentSpeedCmPerSecond;

	CurrentSpeedCmPerSecond = FMath::FInterpConstantTo(
		CurrentSpeedCmPerSecond,
		TargetSpeedCmPerSecond,
		DeltaSeconds,
		RateCmPerSecondSquared);

	DebugState.TargetSpeedCmPerSecond = TargetSpeedCmPerSecond;
	DebugState.Constraint = Constraint;

	DebugState.bAccelerating =
		CurrentSpeedCmPerSecond > PreviousSpeedCmPerSecond + KINDA_SMALL_NUMBER;

	DebugState.bBraking =
		CurrentSpeedCmPerSecond < PreviousSpeedCmPerSecond - KINDA_SMALL_NUMBER;
}

void ATrafficLaneFollower::UpdateDebugState()
{
	DebugState.CurrentSpeedCmPerSecond = CurrentSpeedCmPerSecond;
	DebugState.DesiredSpeedCmPerSecond = SpeedCmPerSecond;
	DebugState.CurrentLane = LaneHandle;
	DebugState.DistanceAlongLaneCm = DistanceAlongLaneCm;
	DebugState.LaneLengthCm = LaneLengthCm;

	DebugState.bHasNextLane = bPendingSuccessorValid;
	DebugState.NextLane = bPendingSuccessorValid
		? PendingSuccessor.Lane
		: FTrafficLaneHandle();

	DebugState.NextTurnType = bPendingSuccessorValid
		? PendingSuccessor.TurnType
		: ETrafficTurnType::Straight;

	DebugState.bNextEntersJunction =
		bPendingSuccessorValid && PendingSuccessor.EntersJunction();

	DebugState.bJunctionEntryGranted = bEntryGranted;

	DebugState.PendingSignalState = ETrafficSignalState::None;

	if (IsValid(PendingJunction) && PendingConnectorIndex != INDEX_NONE)
	{
		FTrafficConnectorLane Connector;

		if (PendingJunction->GetConnector(PendingConnectorIndex, Connector))
		{
			DebugState.PendingSignalState =
				PendingJunction->GetApproachSignalState(
					Connector.ApproachIndex);
		}
	}

	// A vehicle stopped at the end of an open lane with nowhere to go is
	// reported as such rather than as an unexplained free-flow stop.
	if (DebugState.Constraint == ETrafficVehicleConstraint::FreeFlow &&
		!bPendingSuccessorValid &&
		CurrentSpeedCmPerSecond <= StoppedSpeedThresholdCmPerSecond &&
		DistanceAlongLaneCm >= LaneLengthCm - KINDA_SMALL_NUMBER)
	{
		DebugState.Constraint = ETrafficVehicleConstraint::LaneEnd;
	}

	if (CurrentSpeedCmPerSecond <= StoppedSpeedThresholdCmPerSecond)
	{
		DebugState.MotionState = ETrafficVehicleMotionState::Stopped;
	}
	else if (SpeedCmPerSecond > KINDA_SMALL_NUMBER &&
		CurrentSpeedCmPerSecond >= SpeedCmPerSecond * FreeFlowSpeedFraction)
	{
		DebugState.MotionState = ETrafficVehicleMotionState::FreeFlow;
	}
	else
	{
		DebugState.MotionState = ETrafficVehicleMotionState::Constrained;
	}

	if (!IsValid(VehicleMesh))
	{
		return;
	}

	if (bHasAppliedMotionState &&
		AppliedMotionState == DebugState.MotionState)
	{
		return;
	}

	UMaterialInterface* Material = nullptr;

	switch (DebugState.MotionState)
	{
	case ETrafficVehicleMotionState::FreeFlow:
		Material = FreeFlowMaterial;
		break;

	case ETrafficVehicleMotionState::Constrained:
		Material = ConstrainedMaterial;
		break;

	case ETrafficVehicleMotionState::Stopped:
	default:
		Material = StoppedMaterial;
		break;
	}

	if (Material)
	{
		VehicleMesh->SetMaterial(0, Material);
	}

	AppliedMotionState = DebugState.MotionState;
	bHasAppliedMotionState = true;
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
		const float StopDistanceCm = FMath::Max(
			0.0f,
			LaneLengthCm - GetStopLineSetbackCm());

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

	// Keeps whatever scale the vehicle was set up with instead of forcing
	// one, so a car assembled from several meshes scales as a single unit.
	LaneTransform.SetScale3D(GetActorScale3D());

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
