#pragma once

#include "CoreMinimal.h"

#include "TrafficLaneTypes.generated.h"

UENUM(BlueprintType)
enum class ETrafficLaneDirection : uint8
{
    Forward UMETA(DisplayName = "Forward"),
    Reverse UMETA(DisplayName = "Reverse")
};

UENUM(BlueprintType)
enum class ETrafficDrivingSide : uint8
{
    Right UMETA(DisplayName = "Right-hand traffic"),
    Left UMETA(DisplayName = "Left-hand traffic")
};

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLaneHandle
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    FGuid RoadId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    int32 LaneIndex = INDEX_NONE;

    bool IsValid() const
    {
        return RoadId.IsValid() && LaneIndex >= 0;
    }

    friend bool operator==(
        const FTrafficLaneHandle& Left,
        const FTrafficLaneHandle& Right)
    {
        return Left.RoadId == Right.RoadId &&
            Left.LaneIndex == Right.LaneIndex;
    }

    friend bool operator!=(
        const FTrafficLaneHandle& Left,
        const FTrafficLaneHandle& Right)
    {
        return !(Left == Right);
    }
};

FORCEINLINE uint32 GetTypeHash(const FTrafficLaneHandle& Handle)
{
    return HashCombine(
        GetTypeHash(Handle.RoadId),
        GetTypeHash(Handle.LaneIndex));
}

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLaneSample
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    FVector Forward = FVector::ForwardVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    FVector Right = FVector::RightVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    float DistanceAlongLaneCm = 0.0f;
};

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLane
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    FTrafficLaneHandle Handle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    ETrafficLaneDirection Direction = ETrafficLaneDirection::Forward;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    float WidthCm = 350.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    float LengthCm = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    TArray<FTrafficLaneSample> Samples;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Lane")
    float LateralOffsetCm = 0.0f;
};

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLaneGenerationSettings
{
    GENERATED_BODY()

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Lane Generation",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float SampleSpacingCm = 200.0f;
};