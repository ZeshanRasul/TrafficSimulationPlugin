#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Debug/TrafficDebugTypes.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "RoadNetwork/TrafficLaneProvider.h"
#include "TrafficLaneFollower.generated.h"

class ATrafficRoad;
class USceneComponent;
class UStaticMeshComponent;
class ATrafficRoadNetwork;
class ATrafficJunction;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class ETrafficLaneEndBehavior : uint8
{
    Stop UMETA(DisplayName = "Stop"),
    Loop UMETA(DisplayName = "Loop"),
    Destroy UMETA(DisplayName = "Destroy")
};

UCLASS()
class TRAFFICSIMULATION_API ATrafficLaneFollower : public AActor
{
    GENERATED_BODY()

public:
    ATrafficLaneFollower();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason) override;

    // Sets the fields BeginPlay reads before it runs. Intended for use with
    // SpawnActorDeferred, so a procedural builder can configure a vehicle
    // before its first tick rather than fighting BeginPlay's defaults.
    UFUNCTION(BlueprintCallable, Category = "Traffic Vehicle")
    void ConfigureStart(
        ATrafficRoad* InRoad,
        int32 InLaneIndex,
        float InStartingDistanceCm,
        float InSpeedCmPerSecond,
        ATrafficRoadNetwork* InRoadNetwork);

    // Used by the network's forward-gap search to place and size this
    // vehicle relative to others sharing a lane.
    UFUNCTION(BlueprintPure, Category = "Traffic Vehicle")
    FTrafficLaneHandle GetCurrentLaneHandle() const
    {
        return LaneHandle;
    }

    UFUNCTION(BlueprintPure, Category = "Traffic Vehicle")
    float GetDistanceAlongLaneCm() const
    {
        return DistanceAlongLaneCm;
    }

    UFUNCTION(BlueprintPure, Category = "Traffic Vehicle")
    float GetVehicleLengthCm() const
    {
        return VehicleLengthCm;
    }

    // Read by followers to size their own safe speed. May be this frame's or
    // last frame's value depending on actor tick order; that lag is harmless
    // and reads as ordinary driver reaction time.
    UFUNCTION(BlueprintPure, Category = "Traffic Vehicle")
    float GetCurrentSpeedCmPerSecond() const
    {
        return CurrentSpeedCmPerSecond;
    }

    // Refreshed every tick. Safe to read from the overlay or to inspect on the
    // actor while the simulation is paused.
    const FTrafficVehicleDebugState& GetDebugState() const
    {
        return DebugState;
    }

    UFUNCTION(BlueprintPure, Category = "Traffic Vehicle|Debug")
    FTrafficVehicleDebugState GetDebugStateCopy() const
    {
        return DebugState;
    }

