#include "Debug/TrafficDebugOverlay.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Junctions/TrafficJunction.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "Vehicles/TrafficLaneFollower.h"

namespace
{
    const TCHAR* LexTurnType(ETrafficTurnType TurnType)
    {
        switch (TurnType)
        {
        case ETrafficTurnType::Left:
            return TEXT("Left");

        case ETrafficTurnType::Right:
            return TEXT("Right");

        case ETrafficTurnType::UTurn:
            return TEXT("U-Turn");

        case ETrafficTurnType::Straight:
        default:
            return TEXT("Straight");
        }
    }

    const TCHAR* LexConstraint(ETrafficVehicleConstraint Constraint)
    {
        switch (Constraint)
        {
        case ETrafficVehicleConstraint::Following:
            return TEXT("FOLLOWING");

        case ETrafficVehicleConstraint::YieldingToJunction:
            return TEXT("YIELDING");

        case ETrafficVehicleConstraint::LaneEnd:
            return TEXT("LANE END");

        case ETrafficVehicleConstraint::FreeFlow:
        default:
            return TEXT("FREE");
        }
    }

    const TCHAR* LexSignal(ETrafficSignalState State)
    {
        switch (State)
        {
        case ETrafficSignalState::Red:
            return TEXT("RED");

        case ETrafficSignalState::Yellow:
            return TEXT("YELLOW");

        case ETrafficSignalState::Green:
            return TEXT("GREEN");

        case ETrafficSignalState::None:
        default:
            return TEXT("none");
        }
    }
}

ATrafficDebugOverlay::ATrafficDebugOverlay()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
    EditorIcon =
        CreateEditorOnlyDefaultSubobject<UBillboardComponent>(
            TEXT("EditorIcon"));

    if (EditorIcon)
    {
        EditorIcon->SetupAttachment(SceneRoot);
        EditorIcon->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
    }
#endif
}

bool ATrafficDebugOverlay::ShouldTickIfViewportsOnly() const
{
    return true;
}

FColor ATrafficDebugOverlay::GetMotionStateColour(
    ETrafficVehicleMotionState State)
{
    switch (State)
    {
    case ETrafficVehicleMotionState::FreeFlow:
        return FColor::Green;

    case ETrafficVehicleMotionState::Constrained:
        return FColor::Yellow;

    case ETrafficVehicleMotionState::Stopped:
    default:
        return FColor::Red;
    }
}

FTrafficNetworkStats ATrafficDebugOverlay::GetStats() const
{
    if (!IsValid(RoadNetwork))
    {
        return FTrafficNetworkStats();
    }

    return RoadNetwork->GetNetworkStats();
}

bool ATrafficDebugOverlay::TryGetViewLocation(FVector& OutViewLocation) const
{
    OutViewLocation = GetActorLocation();

    const UWorld* World = GetWorld();

    if (!World)
    {
        return false;
    }

    if (const APlayerController* PlayerController =
        World->GetFirstPlayerController())
    {
        FVector CameraLocation;
        FRotator CameraRotation;

        PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

        OutViewLocation = CameraLocation;
        return true;
    }

    return false;
}

void ATrafficDebugOverlay::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bEnabled || !IsValid(RoadNetwork) || !GetWorld())
    {
        return;
    }

    // Debug strings persist until cleared, so the previous frame's labels are
    // flushed before this frame's are queued.
    FlushDebugStrings(GetWorld());

    FVector ViewLocation;
    TryGetViewLocation(ViewLocation);

    for (const TWeakObjectPtr<ATrafficLaneFollower>& WeakVehicle :
        RoadNetwork->GetRegisteredVehicles())
    {
        const ATrafficLaneFollower* Vehicle = WeakVehicle.Get();

        if (IsValid(Vehicle))
        {
            DrawVehicle(Vehicle, ViewLocation);
        }
    }

    if (bShowSignalState)
    {
        DrawJunctions();
    }

    if (bShowSummary)
    {
        DrawSummary(ViewLocation);
    }
}

