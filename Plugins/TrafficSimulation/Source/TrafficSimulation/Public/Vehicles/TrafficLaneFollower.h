#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficLaneFollower.generated.h"

class ATrafficRoad;
class USceneComponent;
class UStaticMeshComponent;
class ATrafficRoadNetwork;

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

private:
    bool InitializeLane();
    void AdvanceAlongLane(float DeltaSeconds);
    void UpdateTransform();
    bool TryTransitionToNextLane(float OverflowDistanceCm);
    void HandleLaneEnd();

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

    FTrafficLaneHandle LaneHandle;
    float DistanceAlongLaneCm = 0.0f;
    float LaneLengthCm = 0.0f;
    bool bLaneInitialized = false;
};