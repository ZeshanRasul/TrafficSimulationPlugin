#include "Demo/TrafficCameraRig.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Junctions/TrafficJunction.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"
#include "Vehicles/TrafficLaneFollower.h"

ATrafficCameraRig::ATrafficCameraRig()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));

    if (Camera)
    {
        Camera->SetupAttachment(SceneRoot);
    }

    CycleModeKey = EKeys::C;
    CycleTargetKey = EKeys::V;
}

void ATrafficCameraRig::BeginPlay()
{
    Super::BeginPlay();

    RefreshTargets();

    APlayerController* PlayerController =
        GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

    if (!PlayerController)
    {
        return;
    }

    if (bPossessOnBeginPlay)
    {
        PlayerController->SetViewTargetWithBlend(this, 0.0f);
    }

    if (!bBindKeys)
    {
        return;
    }

    EnableInput(PlayerController);

    if (InputComponent)
    {
        InputComponent->BindKey(
            CycleModeKey,
            IE_Pressed,
            this,
            &ATrafficCameraRig::CycleMode);

        InputComponent->BindKey(
            CycleTargetKey,
            IE_Pressed,
            this,
            &ATrafficCameraRig::CycleTarget);
    }
}

void ATrafficCameraRig::RefreshTargets()
{
    Junctions.Reset();

    if (!GetWorld())
    {
        return;
    }

    for (TActorIterator<ATrafficJunction> It(GetWorld()); It; ++It)
    {
        ATrafficJunction* Junction = *It;

        if (!IsValid(Junction))
        {
            continue;
        }

        if (!IsValid(RoadNetwork) || Junction->RoadNetwork == RoadNetwork)
        {
            Junctions.Add(Junction);
        }
    }
}

void ATrafficCameraRig::SetMode(ETrafficCameraMode NewMode)
{
    Mode = NewMode;

    // Chase starts snapped rather than sweeping in from the previous shot.
    bChaseInitialised = false;
}

void ATrafficCameraRig::CycleMode()
{
    switch (Mode)
    {
    case ETrafficCameraMode::Overview:
        SetMode(ETrafficCameraMode::JunctionOrbit);
        break;

    case ETrafficCameraMode::JunctionOrbit:
        SetMode(ETrafficCameraMode::ChaseVehicle);
        break;

    case ETrafficCameraMode::ChaseVehicle:
    default:
        SetMode(ETrafficCameraMode::Overview);
        break;
    }
}

void ATrafficCameraRig::CycleTarget()
{
    ++TargetIndex;

    // Chase picks a fresh vehicle; the others index into the junction list.
    if (Mode == ETrafficCameraMode::ChaseVehicle)
    {
        ChaseTarget = nullptr;
        bChaseInitialised = false;
    }
}

bool ATrafficCameraRig::TryGetNetworkBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    if (!GetWorld())
    {
        return false;
    }

    for (TActorIterator<ATrafficRoad> It(GetWorld()); It; ++It)
    {
        ATrafficRoad* Road = *It;

        if (!IsValid(Road))
        {
            continue;
        }

        const int32 LaneCount = Road->GetLaneCount();

        for (int32 LaneIndex = 0; LaneIndex < LaneCount; ++LaneIndex)
        {
            const FTrafficLaneHandle LaneHandle =
                Road->GetLaneHandle(LaneIndex);

            float LaneLengthCm = 0.0f;

            if (!LaneHandle.IsValid() ||
                !Road->GetLaneLength(LaneHandle, LaneLengthCm))
            {
                continue;
            }

            // Sampling the ends and middle is enough to bound a road without
            // walking every sample on every one of them.
            const float Distances[3] =
            {
                0.0f,
                LaneLengthCm * 0.5f,
                LaneLengthCm
            };

            for (const float Distance : Distances)
            {
                FTransform LaneTransform;

                if (Road->EvaluateLaneAtDistance(
                    LaneHandle,
                    Distance,
                    LaneTransform))
                {
                    OutBounds += LaneTransform.GetLocation();
                }
            }
        }
    }

    return OutBounds.IsValid != 0;
}

