#pragma once

#include "CoreMinimal.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficDebugTypes.generated.h"

class ATrafficLaneFollower;

// Why a vehicle is not simply travelling at its desired speed. Distinct from
// the motion state below: this is the cause, that is the degree.
UENUM(BlueprintType)
enum class ETrafficVehicleConstraint : uint8
{
    // Nothing is limiting this vehicle.
    FreeFlow UMETA(DisplayName = "Free Flow"),

    // Limited by the gap to the vehicle ahead.
    Following UMETA(DisplayName = "Following"),

    // Held short of a junction it has not been cleared to enter.
    YieldingToJunction UMETA(DisplayName = "Yielding To Junction"),

    // At the end of an open lane with no successor to continue onto.
    LaneEnd UMETA(DisplayName = "Lane End")
};

// Which vehicles get per-vehicle debug detail drawn for them. A busy network
// is unreadable with everything on at once, so the usual working mode is to
// select one vehicle and watch only that.
UENUM(BlueprintType)
enum class ETrafficDebugVerbosity : uint8
{
    None UMETA(DisplayName = "None"),
    SelectedOnly UMETA(DisplayName = "Selected Only"),
    All UMETA(DisplayName = "All")
};

// How fast the vehicle is actually moving relative to what it wants, which is
// what the free-flow / constrained / stopped colouring keys off.
UENUM(BlueprintType)
enum class ETrafficVehicleMotionState : uint8
{
    FreeFlow UMETA(DisplayName = "Free Flow"),
    Constrained UMETA(DisplayName = "Constrained"),
    Stopped UMETA(DisplayName = "Stopped")
};

// A snapshot of everything worth knowing about one vehicle on a given frame.
// Rebuilt each tick and read by the debug overlay; also inspectable directly
// on the actor while the simulation is paused.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficVehicleDebugState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float CurrentSpeedCmPerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float DesiredSpeedCmPerSecond = 0.0f;

    // What the vehicle is steering its speed towards this frame, after every
    // constraint has been applied.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float TargetSpeedCmPerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    ETrafficVehicleConstraint Constraint =
        ETrafficVehicleConstraint::FreeFlow;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    ETrafficVehicleMotionState MotionState =
        ETrafficVehicleMotionState::FreeFlow;

    // Bumper-to-bumper gap to the vehicle ahead. Negative when there is none.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float ForwardGapCm = -1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    TWeakObjectPtr<ATrafficLaneFollower> Leader;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    FTrafficLaneHandle CurrentLane;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float DistanceAlongLaneCm = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float LaneLengthCm = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    bool bHasNextLane = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    FTrafficLaneHandle NextLane;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    ETrafficTurnType NextTurnType = ETrafficTurnType::Straight;

    // True while the next lane is a junction connector rather than a road.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    bool bNextEntersJunction = false;

    // Signal facing this vehicle at the junction it is approaching. None when
    // it is not approaching one, or that junction is unsignalled.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    ETrafficSignalState PendingSignalState = ETrafficSignalState::None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    bool bJunctionEntryGranted = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    bool bAccelerating = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    bool bBraking = false;
};

// Rolled-up counts across every registered vehicle. Cheap to compute and
// directly reusable as the measurement surface for scale benchmarking.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficNetworkStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 TotalVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 FreeFlowVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 ConstrainedVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 StoppedVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 VehiclesInsideJunctions = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    int32 VehiclesYielding = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float MeanSpeedCmPerSecond = 0.0f;

    // Mean of CurrentSpeed / DesiredSpeed across all vehicles: 1.0 is wholly
    // unimpeded traffic, 0.0 is total gridlock. The single number that best
    // summarises how congested the network is.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Debug")
    float MeanSpeedFraction = 0.0f;
};
