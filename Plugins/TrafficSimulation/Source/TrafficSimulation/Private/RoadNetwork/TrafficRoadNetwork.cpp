#include "RoadNetwork/TrafficRoadNetwork.h"

#include "DrawDebugHelpers.h"
#include "Junctions/TrafficJunction.h"
#include "TrafficRoad.h"
#include "Vehicles/TrafficLaneFollower.h"

namespace
{
    bool TryEvaluateLaneEndpoint(
        ATrafficRoad* Road,
        int32 LaneIndex,
        ETrafficLaneEndpoint Endpoint,
        FTrafficLaneEndpointHandle& OutHandle,
        FTransform& OutTransform)
    {
        OutHandle =
            FTrafficLaneEndpointHandle();

        OutTransform =
            FTransform::Identity;

        if (!IsValid(Road))
        {
            return false;
        }

        OutHandle =
            Road->GetLaneEndpointHandle(
                LaneIndex,
                Endpoint);

        return OutHandle.IsValid() &&
            Road->EvaluateLaneEndpoint(
                OutHandle,
                OutTransform);
    }
}

ATrafficRoadNetwork::ATrafficRoadNetwork()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ATrafficRoadNetwork::BeginPlay()
{
    Super::BeginPlay();

    RebuildNetwork();
}

void ATrafficRoadNetwork::OnConstruction(
    const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    RebuildNetwork();
}

void ATrafficRoadNetwork::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    DrawDebugConnections();
}

bool ATrafficRoadNetwork::FindNextLane(
    FTrafficLaneHandle CurrentLane,
    FTrafficLaneHandle& OutNextLane) const
{
    OutNextLane = FTrafficLaneHandle();

    FTrafficLaneSuccessor Choice;

    if (!ChooseNextLane(CurrentLane, Choice))
    {
        return false;
    }

    OutNextLane = Choice.Lane;
    return true;
}

void ATrafficRoadNetwork::DrawDebugConnections() const
{
    if (!bDrawDebugConnections || !GetWorld())

    {
        return;
    }

    for (const FTrafficLaneConnection& Connection :
        Connections)
    {
        if (!Connection.IsValid())
        {
            continue;
        }

        ATrafficRoad* SourceRoad =
            FindRoad(Connection.Source.Lane.RoadId);

        ATrafficRoad* TargetRoad =
            FindRoad(Connection.Target.Lane.RoadId);

        if (!SourceRoad || !TargetRoad)
        {
            continue;
        }

        FTransform SourceTransform;
        FTransform TargetTransform;

        if (!SourceRoad->EvaluateLaneEndpoint(
            Connection.Source,
            SourceTransform) ||
            !TargetRoad->EvaluateLaneEndpoint(
                Connection.Target,
                TargetTransform))
        {
            continue;
        }

        const FVector HeightOffset =
            FVector::UpVector * 40.0f;

        const FVector Start =
            SourceTransform.GetLocation() + HeightOffset;

        const FVector End =
            TargetTransform.GetLocation() + HeightOffset;

        DrawDebugDirectionalArrow(
            GetWorld(),
            Start,
            End,
            75.0f,
            FColor::Magenta,
            false,
            0.0f,
            0,
            5.0f);
    }
}

ATrafficRoad* ATrafficRoadNetwork::FindRoad(
    const FGuid& RoadId) const
{
    for (ATrafficRoad* Road : Roads)
    {
        if (IsValid(Road) &&
            Road->GetRoadId() == RoadId)
        {
            return Road;
        }
    }

    return nullptr;
}

void ATrafficRoadNetwork::BuildSimpleConnections()
{
    Modify();

    Connections.Reset();

    if (Roads.Num() < 2)
    {
        RebuildNetwork();
        return;
    }

    for (int32 RoadIndex = 0;
        RoadIndex < Roads.Num() - 1;
        ++RoadIndex)
    {
        BuildConnectionsBetweenRoads(
            Roads[RoadIndex],
            Roads[RoadIndex + 1]);
    }

    if (bConnectLastRoadToFirst && Roads.Num() > 2)
    {
        BuildConnectionsBetweenRoads(
            Roads.Last(),
            Roads[0]);
    }

    RebuildNetwork();
    ValidateNetwork();

#if WITH_EDITOR
    MarkPackageDirty();
#endif
}

