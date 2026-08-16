#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
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

    virtual void PostActorCreated() override;
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;

    // Destroys anything from a previous build, then lays out roads, the
    // junction, the ring, and vehicles from scratch. Safe to press repeatedly
    // while tuning the shape.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Demo")
    void BuildDemoScene();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Demo")
    void ClearDemoScene();

    // Lets the benchmark runner sweep population sizes between rebuilds.
    UFUNCTION(BlueprintCallable, Category = "Traffic Demo")
    void SetTotalVehicleCount(int32 NewCount);

    // The network the last build used. Rebuilding may replace it, so this
    // must be re-read after every BuildDemoScene call.
    UFUNCTION(BlueprintPure, Category = "Traffic Demo")
    ATrafficRoadNetwork* GetBuiltNetwork() const;

private:
    ATrafficRoad* SpawnRoad(
        const TArray<FVector>& WorldPoints,
        bool bRoadClosedLoop);

    void SpawnVehiclesOnRoad(
        ATrafficRoad* Road,
        ATrafficRoadNetwork* Network,
        int32 VehicleCount);

    ATrafficRoadNetwork* ResolveNetwork();

    void EnsureBuilderId();

    // Tag written onto everything this builder spawns. Recorded on the actors
    // themselves rather than only in a list here, because that list does not
    // survive a restart while the actors do - which previously left a scene
    // that could not be cleared and got built on top of instead.
    FName GetSceneTag() const;

    void RegisterSpawnedActor(AActor* Actor);

    // NetworkBounds is the real extent of the roads, gathered as they are
    // built. A circular exclusion cannot describe a rounded rectangle, and
    // underestimating it puts buildings on the ring road's corners.
    void SpawnBuildings(
        const FVector& Origin,
        int32 Columns,
        int32 Rows,
        const FBox& NetworkBounds);

    void SpawnGroundPlane(
        const FVector& Origin,
        const FBox& NetworkBounds);

    // Places one building on a square plot, scaled from its own bounds so any
    // mesh set can be dropped in without retuning the layout.
    bool SpawnBuildingOnPlot(
        const FVector& PlotCentre,
        float NormalisedDistanceFromCentre,
        float PlotSizeCm);

    int32 SpawnedBuildingCount = 0;

    // How many vehicles a road can take without breaching MinVehicleSpacingCm.
    int32 GetRoadVehicleCapacity(ATrafficRoad* Road) const;

    UPROPERTY(VisibleAnywhere, Category = "Traffic Demo")
    TObjectPtr<USceneComponent> SceneRoot;

    // An existing network to build into. Leave unset to have the builder
    // spawn and own its own network actor.
    UPROPERTY(EditInstanceOnly, Category = "Traffic Demo|Network")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

    // Junctions are laid out on a grid. 1x1 is a single crossroads with a
    // ring road around it; larger grids add interior roads between adjacent
    // junctions and are what the scene needs to hold hundreds of vehicles.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "1", UIMin = "1", ClampMax = "8"))
    int32 GridColumns = 1;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "1", UIMin = "1", ClampMax = "8"))
    int32 GridRows = 1;

    // Centre-to-centre distance between adjacent junctions.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "2000.0", UIMin = "2000.0", Units = "cm"))
    float JunctionSpacingCm = 9000.0f;

    // Length of the stub roads that run outward from the edge junctions to
    // meet the perimeter ring.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "500.0", UIMin = "500.0", Units = "cm"))
    float SpurLengthCm = 4000.0f;

    // How far each perimeter link bows outward, as a fraction of the gap
    // between the two stub ends it joins. Rounds off the ring's corners.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Layout",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0"))
    float PerimeterBulgeFraction = 0.3f;

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

    // Which axis of RoadSurfaceMesh runs along the road. A tile authored
    // long on Y needs Y here, or every road is laid out sideways.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Rendering")
    TEnumAsByte<ESplineMeshAxis::Type> RoadSurfaceForwardAxis =
        ESplineMeshAxis::X;

    // Rotation about the forward axis, for when the tile lands on its side.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Rendering",
        meta = (Units = "deg"))
    float RoadSurfaceRollDegrees = 0.0f;

    // The crossing mesh every junction is built with. Junctions are spawned
    // fresh on each build, so this has to be set here rather than on a
    // placed junction, which a rebuild would discard.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Rendering")
    TObjectPtr<UStaticMesh> JunctionSurfaceMesh;

    // Spawns a debug overlay actor already pointed at the built network.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Debug")
    bool bSpawnDebugOverlay = true;

    // Spawns the preset camera rig and hands it the view on begin play.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Debug")
    bool bSpawnCameraRig = false;

    // Lane and connector lines the roads and junctions draw for themselves.
    // Applied at build time, so it holds whether or not a debug overlay is
    // present to drive it.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Debug")
    bool bShowNetworkDebugLines = false;

    // Without one the city floats over empty space, which undoes most of the
    // work the buildings do.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Ground")
    bool bSpawnGroundPlane = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Ground")
    TObjectPtr<UStaticMesh> GroundPlaneMesh;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Ground")
    TObjectPtr<UMaterialInterface> GroundPlaneMaterial;

    // How far the ground reaches past the roads. Needs to clear the exterior
    // buildings, or the city ends on a visible edge.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Ground",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float GroundPlaneMarginCm = 30000.0f;

    // Dropped below the road surface so the two do not z-fight.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Ground",
        meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
    float GroundPlaneDepthCm = 40.0f;

    // Fills the blocks between junctions, which is what makes the network
    // read as a city rather than roads in a void.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Buildings")
    bool bSpawnInteriorBuildings = true;

    // Fills the land outside the perimeter ring. Improves a wide overview
    // shot but adds the most geometry, so it is separately switchable.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Buildings")
    bool bSpawnExteriorBuildings = false;

    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Buildings")
    TArray<TObjectPtr<UStaticMesh>> BuildingMeshes;

    // Kept separate so height can be concentrated near the middle instead of
    // scattered evenly, which is what gives the skyline a centre.
    UPROPERTY(EditAnywhere, Category = "Traffic Demo|Buildings")
    TArray<TObjectPtr<UStaticMesh>> SkyscraperMeshes;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "200.0", UIMin = "200.0", Units = "cm"))
    float BuildingPlotSizeCm = 1800.0f;

    // Gap left between the roads and the nearest building, which is what
    // stops the city crowding the traffic being demonstrated.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float BuildingRoadClearanceCm = 1100.0f;

    // Fraction of its plot a building occupies, leaving the rest as space
    // between neighbours.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "0.1", UIMin = "0.1", ClampMax = "1.0"))
    float BuildingPlotFillFraction = 0.72f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0"))
    float BuildingHeightVariation = 0.35f;

    // Chance of a tower at the very centre, falling to zero at the edge.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0"))
    float SkyscraperCentreChance = 0.75f;

    // How many plots deep the band outside the ring runs.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "1", UIMin = "1", ClampMax = "8"))
    int32 ExteriorBuildingDepth = 3;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Buildings",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 MaxBuildings = 600;

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

    // When above zero, this many vehicles are distributed across the whole
    // network in proportion to lane length, and the per-road counts below are
    // ignored. This is the knob to sweep when benchmarking.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 TotalVehicleCount = 0;

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

    // Lower bound on spacing when distributing TotalVehicleCount, so a lane
    // is never asked to hold more vehicles than physically fit.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Demo|Vehicles",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float MinVehicleSpacingCm = 700.0f;

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

    // Identifies this builder's own scene, so two builders in one level do
    // not clear each other's work. Saved, unlike the list below.
    UPROPERTY(VisibleInstanceOnly, Category = "Traffic Demo|Identity")
    FGuid BuilderId;

    // Fast path for clearing within a session. The tag on each actor is the
    // authoritative record; this is only a convenience.
    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> SpawnedActors;

    UPROPERTY(Transient)
    TObjectPtr<ATrafficRoadNetwork> SpawnedNetwork;
};
