#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrafficCameraRig.generated.h"

class ATrafficRoadNetwork;
class ATrafficLaneFollower;
class ATrafficJunction;
class UCameraComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class ETrafficCameraMode : uint8
{
    // High and looking down, framed to fit the whole network.
    Overview UMETA(DisplayName = "Overview"),

    // Circling one junction, which is where the behaviour worth watching is.
    JunctionOrbit UMETA(DisplayName = "Junction Orbit"),

    // Behind and above a single vehicle, following it through the network.
    ChaseVehicle UMETA(DisplayName = "Chase Vehicle")
};

// Preset viewpoints for recording. Cycles between a framed overview, an
// orbit of one junction, and a chase camera on a single vehicle.
UCLASS()
class TRAFFICSIMULATION_API ATrafficCameraRig : public AActor
{
    GENERATED_BODY()

public:
    ATrafficCameraRig();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category = "Traffic Camera")
    void CycleMode();

    UFUNCTION(BlueprintCallable, Category = "Traffic Camera")
    void CycleTarget();

    UFUNCTION(BlueprintCallable, Category = "Traffic Camera")
    void SetMode(ETrafficCameraMode NewMode);

    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadWrite,
        Category = "Traffic Camera|Setup")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

private:
    void UpdateOverview(float DeltaSeconds);

    void UpdateJunctionOrbit(float DeltaSeconds);

    void UpdateChase(float DeltaSeconds);

    // Fits the whole road network in view, so the overview shot does not have
    // to be re-tuned every time the grid size changes.
    bool TryGetNetworkBounds(FBox& OutBounds) const;

    void RefreshTargets();

    void DrawStatus() const;

    UPROPERTY(VisibleAnywhere, Category = "Traffic Camera")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Traffic Camera")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    ETrafficCameraMode Mode = ETrafficCameraMode::Overview;

    // Takes over the view on begin play. Turn off to fly the level normally.
    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    bool bPossessOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    bool bBindKeys = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    FKey CycleModeKey;

    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    FKey CycleTargetKey;

    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Setup")
    bool bShowStatus = true;

    // Extra room around the network in the overview shot.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Overview",
        meta = (ClampMin = "1.0", UIMin = "1.0"))
    float OverviewFitMargin = 1.25f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Overview",
        meta = (ClampMin = "5.0", UIMin = "5.0", ClampMax = "89.0"))
    float OverviewPitchDegrees = 55.0f;

    // Slow drift so the overview is not a completely static shot.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Overview",
        meta = (Units = "deg/s"))
    float OverviewYawSpeed = 2.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Junction Orbit",
        meta = (ClampMin = "200.0", UIMin = "200.0", Units = "cm"))
    float OrbitRadiusCm = 3200.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Junction Orbit",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float OrbitHeightCm = 1800.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Junction Orbit",
        meta = (Units = "deg/s"))
    float OrbitYawSpeed = 9.0f;

    // Pulls the camera in when a building comes between it and the junction,
    // rather than letting the shot cut through the interior of one.
    UPROPERTY(EditAnywhere, Category = "Traffic Camera|Junction Orbit")
    bool bAvoidObstructions = true;

    // Stops short of whatever was hit, so the near clip plane does not end up
    // inside the wall it just found.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Junction Orbit",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float ObstructionPaddingCm = 250.0f;

    // How quickly the radius recovers once the view clears. Low values keep
    // the move gradual, which matters far more on camera than reacting fast.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Junction Orbit",
        meta = (ClampMin = "0.1", UIMin = "0.1"))
    float ObstructionRecoverySpeed = 1.5f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Chase",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float ChaseDistanceCm = 900.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Chase",
        meta = (ClampMin = "50.0", UIMin = "50.0", Units = "cm"))
    float ChaseHeightCm = 400.0f;

    // Lag makes the chase read as a camera operator rather than a rigid mount.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Camera|Chase",
        meta = (ClampMin = "0.1", UIMin = "0.1"))
    float ChaseLagSpeed = 3.0f;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ATrafficJunction>> Junctions;

    UPROPERTY(Transient)
    TWeakObjectPtr<ATrafficLaneFollower> ChaseTarget;

    int32 TargetIndex = 0;
    float OrbitAngleDegrees = 0.0f;
    bool bChaseInitialised = false;

    // Eased rather than snapped, so passing behind a building is a gentle
    // move in and back out instead of a jump.
    float CurrentOrbitRadiusCm = 0.0f;

    static constexpr uint64 StatusMessageKey = 0x7A11C003;
};