void ATrafficRoadNetwork::RebuildNetwork()
{
    SuccessorsByLane.Reset();

    // Guards against the same directed pair being authored twice. Multiple
    // distinct successors per lane are expected now that junctions fan out.
    TSet<TPair<FTrafficLaneHandle, FTrafficLaneHandle>> SeenPairs;

    for (const FTrafficLaneConnection& Connection :
        Connections)
    {
        if (!Connection.IsValid())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("%s contains an invalid lane connection."),
                *GetName());

            continue;
        }

        const FTrafficLaneHandle& SourceLane =
            Connection.Source.Lane;

        const FTrafficLaneHandle& TargetLane =
            Connection.Target.Lane;

        ATrafficRoad* SourceRoad =
            FindRoad(SourceLane.RoadId);

        ATrafficRoad* TargetRoad =
            FindRoad(TargetLane.RoadId);

        if (!IsValid(SourceRoad) ||
            !IsValid(TargetRoad))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "%s contains a connection whose "
                    "road cannot be resolved."),
                *GetName());

            continue;
        }

        // Confirm that both lane handles still refer to lanes
        // currently generated by their roads.
        float SourceLaneLengthCm = 0.0f;
        float TargetLaneLengthCm = 0.0f;

        if (!SourceRoad->GetLaneLength(
            SourceLane,
            SourceLaneLengthCm) ||
            !TargetRoad->GetLaneLength(
                TargetLane,
                TargetLaneLengthCm))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "%s could not resolve generated source or "
                    "target lane data."),
                *GetName());

            continue;
        }

        const TPair<FTrafficLaneHandle, FTrafficLaneHandle> Pair(
            SourceLane,
            TargetLane);

        if (SeenPairs.Contains(Pair))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "%s contains a duplicate connection from road %s "
                    "lane %d to road %s lane %d."),
                *GetName(),
                *SourceLane.RoadId.ToString(),
                SourceLane.LaneIndex,
                *TargetLane.RoadId.ToString(),
                TargetLane.LaneIndex);

            continue;
        }

        SeenPairs.Add(Pair);

        FTrafficLaneSuccessor Successor;
        Successor.Lane = TargetLane;
        Successor.TurnType = ETrafficTurnType::Straight;

        SuccessorsByLane.FindOrAdd(SourceLane).Successors.Add(Successor);
    }

    // Junctions contribute the approach -> connector -> departure links that
    // give a lane more than one successor.
    TArray<TPair<FTrafficLaneHandle, FTrafficLaneSuccessor>> JunctionLinks;

    for (ATrafficJunction* Junction : Junctions)
    {
        if (!IsValid(Junction))
        {
            continue;
        }

        Junction->GetSuccessorLinks(JunctionLinks);

        for (const TPair<FTrafficLaneHandle, FTrafficLaneSuccessor>& Link :
            JunctionLinks)
        {
            if (!Link.Key.IsValid() || !Link.Value.IsValid())
            {
                continue;
            }

            const TPair<FTrafficLaneHandle, FTrafficLaneHandle> Pair(
                Link.Key,
                Link.Value.Lane);

            if (SeenPairs.Contains(Pair))
            {
                continue;
            }

            SeenPairs.Add(Pair);

            SuccessorsByLane.FindOrAdd(Link.Key).Successors.Add(Link.Value);
        }
    }
}

bool ATrafficRoadNetwork::GetLaneSuccessors(
    FTrafficLaneHandle CurrentLane,
    TArray<FTrafficLaneSuccessor>& OutSuccessors) const
{
    OutSuccessors.Reset();

    if (!CurrentLane.IsValid())
    {
        return false;
    }

    const FTrafficLaneSuccessorSet* Found =
        SuccessorsByLane.Find(CurrentLane);

    if (!Found || Found->Successors.Num() == 0)
    {
        return false;
    }

    OutSuccessors = Found->Successors;
    return true;
}

bool ATrafficRoadNetwork::ChooseNextLane(
    FTrafficLaneHandle CurrentLane,
    FTrafficLaneSuccessor& OutChoice) const
{
    OutChoice = FTrafficLaneSuccessor();

    TArray<FTrafficLaneSuccessor> Successors;

    if (!GetLaneSuccessors(CurrentLane, Successors))
    {
        return false;
    }

    const int32 ChosenIndex =
        FMath::RandRange(0, Successors.Num() - 1);

    OutChoice = Successors[ChosenIndex];
    return true;
}

