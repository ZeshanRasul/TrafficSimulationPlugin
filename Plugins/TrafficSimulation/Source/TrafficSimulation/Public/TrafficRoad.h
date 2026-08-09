// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrafficRoad.generated.h"

class USplineComponent;

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

private: 
	void DrawDebugLanes() const;

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
        Category = "Traffic Road",
        meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
    float LaneWidthCm = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Road")
    bool bClosedLoop = false;

    UPROPERTY(EditAnywhere, Category = "Traffic Road|Debug")
    bool bDrawDebugLanes = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Road|Debug",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float DebugSampleSpacingCm = 100.0f;
};
