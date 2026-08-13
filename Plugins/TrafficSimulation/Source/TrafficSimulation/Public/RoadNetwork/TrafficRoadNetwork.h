#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficRoadNetwork.generated.h"

class ATrafficRoad;

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

    UFUNCTION(BlueprintCallable, Category = "Traffic Network|Roads")
    void AddRoad(ATrafficRoad* Road);

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

    TMap<
        FTrafficLaneHandle,
        FTrafficLaneHandle> NextLaneByLane;
};