void ATrafficRoadNetwork::BuildConnectionsBetweenRoads(
    ATrafficRoad* FirstRoad,
    ATrafficRoad* SecondRoad)
{
    if (!IsValid(FirstRoad) ||
        !IsValid(SecondRoad) ||
        FirstRoad == SecondRoad)
    {
        return;
    }

    const float MaximumDistanceSquared =
        FMath::Square(MaximumConnectionDistanceCm);

    // Evaluate both directed possibilities:
    //
    // 0: FirstRoad exit  -> SecondRoad entry
    // 1: SecondRoad exit -> FirstRoad entry
    for (int32 DirectionIndex = 0;
        DirectionIndex < 2;
        ++DirectionIndex)
    {
        ATrafficRoad* SourceRoad =
            DirectionIndex == 0
            ? FirstRoad
            : SecondRoad;

        ATrafficRoad* TargetRoad =
            DirectionIndex == 0
            ? SecondRoad
            : FirstRoad;

        const int32 SourceLaneCount =
            SourceRoad->GetLaneCount();

        const int32 TargetLaneCount =
            TargetRoad->GetLaneCount();

        // Prevent two source lanes from selecting the same target
        // during this simple one-to-one generation pass.
        TSet<FTrafficLaneHandle> UsedTargetLanes;

        for (int32 SourceLaneIndex = 0;
            SourceLaneIndex < SourceLaneCount;
            ++SourceLaneIndex)
        {
            FTrafficLaneEndpointHandle SourceHandle;
            FTransform SourceTransform;

            if (!TryEvaluateLaneEndpoint(
                SourceRoad,
                SourceLaneIndex,
                ETrafficLaneEndpoint::Exit,
                SourceHandle,
                SourceTransform))
            {
                continue;
            }

            bool bFoundTarget = false;
            float BestDistanceSquared =
                MaximumDistanceSquared;

            FTrafficLaneEndpointHandle BestTargetHandle;

            for (int32 TargetLaneIndex = 0;
                TargetLaneIndex < TargetLaneCount;
                ++TargetLaneIndex)
            {
                FTrafficLaneEndpointHandle TargetHandle;
                FTransform TargetTransform;

                if (!TryEvaluateLaneEndpoint(
                    TargetRoad,
                    TargetLaneIndex,
                    ETrafficLaneEndpoint::Entry,
                    TargetHandle,
                    TargetTransform))
                {
                    continue;
                }

                if (UsedTargetLanes.Contains(
                    TargetHandle.Lane))
                {
                    continue;
                }

                const float DistanceSquared =
                    FVector::DistSquared(
                        SourceTransform.GetLocation(),
                        TargetTransform.GetLocation());

                if (DistanceSquared <=
                    BestDistanceSquared)
                {
                    bFoundTarget = true;
                    BestDistanceSquared =
                        DistanceSquared;
                    BestTargetHandle =
                        TargetHandle;
                }
            }

            if (!bFoundTarget)
            {
                continue;
            }

            FTrafficLaneConnection& Connection =
                Connections.AddDefaulted_GetRef();

            Connection.Source = SourceHandle;
            Connection.Target = BestTargetHandle;

            UsedTargetLanes.Add(
                BestTargetHandle.Lane);
        }
    }
}

ATrafficJunction* ATrafficRoadNetwork::FindJunction(
    const FGuid& JunctionId) const
{
    for (ATrafficJunction* Junction : Junctions)
    {
        if (IsValid(Junction) &&
            Junction->GetRoadId() == JunctionId)
        {
            return Junction;
        }
    }

    return nullptr;
}

TScriptInterface<ITrafficLaneProvider>
ATrafficRoadNetwork::FindLaneProvider(const FGuid& ProviderId) const
{
    TScriptInterface<ITrafficLaneProvider> Provider;

    if (ATrafficRoad* Road = FindRoad(ProviderId))
    {
        Provider.SetObject(Road);
        Provider.SetInterface(Cast<ITrafficLaneProvider>(Road));

        return Provider;
    }

    if (ATrafficJunction* Junction = FindJunction(ProviderId))
    {
        Provider.SetObject(Junction);
        Provider.SetInterface(Cast<ITrafficLaneProvider>(Junction));
    }

    return Provider;
}

void ATrafficRoadNetwork::AddJunction(ATrafficJunction* Junction)
{
    if (!IsValid(Junction) ||
        Junctions.Contains(Junction))
    {
        return;
    }

    Modify();
    Junctions.Add(Junction);

    RebuildNetwork();
}

int32 ATrafficRoadNetwork::GetJunctionCount() const
{
    return Junctions.Num();
}

void ATrafficRoadNetwork::AddRoad(
    ATrafficRoad* Road)
{
    if (!IsValid(Road) ||
        Roads.Contains(Road))
    {
        return;
    }

    Modify();
    Roads.Add(Road);

    RebuildNetwork();
}

