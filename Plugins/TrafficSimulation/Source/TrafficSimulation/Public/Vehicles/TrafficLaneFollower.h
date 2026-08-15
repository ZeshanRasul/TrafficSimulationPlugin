#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "RoadNetwork/TrafficLaneProvider.h"
#include "TrafficLaneFollower.generated.h"

class ATrafficRoad;
class USceneComponent;
class UStaticMeshComponent;
class ATrafficRoadNetwork;
class ATrafficJunction;

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

private:
    bool InitializeLane();

    // True while the vehicle is approaching a junction it has not been
    // cleared to enter.
    bool IsYieldingToJunction() const;

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

    // Centimetres per second squared.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Vehicle|Movement",
        meta = (ClampMin = "1.0", UIMin = "1.0"))
    float AccelerationCmPerSecondSquared = 250.0f;

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

    FTrafficLaneHandle LaneHandle;
    float DistanceAlongLaneCm = 0.0f;
    float LaneLengthCm = 0.0f;
    float CurrentSpeedCmPerSecond = 0.0f;
    bool bLaneInitialized = false;
};