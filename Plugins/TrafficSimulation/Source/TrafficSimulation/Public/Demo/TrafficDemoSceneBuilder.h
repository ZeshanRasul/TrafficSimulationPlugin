#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrafficDemoSceneBuilder.generated.h"

class ATrafficRoad;
class ATrafficJunction;
class ATrafficRoadNetwork;
class ATrafficLaneFollower;
class USceneComponent;
class UStaticMesh;
class UMaterialInterface;

// Procedurally lays out a four-way junction with a rounded ring road looping
// each arm back into the next one, then populates it with vehicles. Exists so
// the demo scene is reproducible from a single button rather than hand-placed
// actors in a .umap, and so its shape is a few float properties instead of a
// fixed layout.
UCLASS()
class TRAFFICSIMULATION_API ATrafficDemoSceneBuilder : public AActor
{
    GENERATED_BODY()

public:
    ATrafficDemoSceneBuilder();

    // Destroys anything from a previous build, then lays out roads, the
    // junction, the ring, and vehicles from scratch. Safe to press repeatedly
    // while tuning the shape.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Demo")
    void BuildDemoScene();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Demo")
    void ClearDemoScene();

private:
    ATrafficRoad* SpawnRoad(
        const TArray<FVector>& WorldPoints,
        bool bRoadClosedLoop);

    void SpawnVehiclesOnRoad(
        ATrafficRoad* Road,
        ATrafficRoadNetwork* Network,
        int32 VehicleCount);

    ATrafficRoadNetwork* ResolveNetwork();

    UPROPERTY(VisibleAnywhere, Category = "Traffic Demo")
    TObjectPtr<USceneComponent> SceneRoot;

    // An existing network to build into. Leave unset to have the builder
    // spawn and own its own network actor.
    UPROPERTY(EditInstanceOnly, Category = "Traffic Demo|Network")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "500.0", UIMin = "500.0", Units = "cm"))
    float SpurLengthCm = 4000.0f;

    // Distance from the junction centre to each ring corner's control point.
    // Larger values bow the ring further out from the crossroads.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "500.0", UIMin = "500.0", Units = "cm"))
    float RingCornerRadiusCm = 6000.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "1", UIMin = "1", ClampMax = "4"))
    int32 LaneCount = 2;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float JunctionRadiusCm = 1200.0f;

    // How far short of the centre each approach road stops. Without this the
    // spurs meet at a single point, the junction has no physical extent, and
    // waiting vehicles sit on top of the connectors crossing traffic uses.
    // Must stay comfortably below JunctionRadiusCm so the junction still
    // finds these endpoints.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float JunctionApproachInsetCm = 800.0f;

    // Ring corners meet spurs at an angle, so same-centreline lane offsets
    // land further apart there than on a straight join. Generous by default.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float ConnectionToleranceCm = 900.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Rendering")
    TObjectPtr<UStaticMesh> RoadSurfaceMesh;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Rendering")
    TObjectPtr<UMaterialInterface> RoadSurfaceMaterial;

    // Spawns a debug overlay actor already pointed at the built network.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Debug")
    bool bSpawnDebugOverlay = true;

    // Spawns the scripted congestion demonstration, wired to the built
    // network and junction. Off by default so an ordinary demo scene runs on
    // balanced signals rather than immediately starting an experiment.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Debug")
    bool bSpawnCongestionExperiment = false;

    // On by default so a freshly placed builder produces the signalled
    // junction the demo is built around.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Signals")
    bool bUseTrafficSignals = true;

    // Leave unset to keep the junction's own default sphere mesh.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UStaticMesh> SignalMesh;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> RedSignalMaterial;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> YellowSignalMaterial;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> GreenSignalMaterial;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Vehicles")
    TSubclassOf<ATrafficLaneFollower> VehicleClass;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 VehiclesPerSpur = 2;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 VehiclesPerRingSegment = 1;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "10.0", UIMin = "10.0"))
    float MinVehicleSpeedCmPerSecond = 400.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "10.0", UIMin = "10.0"))
    float MaxVehicleSpeedCmPerSecond = 700.0f;

    // Everything this builder spawned, so ClearDemoScene can undo it without
    // touching actors placed by hand.
    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> SpawnedActors;

    UPROPERTY(Transient)
    TObjectPtr<ATrafficRoadNetwork> SpawnedNetwork;
};
