#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TrafficRoad.h"
#include "Vehicles/TrafficLaneFollower.h"


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

    const bool bShouldLoop =
        Road->IsRoadClosedLoop() ||
        OpenRoadEndBehavior == ETrafficLaneEndBehavior::Loop;

    if (bShouldLoop)
    {
        DistanceAlongLaneCm =
            FMath::Fmod(DistanceAlongLaneCm, LaneLengthCm);

        if (DistanceAlongLaneCm < 0.0f)
        {
            DistanceAlongLaneCm += LaneLengthCm;
        }

        return;
    }

    const bool bPassedEnd =
        SpeedCmPerSecond >= 0.0f
        ? DistanceAlongLaneCm >= LaneLengthCm
        : DistanceAlongLaneCm <= 0.0f;

    if (!bPassedEnd)
    {
        return;
    }

    if (OpenRoadEndBehavior ==
        ETrafficLaneEndBehavior::Destroy)
    {
        Destroy();
        return;
    }

    DistanceAlongLaneCm = FMath::Clamp(
        DistanceAlongLaneCm,
        0.0f,
        LaneLengthCm);

    SpeedCmPerSecond = 0.0f;
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