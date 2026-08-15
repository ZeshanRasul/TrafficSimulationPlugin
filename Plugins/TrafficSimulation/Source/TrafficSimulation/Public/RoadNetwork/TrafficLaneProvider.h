#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "TrafficLaneProvider.generated.h"

UINTERFACE(MinimalAPI)
class UTrafficLaneProvider : public UInterface
{
    GENERATED_BODY()
};

// Anything that owns addressable lanes. Roads evaluate their lanes from a
// spline; junctions evaluate theirs by interpolating baked samples. Vehicles
// only ever see this interface, so the traversal code is identical for both.
class TRAFFICSIMULATION_API ITrafficLaneProvider
{
    GENERATED_BODY()

public:
    virtual const FGuid& GetRoadId() const = 0;

    virtual int32 GetLaneCount() const = 0;

    virtual FTrafficLaneHandle GetLaneHandle(int32 LaneIndex) const = 0;

    virtual bool GetLaneLength(
        FTrafficLaneHandle LaneHandle,
        float& OutLengthCm) const = 0;

    virtual bool EvaluateLaneAtDistance(
        FTrafficLaneHandle LaneHandle,
        float DistanceAlongLaneCm,
        FTransform& OutTransform) const = 0;

    // Only splined roads can close on themselves.
    virtual bool IsRoadClosedLoop() const
    {
        return false;
    }
};