void ATrafficDebugOverlay::DrawVehicle(
    const ATrafficLaneFollower* Vehicle,
    const FVector& ViewLocation) const
{
    const FTrafficVehicleDebugState& State = Vehicle->GetDebugState();

    const FVector VehicleLocation = Vehicle->GetActorLocation();
    const FColor StateColour = GetMotionStateColour(State.MotionState);

    if (bShowVehicleStateBoxes)
    {
        const float HalfLengthCm = Vehicle->GetVehicleLengthCm() * 0.5f;

        DrawDebugBox(
            GetWorld(),
            VehicleLocation,
            FVector(HalfLengthCm, HalfLengthCm * 0.45f, 60.0f),
            Vehicle->GetActorQuat(),
            StateColour,
            false,
            0.0f,
            0,
            4.0f);
    }

    if (bShowLeaderLines)
    {
        if (const ATrafficLaneFollower* Leader = State.Leader.Get())
        {
            DrawDebugLine(
                GetWorld(),
                VehicleLocation,
                Leader->GetActorLocation(),
                FColor::Cyan,
                false,
                0.0f,
                0,
                2.0f);
        }
    }

    if (!bShowVehicleLabels)
    {
        return;
    }

    if (FVector::Dist(VehicleLocation, ViewLocation) > MaxLabelDistanceCm)
    {
        return;
    }

    TStringBuilder<512> Label;

    Label.Appendf(
        TEXT("%s  %.0f / %.0f cm/s"),
        LexConstraint(State.Constraint),
        State.CurrentSpeedCmPerSecond,
        State.DesiredSpeedCmPerSecond);

    if (State.bAccelerating)
    {
        Label.Append(TEXT("  [accel]"));
    }
    else if (State.bBraking)
    {
        Label.Append(TEXT("  [brake]"));
    }

    Label.Appendf(
        TEXT("\nlane %d  %.0f / %.0f cm"),
        State.CurrentLane.LaneIndex,
        State.DistanceAlongLaneCm,
        State.LaneLengthCm);

    if (State.ForwardGapCm >= 0.0f)
    {
        Label.Appendf(TEXT("\ngap %.0f cm"), State.ForwardGapCm);
    }
    else
    {
        Label.Append(TEXT("\ngap -"));
    }

    if (State.bHasNextLane)
    {
        Label.Appendf(
            TEXT("\nnext lane %d (%s)"),
            State.NextLane.LaneIndex,
            LexTurnType(State.NextTurnType));

        if (State.bNextEntersJunction)
        {
            Label.Appendf(
                TEXT("\njunction %s  %s"),
                LexSignal(State.PendingSignalState),
                State.bJunctionEntryGranted
                    ? TEXT("GRANTED")
                    : TEXT("waiting"));
        }
    }
    else
    {
        Label.Append(TEXT("\nnext -"));
    }

    DrawDebugString(
        GetWorld(),
        VehicleLocation + FVector::UpVector * LabelHeightOffsetCm,
        Label.ToString(),
        nullptr,
        StateColour,
        0.0f,
        true,
        LabelFontScale);
}

void ATrafficDebugOverlay::DrawJunctions() const
{
    // The network stores junctions privately and exposes them only by id, so
    // the overlay walks the world instead. Junctions are few, and this keeps
    // the overlay from needing access to the network's internals.
    for (TActorIterator<ATrafficJunction> It(GetWorld()); It; ++It)
    {
        ATrafficJunction* Junction = *It;

        if (!IsValid(Junction) || Junction->RoadNetwork != RoadNetwork)
        {
            continue;
        }

        for (int32 ConnectorIndex = 0;
            ConnectorIndex < Junction->GetConnectorCount();
            ++ConnectorIndex)
        {
            FTrafficConnectorLane Connector;

            if (!Junction->GetConnector(ConnectorIndex, Connector) ||
                Connector.Lane.Samples.Num() == 0)
            {
                continue;
            }

            const ETrafficSignalState SignalState =
                Junction->GetApproachSignalState(Connector.ApproachIndex);

            FColor Colour = FColor::Silver;

            switch (SignalState)
            {
            case ETrafficSignalState::Green:
                Colour = FColor::Green;
                break;

            case ETrafficSignalState::Yellow:
                Colour = FColor::Yellow;
                break;

            case ETrafficSignalState::Red:
                Colour = FColor::Red;
                break;

            case ETrafficSignalState::None:
            default:
                Colour = FColor::Silver;
                break;
            }

            const FVector Start =
                Connector.Lane.Samples[0].Location +
                FVector::UpVector * 20.0f;

            DrawDebugPoint(
                GetWorld(),
                Start,
                14.0f,
                Colour,
                false,
                0.0f);
        }
    }
}

void ATrafficDebugOverlay::DrawSummary(const FVector& ViewLocation) const
{
    const FTrafficNetworkStats Stats = RoadNetwork->GetNetworkStats();

    TStringBuilder<512> Summary;

    Summary.Appendf(
        TEXT("TRAFFIC  %d vehicles\n"),
        Stats.TotalVehicles);

    Summary.Appendf(
        TEXT("free %d   constrained %d   stopped %d\n"),
        Stats.FreeFlowVehicles,
        Stats.ConstrainedVehicles,
        Stats.StoppedVehicles);

    Summary.Appendf(
        TEXT("yielding %d   in junctions %d\n"),
        Stats.VehiclesYielding,
        Stats.VehiclesInsideJunctions);

    Summary.Appendf(
        TEXT("mean speed %.0f cm/s   flow %.0f%%"),
        Stats.MeanSpeedCmPerSecond,
        Stats.MeanSpeedFraction * 100.0f);

    // Anchored to the overlay actor so it stays put in the world rather than
    // tracking the camera.
    DrawDebugString(
        GetWorld(),
        GetActorLocation() + FVector::UpVector * 400.0f,
        Summary.ToString(),
        nullptr,
        FColor::White,
        0.0f,
        true,
        LabelFontScale * 1.2f);
}