void ATrafficCameraRig::UpdateOverview(float DeltaSeconds)
{
    FBox NetworkBounds;

    FVector Centre = GetActorLocation();
    float RadiusCm = 5000.0f;

    if (TryGetNetworkBounds(NetworkBounds))
    {
        Centre = NetworkBounds.GetCenter();

        RadiusCm = FMath::Max(
            NetworkBounds.GetExtent().Size2D(),
            100.0f);
    }

    OrbitAngleDegrees += OverviewYawSpeed * DeltaSeconds;

    const float PitchRadians =
        FMath::DegreesToRadians(
            FMath::Clamp(OverviewPitchDegrees, 5.0f, 89.0f));

    // Far enough back that the network's radius fits inside the vertical
    // field of view, with a margin so it is not framed edge to edge.
    const float HalfFovRadians =
        FMath::DegreesToRadians(
            Camera ? Camera->FieldOfView * 0.5f : 45.0f);

    const float DistanceCm =
        RadiusCm * OverviewFitMargin /
        FMath::Max(FMath::Tan(HalfFovRadians), 0.05f);

    const float YawRadians =
        FMath::DegreesToRadians(OrbitAngleDegrees);

    const FVector Offset(
        FMath::Cos(YawRadians) * FMath::Cos(PitchRadians) * DistanceCm,
        FMath::Sin(YawRadians) * FMath::Cos(PitchRadians) * DistanceCm,
        FMath::Sin(PitchRadians) * DistanceCm);

    SetActorLocation(Centre + Offset);

    SetActorRotation(
        (Centre - (Centre + Offset)).Rotation());
}

void ATrafficCameraRig::UpdateJunctionOrbit(float DeltaSeconds)
{
    if (Junctions.Num() == 0)
    {
        RefreshTargets();
    }

    if (Junctions.Num() == 0)
    {
        UpdateOverview(DeltaSeconds);
        return;
    }

    ATrafficJunction* Junction =
        Junctions[FMath::Abs(TargetIndex) % Junctions.Num()];

    if (!IsValid(Junction))
    {
        RefreshTargets();
        return;
    }

    OrbitAngleDegrees += OrbitYawSpeed * DeltaSeconds;

    const float YawRadians =
        FMath::DegreesToRadians(OrbitAngleDegrees);

    const FVector Centre = Junction->GetActorLocation();

    // Aimed at roughly where the traffic is rather than the road surface, so
    // the junction sits in frame instead of at the very bottom of it.
    const FVector LookAtPoint = Centre + FVector::UpVector * 150.0f;

    const FVector OrbitDirection(
        FMath::Cos(YawRadians),
        FMath::Sin(YawRadians),
        0.0f);

    float AllowedRadiusCm = OrbitRadiusCm;

    if (bAvoidObstructions && GetWorld())
    {
        const FVector DesiredLocation =
            Centre +
            OrbitDirection * OrbitRadiusCm +
            FVector::UpVector * OrbitHeightCm;

        FHitResult Hit;

        FCollisionQueryParams Params(SCENE_QUERY_STAT(TrafficOrbit), false);
        Params.AddIgnoredActor(this);
        Params.AddIgnoredActor(Junction);

        // Traced outward from the junction: the first thing standing between
        // the two is what the camera has to come in front of.
        if (GetWorld()->LineTraceSingleByChannel(
            Hit,
            LookAtPoint,
            DesiredLocation,
            ECC_Visibility,
            Params))
        {
            const float HitDistanceCm =
                FVector::Dist(LookAtPoint, Hit.ImpactPoint);

            // Converted back into an orbit radius, since the trace runs along
            // the diagonal while the radius is measured horizontally.
            const float DiagonalLengthCm =
                FMath::Sqrt(
                    FMath::Square(OrbitRadiusCm) +
                    FMath::Square(OrbitHeightCm));

            const float Fraction =
                DiagonalLengthCm > KINDA_SMALL_NUMBER
                ? FMath::Clamp(
                    (HitDistanceCm - ObstructionPaddingCm) /
                        DiagonalLengthCm,
                    0.1f,
                    1.0f)
                : 1.0f;

            AllowedRadiusCm = OrbitRadiusCm * Fraction;
        }
    }

    if (CurrentOrbitRadiusCm <= KINDA_SMALL_NUMBER)
    {
        CurrentOrbitRadiusCm = AllowedRadiusCm;
    }

    // Closing in happens quickly so the camera never ends up inside a wall;
    // opening back out is deliberately slower so the recovery is not a snap.
    const float InterpSpeed =
        AllowedRadiusCm < CurrentOrbitRadiusCm
        ? ObstructionRecoverySpeed * 4.0f
        : ObstructionRecoverySpeed;

    CurrentOrbitRadiusCm = FMath::FInterpTo(
        CurrentOrbitRadiusCm,
        AllowedRadiusCm,
        DeltaSeconds,
        InterpSpeed);

    const float HeightScale =
        OrbitRadiusCm > KINDA_SMALL_NUMBER
        ? CurrentOrbitRadiusCm / OrbitRadiusCm
        : 1.0f;

    // Height comes in with the radius, keeping the camera on the same line of
    // sight rather than dropping it towards the road as it approaches.
    const FVector Location =
        Centre +
        OrbitDirection * CurrentOrbitRadiusCm +
        FVector::UpVector * OrbitHeightCm * HeightScale;

    SetActorLocation(Location);
    SetActorRotation((LookAtPoint - Location).Rotation());
}

