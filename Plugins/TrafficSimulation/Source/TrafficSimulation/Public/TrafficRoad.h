// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "RoadNetwork/TrafficLaneProvider.h"
#include "TrafficRoad.generated.h"

class USplineComponent;
class UMaterialInterface;
class USplineMeshComponent;
class UStaticMesh;

UCLASS()
class TRAFFICSIMULATION_API ATrafficRoad
	: public AActor
	, public ITrafficLaneProvider
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATrafficRoad();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
    virtual bool ShouldTickIfViewportsOnly() const override;
    virtual const FGuid& GetRoadId() const override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road")
    virtual FTrafficLaneHandle GetLaneHandle(int32 LaneIndex) const override;

    virtual void PostActorCreated() override;
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Lanes")
    virtual bool EvaluateLaneAtDistance(
        FTrafficLaneHandle LaneHandle,
        float DistanceAlongLaneCm,
        FTransform& OutTransform) const override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Lanes")
    virtual bool GetLaneLength(
        FTrafficLaneHandle LaneHandle,
        float& OutLengthCm) const override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Lanes")
    virtual int32 GetLaneCount() const override;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Shape")
    virtual bool IsRoadClosedLoop() const override;

    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Shape")
    void SetRoadClosedLoop(bool bNewClosedLoop);

    // Lets a procedural builder assign visuals without reaching past the
    // class's encapsulated properties.
    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Rendering")
    void SetRoadSurface(
        UStaticMesh* NewSurfaceMesh,
        UMaterialInterface* NewSurfaceMaterial);

    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Rendering")
    void SetRoadSurfaceOrientation(
        TEnumAsByte<ESplineMeshAxis::Type> NewForwardAxis,
        float NewRollDegrees);

    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Shape")
    void SetLaneCount(int32 NewLaneCount);

    // Lets the debug overlay silence this road's lane lines from one place.
    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Debug")
    void SetDebugDrawEnabled(bool bNewEnabled);

    // Replaces the spline's points wholesale and regenerates lanes and the
    // road surface from the new shape. Intended for procedural road layout.
    //
    // The end tangents may be given explicitly. Where one road continues into
    // another, letting the spline choose its own start direction leaves the
    // two meeting at an angle, and their lane endpoints then sit apart even
    // though the centrelines touch - which a vehicle crosses by jumping.
    // Passing the neighbour's direction makes the join continuous.
    UFUNCTION(BlueprintCallable, Category = "Traffic Road|Shape")
    void SetSplinePoints(
        const TArray<FVector>& WorldPoints,
        bool bNewClosedLoop,
        FVector StartTangent = FVector::ZeroVector,
        FVector EndTangent = FVector::ZeroVector);

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Endpoints")
    FTrafficRoadEndpointHandle GetRoadEndpointHandle(
        ETrafficRoadEndpoint Endpoint) const;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Endpoints")
    FTrafficLaneEndpointHandle GetLaneEndpointHandle(
        int32 LaneIndex,
        ETrafficLaneEndpoint Endpoint) const;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Endpoints")
    bool EvaluateRoadEndpoint(
        FTrafficRoadEndpointHandle EndpointHandle,
        FTransform& OutTransform) const;

    UFUNCTION(BlueprintPure, Category = "Traffic Road|Endpoints")
    bool EvaluateLaneEndpoint(
        FTrafficLaneEndpointHandle EndpointHandle,
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
    bool bDrawDebugLanes = false;

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

    // Which axis of the surface mesh runs along the road. Imported tiles are
    // rarely authored down +X, and picking the wrong one lays the road out
    // sideways. The cross-section scaling follows from this.
    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (AllowPrivateAccess = "true"))
    TEnumAsByte<ESplineMeshAxis::Type> RoadSurfaceForwardAxis =
        ESplineMeshAxis::X;

    // Rotation about the forward axis. Choosing the forward axis fixes which
    // way the tile runs but not which of its faces ends up pointing at the
    // sky, so a tile can come out on its side; 90 or 180 here rights it.
    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (AllowPrivateAccess = "true", Units = "deg"))
    float RoadSurfaceRollDegrees = 0.0f;

    // Length of each piece the surface is built from. Shorter pieces follow a
    // bend more closely and repeat the tile's markings more often; longer
    // ones cost fewer components but stretch the texture and cut corners.
    UPROPERTY(
        EditAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road|Rendering",
        meta = (
            AllowPrivateAccess = "true",
            ClampMin = "50.0",
            UIMin = "50.0",
            Units = "cm"))
    float RoadSurfaceSegmentLengthCm = 400.0f;

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
    bool bDrawLaneDirections = false;

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
