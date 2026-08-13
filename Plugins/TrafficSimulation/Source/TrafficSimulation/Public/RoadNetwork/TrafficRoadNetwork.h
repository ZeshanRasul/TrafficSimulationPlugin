#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficRoadNetwork.generated.h"

class ATrafficRoad;

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

    UFUNCTION(BlueprintPure, Category = "Traffic Network")
    bool FindNextLane(
        FTrafficLaneHandle CurrentLane,
        FTrafficLaneHandle& OutNextLane) const;

    ATrafficRoad* FindRoad(const FGuid& RoadId) const;
    
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

    void BuildConnectionsBetweenRoads(
        ATrafficRoad* FirstRoad,
        ATrafficRoad* SecondRoad);

private:
    void DrawDebugConnections() const;


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

    TMap<
        FTrafficLaneHandle,
        FTrafficLaneHandle> NextLaneByLane;
};