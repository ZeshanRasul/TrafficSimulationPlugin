#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Debug/TrafficDebugTypes.h"
#include "TrafficDebugOverlay.generated.h"

class ATrafficRoadNetwork;
class ATrafficLaneFollower;
class USceneComponent;
class UBillboardComponent;

// Draws the simulation's internal state into the viewport: per-vehicle labels,
// leader links, state colouring, junction signals, and a rolled-up summary.
// Reads only; it never influences the simulation.
UCLASS()
class TRAFFICSIMULATION_API ATrafficDebugOverlay : public AActor
{
    GENERATED_BODY()

public:
    ATrafficDebugOverlay();

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;

    UFUNCTION(BlueprintPure, Category = "Traffic Debug")
    FTrafficNetworkStats GetStats() const;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Traffic Debug")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

private:
    void DrawVehicle(
        const ATrafficLaneFollower* Vehicle,
        const FVector& ViewLocation) const;

    void DrawJunctions() const;

    void DrawSummary(const FVector& ViewLocation) const;

    // Where the viewer is, used to cull distant labels. Falls back to the
    // actor's own location when no camera can be resolved.
    bool TryGetViewLocation(FVector& OutViewLocation) const;

    static FColor GetMotionStateColour(ETrafficVehicleMotionState State);

    UPROPERTY(VisibleAnywhere, Category = "Traffic Debug")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Traffic Debug")
    TObjectPtr<UBillboardComponent> EditorIcon;
#endif

    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bEnabled = true;

    // Per-vehicle text. Requires a HUD, so this only renders during play.
    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bShowVehicleLabels = true;

    // A box around each vehicle tinted by motion state. Works in the editor
    // viewport as well as in play.
    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bShowVehicleStateBoxes = true;

    // A line from each vehicle to the one it is following.
    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bShowLeaderLines = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bShowSignalState = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Debug|Toggles")
    bool bShowSummary = true;

    // Labels are skipped beyond this range so a dense network stays readable.
    // Boxes and leader lines are always drawn.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Debug|Display",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float MaxLabelDistanceCm = 8000.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Debug|Display",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float LabelHeightOffsetCm = 220.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Debug|Display",
        meta = (ClampMin = "0.1", UIMin = "0.1"))
    float LabelFontScale = 1.0f;
};
