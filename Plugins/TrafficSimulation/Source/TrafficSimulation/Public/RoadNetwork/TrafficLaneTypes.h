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

UENUM(BlueprintType)
enum class ETrafficRoadEndpoint : uint8
{
    Start UMETA(DisplayName = "Start"),
    End UMETA(DisplayName = "End")
};

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficRoadEndpointHandle
{
    GENERATED_BODY()

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road Endpoint")
    FGuid RoadId;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Road Endpoint")
    ETrafficRoadEndpoint Endpoint =
        ETrafficRoadEndpoint::Start;

    bool IsValid() const
    {
        return RoadId.IsValid();
    }

    friend bool operator==(
        const FTrafficRoadEndpointHandle& Left,
        const FTrafficRoadEndpointHandle& Right)
    {
        return Left.RoadId == Right.RoadId &&
            Left.Endpoint == Right.Endpoint;
    }

    friend bool operator!=(
        const FTrafficRoadEndpointHandle& Left,
        const FTrafficRoadEndpointHandle& Right)
    {
        return !(Left == Right);
    }
};

FORCEINLINE uint32 GetTypeHash(
    const FTrafficRoadEndpointHandle& Handle)
{
    return HashCombine(
        GetTypeHash(Handle.RoadId),
        GetTypeHash(
            static_cast<uint8>(Handle.Endpoint)));
}

UENUM(BlueprintType)
enum class ETrafficLaneEndpoint : uint8
{
    Entry UMETA(DisplayName = "Entry"),
    Exit UMETA(DisplayName = "Exit")
};

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLaneEndpointHandle
{
    GENERATED_BODY()

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Lane Endpoint")
    FTrafficLaneHandle Lane;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Lane Endpoint")
    ETrafficLaneEndpoint Endpoint =
        ETrafficLaneEndpoint::Entry;

    bool IsValid() const
    {
        return Lane.IsValid();
    }

    friend bool operator==(
        const FTrafficLaneEndpointHandle& Left,
        const FTrafficLaneEndpointHandle& Right)
    {
        return Left.Lane == Right.Lane &&
            Left.Endpoint == Right.Endpoint;
    }

    friend bool operator!=(
        const FTrafficLaneEndpointHandle& Left,
        const FTrafficLaneEndpointHandle& Right)
    {
        return !(Left == Right);
    }
};

FORCEINLINE uint32 GetTypeHash(
    const FTrafficLaneEndpointHandle& Handle)
{
    return HashCombine(
        GetTypeHash(Handle.Lane),
        GetTypeHash(
            static_cast<uint8>(Handle.Endpoint)));
}

USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficLaneConnection
{
    GENERATED_BODY()

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Lane Connection")
    FTrafficLaneEndpointHandle Source;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Lane Connection")
    FTrafficLaneEndpointHandle Target;

    bool IsValid() const
    {
        return Source.IsValid() &&
            Target.IsValid() &&
            Source.Endpoint == ETrafficLaneEndpoint::Exit &&
            Target.Endpoint == ETrafficLaneEndpoint::Entry &&
            Source.Lane != Target.Lane;
    }
};