void ATrafficCameraRig::UpdateChase(float DeltaSeconds)
{
    if (!ChaseTarget.IsValid())
    {
        if (!IsValid(RoadNetwork))
        {
            UpdateOverview(DeltaSeconds);
            return;
        }

        const TArray<TWeakObjectPtr<ATrafficLaneFollower>>& Vehicles =
            RoadNetwork->GetRegisteredVehicles();

        if (Vehicles.Num() == 0)
        {
            UpdateOverview(DeltaSeconds);
            return;
        }

        // Walks the list rather than picking at random, so pressing the key
        // repeatedly works through different vehicles instead of sometimes
        // landing on the same one.
        for (int32 Attempt = 0; Attempt < Vehicles.Num(); ++Attempt)
        {
            const int32 Index =
                FMath::Abs(TargetIndex + Attempt) % Vehicles.Num();

            if (Vehicles[Index].IsValid())
            {
                ChaseTarget = Vehicles[Index];
                break;
            }
        }

        if (!ChaseTarget.IsValid())
        {
            UpdateOverview(DeltaSeconds);
            return;
        }
    }

    const ATrafficLaneFollower* Vehicle = ChaseTarget.Get();

    const FVector VehicleLocation = Vehicle->GetActorLocation();
    const FVector VehicleForward = Vehicle->GetActorForwardVector();

    const FVector DesiredLocation =
        VehicleLocation -
        VehicleForward * ChaseDistanceCm +
        FVector::UpVector * ChaseHeightCm;

    // Snapped on the first frame, then eased, so switching target does not
    // sweep the camera across the whole network.
    const FVector NewLocation = bChaseInitialised
        ? FMath::VInterpTo(
            GetActorLocation(),
            DesiredLocation,
            DeltaSeconds,
            ChaseLagSpeed)
        : DesiredLocation;

    bChaseInitialised = true;

    SetActorLocation(NewLocation);
    SetActorRotation((VehicleLocation - NewLocation).Rotation());
}

void ATrafficCameraRig::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    switch (Mode)
    {
    case ETrafficCameraMode::JunctionOrbit:
        UpdateJunctionOrbit(DeltaSeconds);
        break;

    case ETrafficCameraMode::ChaseVehicle:
        UpdateChase(DeltaSeconds);
        break;

    case ETrafficCameraMode::Overview:
    default:
        UpdateOverview(DeltaSeconds);
        break;
    }

    if (bShowStatus)
    {
        DrawStatus();
    }
}

void ATrafficCameraRig::DrawStatus() const
{
    if (!GEngine)
    {
        return;
    }

    const TCHAR* ModeName = TEXT("Overview");

    switch (Mode)
    {
    case ETrafficCameraMode::JunctionOrbit:
        ModeName = TEXT("Junction Orbit");
        break;

    case ETrafficCameraMode::ChaseVehicle:
        ModeName = TEXT("Chase Vehicle");
        break;

    default:
        break;
    }

    GEngine->AddOnScreenDebugMessage(
        StatusMessageKey,
        0.0f,
        FColor::Silver,
        FString::Printf(
            TEXT("CAMERA  %s   [%s] mode   [%s] target"),
            ModeName,
            *CycleModeKey.GetDisplayName().ToString(),
            *CycleTargetKey.GetDisplayName().ToString()));
}