private:
    bool InitializeLane();

    // True while the vehicle is approaching a junction it has not been
    // cleared to enter.
    bool IsYieldingToJunction() const;

    // How far back from the end of the lane the vehicle holds when yielding,
    // measured to its centre. The buffer applies to the front of the vehicle,
    // so half its length is included. Both the hold position and the point at
    // which entry is first requested derive from this; if they disagree the
    // vehicle can stop short of the range in which it asks to proceed and
    // wait there indefinitely.
    float GetStopLineSetbackCm() const;

    void UpdateSpeed(float DeltaSeconds);
    void AdvanceAlongLane(float DeltaSeconds);
    void UpdateTransform();
    bool TryTransitionToNextLane(float OverflowDistanceCm);
    void HandleLaneEnd();

    // Looks ahead to the successor that will be taken at the end of the
    // current lane and caches it, so the vehicle knows whether it is about to
    // enter a junction while it still has room to stop.
    void UpdatePendingSuccessor();

    void ReleaseJunctionReservation();

    void ApplyMeshVariant();

    // Fills in the parts of DebugState that are not already known from
    // UpdateSpeed, and applies the state colour if materials are assigned.
    void UpdateDebugState();

    UPROPERTY(VisibleAnywhere, Category = "Traffic Vehicle")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Traffic Vehicle")
    TObjectPtr<UStaticMeshComponent> VehicleMesh;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Vehicle|Lane")
    TObjectPtr<ATrafficRoad> Road;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Vehicle|Lane",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 LaneIndex = 0;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Vehicle|Movement",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float StartingDistanceCm = 0.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement",
        meta = (Units = "cm/s"))
    float SpeedCmPerSecond = 500.0f;

    // Centimetres per second squared. Queue discharge is limited by how
    // briskly a stopped vehicle can pull away, so this also sets how quickly
    // a junction clears.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement",
        meta = (ClampMin = "1.0", UIMin = "1.0"))
    float AccelerationCmPerSecondSquared = 450.0f;

    // Centimetres per second squared.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement",
        meta = (ClampMin = "1.0", UIMin = "1.0"))
    float BrakingCmPerSecondSquared = 600.0f;

    // How far short of the lane end the vehicle holds when it must yield.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float StopLineBufferCm = 150.0f;

    // Used for bumper-to-bumper gap math, not for physical collision.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Following",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float VehicleLengthCm = 450.0f;

    // Standstill gap: how much clear space is kept to the vehicle ahead when
    // both are stopped in a queue.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Following",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float MinFollowingGapCm = 200.0f;

    // Gap the vehicle aims to keep expressed as travel time rather than a
    // fixed distance, so spacing opens up at speed and closes in a queue.
    // This is what gives a queue its natural look; the safe-speed limit below
    // is the hard guarantee underneath it.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Following",
        meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s"))
    float DesiredTimeHeadwaySeconds = 1.4f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement")
    ETrafficLaneEndBehavior OpenRoadEndBehavior =
        ETrafficLaneEndBehavior::Stop;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Presentation",
        meta = (Units = "cm"))
    float HeightOffsetCm = 60.0f;

    // One is picked per vehicle on spawn, so a populated network is not a
    // fleet of identical cars. Leave empty to keep whatever mesh the
    // Blueprint already has.
    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Presentation")
    TArray<TObjectPtr<UStaticMesh>> MeshVariants;

    // Applied to whichever variant is chosen. Imported assets rarely face
    // down +X at the scale the simulation works in, and correcting that on
    // the component keeps the lane maths in centimetres regardless.
    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Presentation")
    FRotator MeshRotationOffset = FRotator::ZeroRotator;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Presentation",
        meta = (ClampMin = "0.01", UIMin = "0.01"))
    float MeshScale = 1.0f;

    // When set, the chosen variant is scaled so its longest horizontal axis
    // matches VehicleLengthCm. Makes assets of any source size agree with the
    // gaps the simulation is actually keeping.
    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Presentation")
    bool bScaleMeshToVehicleLength = true;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Vehicle|Network")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

    // Whatever currently owns the lane being driven: a road, or a junction
    // while the vehicle is inside the junction box.
    UPROPERTY(Transient)
    TScriptInterface<ITrafficLaneProvider> CurrentProvider;

    // The junction whose connector the vehicle is presently driving on.
    UPROPERTY(Transient)
    TObjectPtr<ATrafficJunction> ActiveJunction;

    // The junction the vehicle is approaching but has not yet been cleared to
    // enter.
    UPROPERTY(Transient)
    TObjectPtr<ATrafficJunction> PendingJunction;

    FTrafficLaneSuccessor PendingSuccessor;
    int32 PendingConnectorIndex = INDEX_NONE;
    bool bPendingSuccessorValid = false;
    bool bEntryGranted = false;

    // Optional: assign all three to recolour the vehicle by motion state.
    // Left unset, the mesh keeps whatever material it already had and only
    // the overlay's own drawing conveys state.
    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Debug")
    TObjectPtr<UMaterialInterface> FreeFlowMaterial;

    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Debug")
    TObjectPtr<UMaterialInterface> ConstrainedMaterial;

    UPROPERTY(EditAnywhere, Category = "Traffic Vehicle|Debug")
    TObjectPtr<UMaterialInterface> StoppedMaterial;

    // Fraction of desired speed above which the vehicle counts as free
    // flowing, and below which (approaching zero) it counts as stopped.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Debug",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0"))
    float FreeFlowSpeedFraction = 0.9f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Debug",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
    float StoppedSpeedThresholdCmPerSecond = 20.0f;

    UPROPERTY(
        VisibleInstanceOnly,
        Category = "Traffic Vehicle|Debug",
        meta = (AllowPrivateAccess = "true"))
    FTrafficVehicleDebugState DebugState;

    // Tracks what is currently applied so the material is only swapped when
    // the state actually changes rather than every tick.
    ETrafficVehicleMotionState AppliedMotionState =
        ETrafficVehicleMotionState::FreeFlow;

    bool bHasAppliedMotionState = false;

    FTrafficLaneHandle LaneHandle;
    float DistanceAlongLaneCm = 0.0f;
    float LaneLengthCm = 0.0f;
    float CurrentSpeedCmPerSecond = 0.0f;
    bool bLaneInitialized = false;
};