void ATrafficRoadNetwork::AddConnection(
    const FTrafficLaneConnection& Connection)
{
    Modify();
    Connections.Add(Connection);

    RebuildNetwork();
}

void ATrafficRoadNetwork::ClearConnections()
{
    Modify();
    Connections.Reset();

    RebuildNetwork();
}

void ATrafficRoadNetwork::RegisterVehicle(ATrafficLaneFollower* Vehicle)
{
    if (!IsValid(Vehicle))
    {
        return;
    }

    RegisteredVehicles.AddUnique(Vehicle);
}

void ATrafficRoadNetwork::UnregisterVehicle(ATrafficLaneFollower* Vehicle)
{
    RegisteredVehicles.RemoveAll(
        [Vehicle](const TWeakObjectPtr<ATrafficLaneFollower>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Vehicle;
        });
}

bool ATrafficRoadNetwork::FindForwardGapCm(
    const ATrafficLaneFollower* Requester,
    FTrafficLaneHandle LaneHandle,
    float DistanceAlongLaneCm,
    float LaneLengthCm,
    const FTrafficLaneHandle* NextLaneHandle,
    float& OutGapCm) const
{
    OutGapCm = TNumericLimits<float>::Max();

    bool bFound = false;

    const float RequesterHalfLengthCm =
        IsValid(Requester)
        ? Requester->GetVehicleLengthCm() * 0.5f
        : 0.0f;

    for (const TWeakObjectPtr<ATrafficLaneFollower>& WeakVehicle :
        RegisteredVehicles)
    {
        ATrafficLaneFollower* Other = WeakVehicle.Get();

        if (!IsValid(Other) || Other == Requester)
        {
            continue;
        }

        const FTrafficLaneHandle OtherLaneHandle =
            Other->GetCurrentLaneHandle();

        const float OtherHalfLengthCm =
            Other->GetVehicleLengthCm() * 0.5f;

        float CandidateGapCm = 0.0f;
        bool bCandidateValid = false;

        if (OtherLaneHandle == LaneHandle)
        {
            const float OtherDistanceCm =
                Other->GetDistanceAlongLaneCm();

            if (OtherDistanceCm >= DistanceAlongLaneCm)
            {
                CandidateGapCm =
                    (OtherDistanceCm - DistanceAlongLaneCm) -
                    RequesterHalfLengthCm -
                    OtherHalfLengthCm;

                bCandidateValid = true;
            }
        }
        else if (NextLaneHandle && OtherLaneHandle == *NextLaneHandle)
        {
            // The other vehicle has already crossed onto what this vehicle is
            // about to enter, so its gap is measured across the boundary:
            // whatever remains of this lane, plus how far the other vehicle
            // already is into the next one.
            const float RemainingOnCurrentLaneCm =
                LaneLengthCm - DistanceAlongLaneCm;

            CandidateGapCm =
                RemainingOnCurrentLaneCm +
                Other->GetDistanceAlongLaneCm() -
                RequesterHalfLengthCm -
                OtherHalfLengthCm;

            bCandidateValid = true;
        }

        if (bCandidateValid)
        {
            bFound = true;
            OutGapCm = FMath::Min(OutGapCm, CandidateGapCm);
        }
    }

    return bFound;
}

int32 ATrafficRoadNetwork::GetConnectionCount() const
{
    return Connections.Num();
}

void ATrafficRoadNetwork::SetConnectLastRoadToFirst(bool bNewValue)
{
    bConnectLastRoadToFirst = bNewValue;
}

