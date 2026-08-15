#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Debug/TrafficDebugTypes.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "RoadNetwork/TrafficLaneProvider.h"
#include "TrafficRoadNetwork.generated.h"

class ATrafficRoad;
class ATrafficJunction;
class ATrafficLaneFollower;

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficNetworkValidationReport
{
    GENERATED_BODY()

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Network Validation")
    bool bIsValid = true;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Network Validation")
    TArray<FString> Errors;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Network Validation")
    TArray<FString> Warnings;

    void Reset()
    {
        bIsValid = true;
        Errors.Reset();
        Warnings.Reset();
    }

    void AddError(const FString& Message)
    {
        bIsValid = false;
        Errors.Add(Message);
    }

    void AddWarning(const FString& Message)
    {
        Warnings.Add(Message);
    }
};

UCLASS()
class TRAFFICSIMULATION_API ATrafficRoadNetwork : public AActor
{
    GENERATED_BODY()

public:
    ATrafficRoadNetwork();

    virtual void BeginPlay() override;

    virtual void OnConstruction(
        const FTransform& Transform) override;

    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category = "Traffic Network")
    void RebuildNetwork();

    // Retained for compatibility: returns the first successor only. Prefer
    // GetLaneSuccessors or ChooseNextLane, which understand junction fan-out.
    UFUNCTION(BlueprintPure, Category = "Traffic Network")
    bool FindNextLane(
        FTrafficLaneHandle CurrentLane,
        FTrafficLaneHandle& OutNextLane) const;

    UFUNCTION(BlueprintPure, Category = "Traffic Network")
    bool GetLaneSuccessors(
        FTrafficLaneHandle CurrentLane,
        TArray<FTrafficLaneSuccessor>& OutSuccessors) const;

    UFUNCTION(BlueprintCallable, Category = "Traffic Network")
    bool ChooseNextLane(
        FTrafficLaneHandle CurrentLane,
        FTrafficLaneSuccessor& OutChoice) const;

    ATrafficRoad* FindRoad(const FGuid& RoadId) const;

    ATrafficJunction* FindJunction(const FGuid& JunctionId) const;

    // Resolves a lane handle's owner, whether it is a road or a junction.
    TScriptInterface<ITrafficLaneProvider> FindLaneProvider(
        const FGuid& ProviderId) const;

    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Junctions")
    void AddJunction(ATrafficJunction* Junction);

    UFUNCTION(BlueprintPure, Category = "Traffic Network|Junctions")
    int32 GetJunctionCount() const;

    UFUNCTION(
        BlueprintCallable,
        CallInEditor,
        Category = "Traffic Network")
    void BuildSimpleConnections();

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Network|Connections",
        meta = (
            ClampMin = "1.0",
            UIMin = "1.0",
            Units = "cm"))
    float MaximumConnectionDistanceCm = 500.0f;

    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Roads")
    void AddRoad(ATrafficRoad* Road);

    // Whether BuildSimpleConnections also wires Roads.Last() to Roads[0],
    // turning a chain into a ring.
    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Connections")
    void SetConnectLastRoadToFirst(bool bNewValue);

    UFUNCTION(BlueprintPure, Category = "Traffic Network|Connections")
    int32 GetConnectionCount() const;

    UFUNCTION(
        BlueprintCallable,
        CallInEditor,
        Category = "Traffic Network|Validation")
    void ValidateNetwork();

    UFUNCTION(BlueprintPure, Category = "Traffic Network|Validation")
    FTrafficNetworkValidationReport GetValidationReport() const;

    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Connections")
    void AddConnection(const FTrafficLaneConnection& Connection);

    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Connections")
    void ClearConnections();

    // Connects one specific pair of roads by endpoint proximity. Grid layouts
    // are not a simple chain, so BuildSimpleConnections' adjacent-pair pass
    // cannot express them; this lets a builder state each join explicitly.
    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Connections")
    void ConnectRoads(ATrafficRoad* FirstRoad, ATrafficRoad* SecondRoad);

    // Vehicles register on BeginPlay/unregister on EndPlay so the network can
    // answer forward-gap queries without every vehicle doing its own O(N)
    // world scan independently.
    void RegisterVehicle(ATrafficLaneFollower* Vehicle);
    void UnregisterVehicle(ATrafficLaneFollower* Vehicle);

    // Smallest bumper-to-bumper gap ahead of (LaneHandle, DistanceAlongLaneCm)
    // among registered vehicles, projecting onto NextLaneHandle (if given) for
    // vehicles already past the end of the current lane. Returns false when no
    // vehicle occupies either lane ahead of the requester.
    bool FindForwardGapCm(
        const ATrafficLaneFollower* Requester,
        FTrafficLaneHandle LaneHandle,
        float DistanceAlongLaneCm,
        float LaneLengthCm,
        const FTrafficLaneHandle* NextLaneHandle,
        float& OutGapCm,
        ATrafficLaneFollower*& OutLeader) const;

    const TArray<TWeakObjectPtr<ATrafficLaneFollower>>&
        GetRegisteredVehicles() const
    {
        return RegisteredVehicles;
    }

    // Rolled up from every registered vehicle's debug state. Cheap enough to
    // call per frame; also the measurement surface for scale benchmarking.
    UFUNCTION(BlueprintPure, Category = "Traffic Network|Debug")
    FTrafficNetworkStats GetNetworkStats() const;

    // Vehicles and junctions add their own tick cost here. Frame time cannot
    // answer how expensive the simulation is whenever a frame rate cap or
    // vsync is active, because the engine simply idles to hit the deadline;
    // this measures the work itself, independent of what the renderer does.
    void AddSimulationTimeSeconds(double Seconds)
    {
        AccumulatedSimulationSeconds += Seconds;
    }

    // Returns the time accumulated since the last call, and resets it.
    double ConsumeSimulationTimeSeconds()
    {
        const double Consumed = AccumulatedSimulationSeconds;
        AccumulatedSimulationSeconds = 0.0;

        return Consumed;
    }

private:
    void DrawDebugConnections() const;

    void BuildConnectionsBetweenRoads(
        ATrafficRoad* FirstRoad,
        ATrafficRoad* SecondRoad);

    UPROPERTY(
        VisibleAnywhere,
        Category = "Traffic Network|Validation")
    FTrafficNetworkValidationReport LastValidationReport;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Network|Roads")
    TArray<TObjectPtr<ATrafficRoad>> Roads;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Network|Connections")
    TArray<FTrafficLaneConnection> Connections;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Network|Debug")
    bool bDrawDebugConnections = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Network|Connections")
    bool bConnectLastRoadToFirst = false;

    UPROPERTY(
        EditInstanceOnly,
        Category = "Traffic Network|Junctions")
    TArray<TObjectPtr<ATrafficJunction>> Junctions;

    TMap<FTrafficLaneHandle, FTrafficLaneSuccessorSet> SuccessorsByLane;

    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<ATrafficLaneFollower>> RegisteredVehicles;

    double AccumulatedSimulationSeconds = 0.0;
};
