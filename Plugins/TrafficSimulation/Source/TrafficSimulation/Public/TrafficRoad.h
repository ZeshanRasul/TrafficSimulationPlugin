// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficRoad.generated.h"

class USplineComponent;
class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;

UCLASS()
class TRAFFICSIMULATION_API ATrafficRoad : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATrafficRoad();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
    virtual bool ShouldTickIfViewportsOnly() const override;
    const FGuid& GetRoadId() const;

    UFUNCTION(BlueprintPure, Category = "Traffic Road")
    FTrafficLaneHandle GetLaneHandle(int32 LaneIndex) const;

    virtual void PostActorCreated() override;
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Lanes")
    bool EvaluateLaneAtDistance(
        FTrafficLaneHandle LaneHandle,
        float DistanceAlongLaneCm,
        FTransform& OutTransform) const;

private: 
	void DrawDebugLanes() const;

    void EnsureRoadId();

    void RebuildGeneratedLanes();

	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "Traffic Road",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplineComponent> RoadSpline;
    
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road",
        meta = (ClampMin = "1", UIMin = "1"))
    int32 LaneCount = 2;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Lanes",
        meta = (AllowPrivateAccess = "true"))
    ETrafficDrivingSide DrivingSide = ETrafficDrivingSide::Right;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float LaneWidthCm = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Road|Debug")
    bool bDrawDebugLanes = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Debug",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float DebugSampleSpacingCm = 100.0f;

    UPROPERTY(
        VisibleInstanceOnly,
        BlueprintReadOnly,
        Category = "Traffic Road|Identity",
        meta = (AllowPrivateAccess = "true"))
    FGuid RoadId;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Generation",
        meta = (AllowPrivateAccess = "true"))
    FTrafficLaneGenerationSettings LaneGenerationSettings;

    ETrafficLaneDirection DetermineLaneDirection(float LateralOffset) const;
    
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Transient,
        Category = "Traffic Road|Generated",
        meta = (AllowPrivateAccess = "true"))
    TArray<FTrafficLane> GeneratedLanes;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMesh> RoadSurfaceMesh;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMaterialInterface> RoadSurfaceMaterial;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (
            AllowPrivateAccess = "true",
            ClampMin = "1.0",
            UIMin = "1.0",
            Units = "cm"))
    float RoadThicknessCm = 10.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Debug")
    bool bDrawLaneDirections = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Debug",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float DirectionArrowLengthCm = 150.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Debug",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float DebugHeightOffsetCm = 15.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Shape",
        meta = (DisplayName = "Road Closed Loop"))
    bool bClosedLoop = false;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USplineMeshComponent>> RoadSurfaceComponents;


    void ClearRoadSurface();
    void RebuildRoadSurface();
};