void ATrafficRoadNetwork::ValidateNetwork()
{
    LastValidationReport.Reset();

    TSet<FGuid> RegisteredRoadIds;

    for (int32 RoadIndex = 0;
        RoadIndex < Roads.Num();
        ++RoadIndex)
    {
        ATrafficRoad* Road = Roads[RoadIndex];

        if (!IsValid(Road))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT("Roads[%d] is empty or invalid."),
                    RoadIndex));

            continue;
        }

        const FGuid& RoadId = Road->GetRoadId();

        if (!RoadId.IsValid())
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT("%s does not have a valid road ID."),
                    *Road->GetName()));

            continue;
        }

        if (RegisteredRoadIds.Contains(RoadId))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT("Duplicate road ID detected on %s: %s."),
                    *Road->GetName(),
                    *RoadId.ToString()));

            continue;
        }

        RegisteredRoadIds.Add(RoadId);
    }

    TSet<TPair<FTrafficLaneHandle, FTrafficLaneHandle>> SeenConnectionPairs;

    for (int32 ConnectionIndex = 0;
        ConnectionIndex < Connections.Num();
        ++ConnectionIndex)
    {
        const FTrafficLaneConnection& Connection =
            Connections[ConnectionIndex];

        if (!Connection.IsValid())
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] is not a valid "
                        "exit-to-entry connection."),
                    ConnectionIndex));

            continue;
        }

        const FTrafficLaneHandle& SourceLane =
            Connection.Source.Lane;

        const FTrafficLaneHandle& TargetLane =
            Connection.Target.Lane;

        ATrafficRoad* SourceRoad =
            FindRoad(SourceLane.RoadId);

        ATrafficRoad* TargetRoad =
            FindRoad(TargetLane.RoadId);

        if (!IsValid(SourceRoad))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] source road %s "
                        "is not registered."),
                    ConnectionIndex,
                    *SourceLane.RoadId.ToString()));

            continue;
        }

        if (!IsValid(TargetRoad))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] target road %s "
                        "is not registered."),
                    ConnectionIndex,
                    *TargetLane.RoadId.ToString()));

            continue;
        }

        float SourceLengthCm = 0.0f;
        float TargetLengthCm = 0.0f;

        if (!SourceRoad->GetLaneLength(
                SourceLane,
                SourceLengthCm))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] source lane %d "
                        "does not exist."),
                    ConnectionIndex,
                    SourceLane.LaneIndex));

            continue;
        }

        if (!TargetRoad->GetLaneLength(
                TargetLane,
                TargetLengthCm))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] target lane %d "
                        "does not exist."),
                    ConnectionIndex,
                    TargetLane.LaneIndex));

            continue;
        }

        // A lane may now have several successors (junction fan-out) and several
        // predecessors (junction merges); only an exact repeat is an error.
        const TPair<FTrafficLaneHandle, FTrafficLaneHandle> ConnectionPair(
            SourceLane,
            TargetLane);

        if (SeenConnectionPairs.Contains(ConnectionPair))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] duplicates an existing "
                        "source-to-target pair."),
                    ConnectionIndex));

            continue;
        }

        FTransform SourceTransform;
        FTransform TargetTransform;

        if (!SourceRoad->EvaluateLaneEndpoint(
                Connection.Source,
                SourceTransform) ||
            !TargetRoad->EvaluateLaneEndpoint(
                Connection.Target,
                TargetTransform))
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] endpoints could "
                        "not be evaluated."),
                    ConnectionIndex));

            continue;
        }

        const float ConnectionDistanceCm =
            FVector::Distance(
                SourceTransform.GetLocation(),
                TargetTransform.GetLocation());

        if (ConnectionDistanceCm >
            MaximumConnectionDistanceCm)
        {
            LastValidationReport.AddError(
                FString::Printf(
                    TEXT(
                        "Connection[%d] gap is %.1f cm, "
                        "exceeding the %.1f cm limit."),
                    ConnectionIndex,
                    ConnectionDistanceCm,
                    MaximumConnectionDistanceCm));

            continue;
        }

        const FVector SourceForward =
            SourceTransform.GetUnitAxis(EAxis::X);

        const FVector TargetForward =
            TargetTransform.GetUnitAxis(EAxis::X);

        const float AlignmentDot =
            FVector::DotProduct(
                SourceForward,
                TargetForward);

        if (AlignmentDot < 0.5f)
        {
            LastValidationReport.AddWarning(
                FString::Printf(
                    TEXT(
                        "Connection[%d] has poor direction "
                        "alignment (dot %.2f)."),
                    ConnectionIndex,
                    AlignmentDot));
        }

        SeenConnectionPairs.Add(ConnectionPair);
    }

    if (Roads.IsEmpty())
    {
        LastValidationReport.AddWarning(
            TEXT("The network does not contain any roads."));
    }
    else if (Connections.IsEmpty())
    {
        LastValidationReport.AddWarning(
            TEXT("The network does not contain any connections."));
    }

    if (LastValidationReport.bIsValid)
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("%s validation succeeded with %d warning(s)."),
            *GetName(),
            LastValidationReport.Warnings.Num());
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "%s validation failed with %d error(s) "
                "and %d warning(s)."),
            *GetName(),
            LastValidationReport.Errors.Num(),
            LastValidationReport.Warnings.Num());
    }
}

FTrafficNetworkValidationReport
ATrafficRoadNetwork::GetValidationReport() const
{
    return LastValidationReport;
}
