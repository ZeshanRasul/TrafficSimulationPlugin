#include "Junctions/TrafficJunction.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TrafficLaneFollower.h"

namespace
{
    FVector EvaluateCubicBezier(
        const FVector& P0,
        const FVector& P1,
        const FVector& P2,
        const FVector& P3,
        float T)
    {
        const float U = 1.0f - T;

        return
            U * U * U * P0 +
            3.0f * U * U * T * P1 +
            3.0f * U * T * T * P2 +
            T * T * T * P3;
    }

    FVector EvaluateCubicBezierTangent(
        const FVector& P0,
        const FVector& P1,
        const FVector& P2,
        const FVector& P3,
        float T)
    {
        const float U = 1.0f - T;

        return (
            3.0f * U * U * (P1 - P0) +
            6.0f * U * T * (P2 - P1) +
            3.0f * T * T * (P3 - P2)).GetSafeNormal();
    }

    bool SegmentsIntersect2D(
        const FVector& A1,
        const FVector& A2,
        const FVector& B1,
        const FVector& B2)
    {
        const double RX = A2.X - A1.X;
        const double RY = A2.Y - A1.Y;
        const double SX = B2.X - B1.X;
        const double SY = B2.Y - B1.Y;

        const double Denominator = RX * SY - RY * SX;

        if (FMath::IsNearlyZero(Denominator))
        {
            return false;
        }

        const double DX = B1.X - A1.X;
        const double DY = B1.Y - A1.Y;

        const double T = (DX * SY - DY * SX) / Denominator;
        const double U = (DX * RY - DY * RX) / Denominator;

        return T >= 0.0 && T <= 1.0 && U >= 0.0 && U <= 1.0;
    }
}

ATrafficJunction::ATrafficJunction()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot =
        CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));

    SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
    EditorIcon =
        CreateEditorOnlyDefaultSubobject<UBillboardComponent>(
            TEXT("EditorIcon"));

    if (EditorIcon)
    {
        EditorIcon->SetupAttachment(SceneRoot);
        EditorIcon->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
    }
#endif

    // A shipped engine asset, so signal lights render out of the box without
    // requiring a project-specific mesh to be assigned first.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    if (SphereMeshFinder.Succeeded())
    {
        SignalMesh = SphereMeshFinder.Object;
    }
}

void ATrafficJunction::BeginPlay()
{
    Super::BeginPlay();

    RebuildJunction();
}

void ATrafficJunction::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    EnsureJunctionId();
    RebuildJunction();
}

bool ATrafficJunction::ShouldTickIfViewportsOnly() const
{
    return true;
}

void ATrafficJunction::PostActorCreated()
{
    Super::PostActorCreated();

    EnsureJunctionId();
}

void ATrafficJunction::PostLoad()
{
    Super::PostLoad();

    EnsureJunctionId();
}

void ATrafficJunction::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);

    if (!bDuplicateForPIE)
    {
        // A duplicated junction must not share the original's identity.
        JunctionId.Invalidate();
    }

    EnsureJunctionId();
}

void ATrafficJunction::EnsureJunctionId()
{
    if (!JunctionId.IsValid())
    {
        JunctionId = FGuid::NewGuid();
    }
}

void ATrafficJunction::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    AdvanceSignals(DeltaSeconds);
    UpdateSignalIndicatorColours();
    DrawDebugJunction();
}

const FGuid& ATrafficJunction::GetRoadId() const
{
    return JunctionId;
}

int32 ATrafficJunction::GetLaneCount() const
{
    return Connectors.Num();
}

int32 ATrafficJunction::GetConnectorCount() const
{
    return Connectors.Num();
}

FTrafficLaneHandle ATrafficJunction::GetLaneHandle(int32 LaneIndex) const
{
    FTrafficLaneHandle Handle;

    if (JunctionId.IsValid() && Connectors.IsValidIndex(LaneIndex))
    {
        Handle.RoadId = JunctionId;
        Handle.LaneIndex = LaneIndex;
    }

    return Handle;
}

bool ATrafficJunction::GetConnector(
    int32 ConnectorIndex,
    FTrafficConnectorLane& OutConnector) const
{
    if (!Connectors.IsValidIndex(ConnectorIndex))
    {
        return false;
    }

    OutConnector = Connectors[ConnectorIndex];
    return true;
}

bool ATrafficJunction::GetLaneLength(
    FTrafficLaneHandle LaneHandle,
    float& OutLengthCm) const
{
    OutLengthCm = 0.0f;

    if (!LaneHandle.IsValid() ||
        LaneHandle.RoadId != JunctionId ||
        !Connectors.IsValidIndex(LaneHandle.LaneIndex))
    {
        return false;
    }

    const FTrafficLane& Lane =
        Connectors[LaneHandle.LaneIndex].Lane;

    if (Lane.LengthCm <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    OutLengthCm = Lane.LengthCm;
    return true;
}

bool ATrafficJunction::EvaluateLaneAtDistance(
    FTrafficLaneHandle LaneHandle,
    float DistanceAlongLaneCm,
    FTransform& OutTransform) const
{
    OutTransform = FTransform::Identity;

    if (!LaneHandle.IsValid() ||
        LaneHandle.RoadId != JunctionId ||
        !Connectors.IsValidIndex(LaneHandle.LaneIndex))
    {
        return false;
    }

    const FTrafficLane& Lane =
        Connectors[LaneHandle.LaneIndex].Lane;

    if (Lane.Samples.Num() < 2 ||
        Lane.LengthCm <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float SafeDistanceCm = FMath::Clamp(
        DistanceAlongLaneCm,
        0.0f,
        Lane.LengthCm);

    // Samples are uniform in curve parameter but not in arc length, so the
    // bracketing pair has to be searched for rather than indexed directly.
    int32 UpperIndex = 1;

    while (UpperIndex < Lane.Samples.Num() - 1 &&
        Lane.Samples[UpperIndex].DistanceAlongLaneCm < SafeDistanceCm)
    {
        ++UpperIndex;
    }

    const FTrafficLaneSample& Lower = Lane.Samples[UpperIndex - 1];
    const FTrafficLaneSample& Upper = Lane.Samples[UpperIndex];

    const float SegmentLengthCm =
        Upper.DistanceAlongLaneCm - Lower.DistanceAlongLaneCm;

    const float Alpha =
        SegmentLengthCm > KINDA_SMALL_NUMBER
        ? (SafeDistanceCm - Lower.DistanceAlongLaneCm) / SegmentLengthCm
        : 0.0f;

    const FVector Location = FMath::Lerp(
        Lower.Location,
        Upper.Location,
        Alpha);

    const FVector Forward = FMath::Lerp(
        Lower.Forward,
        Upper.Forward,
        Alpha).GetSafeNormal();

    const FVector Right =
        FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

    const FVector Up =
        FVector::CrossProduct(Forward, Right).GetSafeNormal();

    OutTransform = FTransform(
        FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
        Location,
        FVector::OneVector);

    return true;
}

ETrafficTurnType ATrafficJunction::ClassifyTurn(
    const FVector& ExitForward,
    const FVector& EntryForward)
{
    const FVector A = ExitForward.GetSafeNormal2D();
    const FVector B = EntryForward.GetSafeNormal2D();

    const float Alignment = FVector::DotProduct(A, B);

    if (Alignment > 0.85f)
    {
        return ETrafficTurnType::Straight;
    }

    if (Alignment < -0.85f)
    {
        return ETrafficTurnType::UTurn;
    }

    // Unreal is left-handed, so a positive Z cross means the departure
    // direction bears to the right of the approach direction.
    return FVector::CrossProduct(A, B).Z > 0.0f
        ? ETrafficTurnType::Right
        : ETrafficTurnType::Left;
}

void ATrafficJunction::BuildConnectorGeometry(
    FTrafficConnectorLane& Connector,
    const FTransform& ExitTransform,
    const FTransform& EntryTransform,
    int32 ConnectorIndex) const
{
    const FVector P0 = ExitTransform.GetLocation();
    const FVector P3 = EntryTransform.GetLocation();

    const FVector ExitForward = ExitTransform.GetUnitAxis(EAxis::X);
    const FVector EntryForward = EntryTransform.GetUnitAxis(EAxis::X);

    const float ChordCm = FVector::Distance(P0, P3);
    const float HandleCm = ChordCm * ConnectorTangentScale;

    const FVector P1 = P0 + ExitForward * HandleCm;
    const FVector P2 = P3 - EntryForward * HandleCm;

    const int32 SegmentCount = FMath::Max(
        4,
        FMath::CeilToInt(
            ChordCm / FMath::Max(ConnectorSampleSpacingCm, 10.0f)));

    Connector.Lane.Handle.RoadId = JunctionId;
    Connector.Lane.Handle.LaneIndex = ConnectorIndex;
    Connector.Lane.Direction = ETrafficLaneDirection::Forward;
    Connector.Lane.LateralOffsetCm = 0.0f;
    Connector.Lane.Samples.Reset(SegmentCount + 1);

    float AccumulatedLengthCm = 0.0f;
    FVector PreviousLocation = P0;

    for (int32 SampleIndex = 0;
        SampleIndex <= SegmentCount;
        ++SampleIndex)
    {
        const float T =
            static_cast<float>(SampleIndex) /
            static_cast<float>(SegmentCount);

        const FVector Location =
            EvaluateCubicBezier(P0, P1, P2, P3, T);

        const FVector Forward =
            EvaluateCubicBezierTangent(P0, P1, P2, P3, T);

        if (SampleIndex > 0)
        {
            AccumulatedLengthCm +=
                FVector::Distance(PreviousLocation, Location);
        }

        PreviousLocation = Location;

        FTrafficLaneSample& Sample =
            Connector.Lane.Samples.AddDefaulted_GetRef();

        Sample.Location = Location;
        Sample.Forward = Forward;

        Sample.Right =
            FVector::CrossProduct(
                FVector::UpVector,
                Forward).GetSafeNormal();

        // True arc length, so that a vehicle advancing at a constant speed
        // moves at a constant speed through the curve.
        Sample.DistanceAlongLaneCm = AccumulatedLengthCm;
    }

    Connector.Lane.LengthCm = AccumulatedLengthCm;
}

void ATrafficJunction::RebuildJunction()
{
    Connectors.Reset();
    Reservations.Reset();

    EnsureJunctionId();

    if (!JunctionId.IsValid())
    {
        return;
    }

    const FVector JunctionLocation = GetActorLocation();
    const float RadiusSquared = FMath::Square(JunctionRadiusCm);

    struct FJunctionEndpoint
    {
        FTrafficLaneEndpointHandle Handle;
        FTransform Transform;
        int32 ApproachIndex = INDEX_NONE;
    };

    TArray<FJunctionEndpoint> IncomingExits;
    TArray<FJunctionEndpoint> OutgoingEntries;

    for (int32 RoadIndex = 0;
        RoadIndex < ApproachRoads.Num();
        ++RoadIndex)
    {
        ATrafficRoad* Road = ApproachRoads[RoadIndex];

        if (!IsValid(Road))
        {
            continue;
        }

        const int32 LaneCount = Road->GetLaneCount();

        for (int32 LaneIndex = 0; LaneIndex < LaneCount; ++LaneIndex)
        {
            // A lane whose exit lands inside the junction feeds it; a lane
            // whose entry lands inside the junction leads away from it.
            const FTrafficLaneEndpointHandle ExitHandle =
                Road->GetLaneEndpointHandle(
                    LaneIndex,
                    ETrafficLaneEndpoint::Exit);

            FTransform ExitTransform;

            if (ExitHandle.IsValid() &&
                Road->EvaluateLaneEndpoint(ExitHandle, ExitTransform) &&
                FVector::DistSquared(
                    ExitTransform.GetLocation(),
                    JunctionLocation) <= RadiusSquared)
            {
                FJunctionEndpoint& Endpoint =
                    IncomingExits.AddDefaulted_GetRef();

                Endpoint.Handle = ExitHandle;
                Endpoint.Transform = ExitTransform;
                Endpoint.ApproachIndex = RoadIndex;
            }

            const FTrafficLaneEndpointHandle EntryHandle =
                Road->GetLaneEndpointHandle(
                    LaneIndex,
                    ETrafficLaneEndpoint::Entry);

            FTransform EntryTransform;

            if (EntryHandle.IsValid() &&
                Road->EvaluateLaneEndpoint(EntryHandle, EntryTransform) &&
                FVector::DistSquared(
                    EntryTransform.GetLocation(),
                    JunctionLocation) <= RadiusSquared)
            {
                FJunctionEndpoint& Endpoint =
                    OutgoingEntries.AddDefaulted_GetRef();

                Endpoint.Handle = EntryHandle;
                Endpoint.Transform = EntryTransform;
                Endpoint.ApproachIndex = RoadIndex;
            }
        }
    }

    for (const FJunctionEndpoint& Incoming : IncomingExits)
    {
        for (const FJunctionEndpoint& Outgoing : OutgoingEntries)
        {
            if (Incoming.Handle.Lane == Outgoing.Handle.Lane)
            {
                continue;
            }

            const FVector ExitForward =
                Incoming.Transform.GetUnitAxis(EAxis::X);

            const FVector EntryForward =
                Outgoing.Transform.GetUnitAxis(EAxis::X);

            const ETrafficTurnType TurnType =
                ClassifyTurn(ExitForward, EntryForward);

            if (TurnType == ETrafficTurnType::UTurn && !bAllowUTurns)
            {
                continue;
            }

            const int32 ConnectorIndex = Connectors.Num();

            FTrafficConnectorLane& Connector =
                Connectors.AddDefaulted_GetRef();

            Connector.SourceExit = Incoming.Handle;
            Connector.TargetEntry = Outgoing.Handle;
            Connector.TurnType = TurnType;
            Connector.ApproachIndex = Incoming.ApproachIndex;

            BuildConnectorGeometry(
                Connector,
                Incoming.Transform,
                Outgoing.Transform,
                ConnectorIndex);

            if (Connector.Lane.LengthCm <= KINDA_SMALL_NUMBER)
            {
                Connectors.Pop();
            }
        }
    }

    BuildConflictMatrix();
    RebuildSignalIndicators();

    if (IsValid(RoadNetwork))
    {
        RoadNetwork->RebuildNetwork();
    }
}

bool ATrafficJunction::ConnectorsCross(
    const FTrafficConnectorLane& First,
    const FTrafficConnectorLane& Second) const
{
    const TArray<FTrafficLaneSample>& FirstSamples = First.Lane.Samples;
    const TArray<FTrafficLaneSample>& SecondSamples = Second.Lane.Samples;

    // A literal segment crossing always conflicts. Below that, two paths that
    // never actually cross but run closer than a lane width apart still need
    // to be flagged - otherwise a shallow-angle near-miss (e.g. two rights
    // sweeping almost parallel through the box) reads as clear and vehicles
    // clip each other without either connector ever registering a conflict.
    const float ClearanceSquaredCm =
        FMath::Square(ConflictClearanceCm);

    for (int32 FirstIndex = 0;
        FirstIndex + 1 < FirstSamples.Num();
        ++FirstIndex)
    {
        for (int32 SecondIndex = 0;
            SecondIndex + 1 < SecondSamples.Num();
            ++SecondIndex)
        {
            if (SegmentsIntersect2D(
                FirstSamples[FirstIndex].Location,
                FirstSamples[FirstIndex + 1].Location,
                SecondSamples[SecondIndex].Location,
                SecondSamples[SecondIndex + 1].Location))
            {
                return true;
            }

            if (FVector::DistSquared(
                FirstSamples[FirstIndex].Location,
                SecondSamples[SecondIndex].Location) <=
                ClearanceSquaredCm)
            {
                return true;
            }
        }
    }

    return false;
}

void ATrafficJunction::BuildConflictMatrix()
{
    for (FTrafficConnectorLane& Connector : Connectors)
    {
        Connector.ConflictingConnectors.Reset();
    }

    for (int32 First = 0; First < Connectors.Num(); ++First)
    {
        for (int32 Second = First + 1;
            Second < Connectors.Num();
            ++Second)
        {
            // Two connectors leaving the same lane diverge: a driver takes one
            // or the other, so they never contend.
            if (Connectors[First].SourceExit.Lane ==
                Connectors[Second].SourceExit.Lane)
            {
                continue;
            }

            // Merging into a shared departure lane is a conflict even when the
            // paths do not cross inside the junction box.
            const bool bMerges =
                Connectors[First].TargetEntry.Lane ==
                Connectors[Second].TargetEntry.Lane;

            if (bMerges ||
                ConnectorsCross(Connectors[First], Connectors[Second]))
            {
                Connectors[First].ConflictingConnectors.Add(Second);
                Connectors[Second].ConflictingConnectors.Add(First);
            }
        }
    }
}

void ATrafficJunction::GetSuccessorLinks(
    TArray<TPair<FTrafficLaneHandle, FTrafficLaneSuccessor>>& OutLinks) const
{
    OutLinks.Reset();

    for (int32 ConnectorIndex = 0;
        ConnectorIndex < Connectors.Num();
        ++ConnectorIndex)
    {
        const FTrafficConnectorLane& Connector = Connectors[ConnectorIndex];

        // Approach lane -> connector. Carries the junction identity so the
        // vehicle knows it must reserve before entering.
        FTrafficLaneSuccessor IntoJunction;

        IntoJunction.Lane = Connector.Lane.Handle;
        IntoJunction.TurnType = Connector.TurnType;
        IntoJunction.JunctionId = JunctionId;
        IntoJunction.ConnectorIndex = ConnectorIndex;

        OutLinks.Emplace(Connector.SourceExit.Lane, IntoJunction);

        // Connector -> departure lane. No reservation needed on the way out.
        FTrafficLaneSuccessor OutOfJunction;

        OutOfJunction.Lane = Connector.TargetEntry.Lane;
        OutOfJunction.TurnType = Connector.TurnType;

        OutLinks.Emplace(Connector.Lane.Handle, OutOfJunction);
    }
}

bool ATrafficJunction::RequestEntry(AActor* Vehicle, int32 ConnectorIndex)
{
    if (!IsValid(Vehicle) || !Connectors.IsValidIndex(ConnectorIndex))
    {
        return false;
    }

    // Drop reservations held by vehicles that have since been destroyed,
    // otherwise a stale ticket can wedge the junction permanently.
    Reservations.RemoveAll(
        [](const FTrafficJunctionReservation& Entry)
        {
            return !Entry.Vehicle.IsValid();
        });

    FTrafficJunctionReservation* Existing =
        Reservations.FindByPredicate(
            [Vehicle](const FTrafficJunctionReservation& Entry)
            {
                return Entry.Vehicle.Get() == Vehicle;
            });

    if (!Existing)
    {
        FTrafficJunctionReservation& Added =
            Reservations.AddDefaulted_GetRef();

        Added.Vehicle = Vehicle;
        Added.ConnectorIndex = ConnectorIndex;
        Added.Ticket = NextTicket++;

        Existing = &Added;
    }

    if (Existing->bGranted)
    {
        return true;
    }

    if (!IsConnectorSignalGreen(ConnectorIndex))
    {
        return false;
    }

    // Connectors sharing a source lane are not conflicts, because a driver
    // takes one or the other. Two different vehicles from that lane, however,
    // still occupy the same tarmac until their paths separate, and car
    // following cannot see across two different connector lanes. Stagger
    // their release so the second only leaves once the first is clear of the
    // shared stretch.
    if (EntryHeadwayCm > 0.0f)
    {
        const FTrafficLaneHandle& OwnSourceLane =
            Connectors[ConnectorIndex].SourceExit.Lane;

        for (const FTrafficJunctionReservation& Other : Reservations)
        {
            if (Other.Vehicle.Get() == Vehicle ||
                !Other.bGranted ||
                !Connectors.IsValidIndex(Other.ConnectorIndex))
            {
                continue;
            }

            if (Connectors[Other.ConnectorIndex].SourceExit.Lane !=
                OwnSourceLane)
            {
                continue;
            }

            const ATrafficLaneFollower* OtherVehicle =
                Cast<ATrafficLaneFollower>(Other.Vehicle.Get());

            if (!OtherVehicle)
            {
                continue;
            }

            // Granted but still on the approach: it has not begun to clear
            // the shared stretch at all.
            if (OtherVehicle->GetCurrentLaneHandle() !=
                Connectors[Other.ConnectorIndex].Lane.Handle)
            {
                return false;
            }

            if (OtherVehicle->GetDistanceAlongLaneCm() < EntryHeadwayCm)
            {
                return false;
            }
        }
    }

    const TArray<int32>& Conflicts =
        Connectors[ConnectorIndex].ConflictingConnectors;

    const uint64 OwnTicket = Existing->Ticket;

    for (const FTrafficJunctionReservation& Other : Reservations)
    {
        if (Other.Vehicle.Get() == Vehicle)
        {
            continue;
        }

        if (!Conflicts.Contains(Other.ConnectorIndex))
        {
            continue;
        }

        // Anything already inside the box must be allowed to clear it.
        if (Other.bGranted)
        {
            return false;
        }

        // A vehicle held at a red light must not block cross traffic that has
        // a green, even though it arrived first.
        if (!IsConnectorSignalGreen(Other.ConnectorIndex))
        {
            continue;
        }

        if (Other.Ticket < OwnTicket)
        {
            return false;
        }
    }

    Existing->bGranted = true;
    return true;
}

void ATrafficJunction::ReleaseEntry(AActor* Vehicle)
{
    Reservations.RemoveAll(
        [Vehicle](const FTrafficJunctionReservation& Entry)
        {
            return !Entry.Vehicle.IsValid() ||
                Entry.Vehicle.Get() == Vehicle;
        });
}

void ATrafficJunction::AdvanceSignals(float DeltaSeconds)
{
    if (!bUseTrafficSignals || SignalPhases.Num() == 0)
    {
        return;
    }

    if (!SignalPhases.IsValidIndex(ActivePhaseIndex))
    {
        ActivePhaseIndex = 0;
        PhaseElapsedSeconds = 0.0f;
        bPhaseInClearance = false;
    }

    PhaseElapsedSeconds += DeltaSeconds;

    const FTrafficSignalPhase& Phase = SignalPhases[ActivePhaseIndex];

    if (!bPhaseInClearance)
    {
        if (PhaseElapsedSeconds >= Phase.GreenDurationSeconds)
        {
            bPhaseInClearance = true;
            PhaseElapsedSeconds = 0.0f;
        }

        return;
    }

    if (PhaseElapsedSeconds >= Phase.ClearanceDurationSeconds)
    {
        ActivePhaseIndex =
            (ActivePhaseIndex + 1) % SignalPhases.Num();

        PhaseElapsedSeconds = 0.0f;
        bPhaseInClearance = false;
    }
}

int32 ATrafficJunction::GetActivePhaseIndex() const
{
    return ActivePhaseIndex;
}

void ATrafficJunction::SetJunctionRadiusCm(float NewRadiusCm)
{
    JunctionRadiusCm = FMath::Max(50.0f, NewRadiusCm);
}

void ATrafficJunction::ConfigureSignals(
    bool bEnable,
    const TArray<FTrafficSignalPhase>& NewPhases)
{
    bUseTrafficSignals = bEnable;
    SignalPhases = NewPhases;
    ActivePhaseIndex = 0;
    PhaseElapsedSeconds = 0.0f;
    bPhaseInClearance = false;

    RebuildSignalIndicators();
}

void ATrafficJunction::SetSignalVisuals(
    UStaticMesh* NewSignalMesh,
    UMaterialInterface* NewRedMaterial,
    UMaterialInterface* NewYellowMaterial,
    UMaterialInterface* NewGreenMaterial)
{
    if (NewSignalMesh)
    {
        SignalMesh = NewSignalMesh;
    }

    RedSignalMaterial = NewRedMaterial;
    YellowSignalMaterial = NewYellowMaterial;
    GreenSignalMaterial = NewGreenMaterial;

    RebuildSignalIndicators();
}

bool ATrafficJunction::IsConnectorSignalGreen(int32 ConnectorIndex) const
{
    if (!bUseTrafficSignals)
    {
        return true;
    }

    if (!Connectors.IsValidIndex(ConnectorIndex))
    {
        return false;
    }

    return GetApproachSignalState(Connectors[ConnectorIndex].ApproachIndex) ==
        ETrafficSignalState::Green;
}

ETrafficSignalState ATrafficJunction::GetApproachSignalState(
    int32 ApproachIndex) const
{
    if (!bUseTrafficSignals || SignalPhases.Num() == 0)
    {
        return ETrafficSignalState::None;
    }

    if (!SignalPhases.IsValidIndex(ActivePhaseIndex))
    {
        return ETrafficSignalState::Red;
    }

    const bool bIsInActivePhase =
        SignalPhases[ActivePhaseIndex].GreenApproachIndices.Contains(
            ApproachIndex);

    // Every approach is held red during the clearance interval so the box
    // empties before the next phase starts; the phase's own approaches show
    // yellow first as the standard just-turned-red warning.
    if (bPhaseInClearance)
    {
        return bIsInActivePhase
            ? ETrafficSignalState::Yellow
            : ETrafficSignalState::Red;
    }

    return bIsInActivePhase
        ? ETrafficSignalState::Green
        : ETrafficSignalState::Red;
}

void ATrafficJunction::RebuildSignalIndicators()
{
    for (UStaticMeshComponent* Indicator : SignalIndicators)
    {
        if (IsValid(Indicator))
        {
            Indicator->DestroyComponent();
        }
    }

    SignalIndicators.Reset();
    SignalIndicatorApproachIndices.Reset();

    if (!bUseTrafficSignals || !SignalMesh)
    {
        return;
    }

    for (int32 ApproachIndex = 0;
        ApproachIndex < ApproachRoads.Num();
        ++ApproachIndex)
    {
        // Any connector leaving this approach shares the same stop-line
        // transform, so the first one found is enough to place the light.
        FTransform ApproachTransform;
        bool bFoundTransform = false;

        for (const FTrafficConnectorLane& Connector : Connectors)
        {
            if (Connector.ApproachIndex != ApproachIndex)
            {
                continue;
            }

            ATrafficRoad* Road = ApproachRoads[ApproachIndex];

            bFoundTransform = IsValid(Road) &&
                Road->EvaluateLaneEndpoint(
                    Connector.SourceExit,
                    ApproachTransform);

            break;
        }

        // An approach with no legal connector (e.g. a stub with nowhere to
        // go) gets no light rather than a dangling one.
        if (!bFoundTransform)
        {
            continue;
        }

        UStaticMeshComponent* Indicator =
            NewObject<UStaticMeshComponent>(
                this,
                NAME_None,
                RF_Transient);

        if (!Indicator)
        {
            continue;
        }

        Indicator->SetStaticMesh(SignalMesh);
        Indicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Indicator->SetMobility(EComponentMobility::Movable);
        Indicator->SetupAttachment(SceneRoot);
        Indicator->RegisterComponent();

        const FVector Forward = ApproachTransform.GetUnitAxis(EAxis::X);

        // Sits back along the approach direction from the stop line, so it
        // reads to an oncoming driver rather than hovering over the box.
        const FVector Location =
            ApproachTransform.GetLocation() -
            Forward * SignalSetbackCm +
            FVector::UpVector * SignalHeightCm;

        Indicator->SetWorldLocation(Location);

        // Engine basic-shape meshes are roughly 100 uu across; this assumes
        // that scale for any mesh substituted in.
        Indicator->SetWorldScale3D(
            FVector(SignalMeshScaleCm / 100.0f));

        SignalIndicators.Add(Indicator);
        SignalIndicatorApproachIndices.Add(ApproachIndex);
    }

    UpdateSignalIndicatorColours();
}

void ATrafficJunction::UpdateSignalIndicatorColours()
{
    if (!bUseTrafficSignals)
    {
        return;
    }

    for (int32 Index = 0; Index < SignalIndicators.Num(); ++Index)
    {
        UStaticMeshComponent* Indicator = SignalIndicators[Index];

        if (!IsValid(Indicator) ||
            !SignalIndicatorApproachIndices.IsValidIndex(Index))
        {
            continue;
        }

        const ETrafficSignalState State =
            GetApproachSignalState(SignalIndicatorApproachIndices[Index]);

        UMaterialInterface* Material = nullptr;

        switch (State)
        {
        case ETrafficSignalState::Green:
            Material = GreenSignalMaterial;
            break;

        case ETrafficSignalState::Yellow:
            Material = YellowSignalMaterial;
            break;

        case ETrafficSignalState::Red:
        case ETrafficSignalState::None:
        default:
            Material = RedSignalMaterial;
            break;
        }

        if (Material)
        {
            Indicator->SetMaterial(0, Material);
        }
    }
}

void ATrafficJunction::DrawDebugJunction() const
{
    if (!bDrawDebugConnectors || !GetWorld())
    {
        return;
    }

    const FVector HeightOffset =
        FVector::UpVector * DebugHeightOffsetCm;

    for (int32 ConnectorIndex = 0;
        ConnectorIndex < Connectors.Num();
        ++ConnectorIndex)
    {
        const FTrafficConnectorLane& Connector = Connectors[ConnectorIndex];

        FColor Colour = FColor::White;

        switch (Connector.TurnType)
        {
        case ETrafficTurnType::Left:
            Colour = FColor::Cyan;
            break;

        case ETrafficTurnType::Right:
            Colour = FColor::Yellow;
            break;

        case ETrafficTurnType::UTurn:
            Colour = FColor::Purple;
            break;

        case ETrafficTurnType::Straight:
        default:
            Colour = FColor::Green;
            break;
        }

        if (!IsConnectorSignalGreen(ConnectorIndex))
        {
            Colour = FColor::Red;
        }

        for (int32 SampleIndex = 0;
            SampleIndex + 1 < Connector.Lane.Samples.Num();
            ++SampleIndex)
        {
            DrawDebugLine(
                GetWorld(),
                Connector.Lane.Samples[SampleIndex].Location + HeightOffset,
                Connector.Lane.Samples[SampleIndex + 1].Location +
                    HeightOffset,
                Colour,
                false,
                0.0f,
                0,
                6.0f);
        }
    }

    if (!bDrawDebugConflicts)
    {
        return;
    }

    for (int32 First = 0; First < Connectors.Num(); ++First)
    {
        const TArray<FTrafficLaneSample>& FirstSamples =
            Connectors[First].Lane.Samples;

        if (FirstSamples.Num() == 0)
        {
            continue;
        }

        const FVector FirstMidpoint =
            FirstSamples[FirstSamples.Num() / 2].Location + HeightOffset;

        for (const int32 Second : Connectors[First].ConflictingConnectors)
        {
            if (Second <= First || !Connectors.IsValidIndex(Second))
            {
                continue;
            }

            const TArray<FTrafficLaneSample>& SecondSamples =
                Connectors[Second].Lane.Samples;

            if (SecondSamples.Num() == 0)
            {
                continue;
            }

            DrawDebugLine(
                GetWorld(),
                FirstMidpoint,
                SecondSamples[SecondSamples.Num() / 2].Location +
                    HeightOffset,
                FColor::Orange,
                false,
                0.0f,
                0,
                1.5f);
        }
    }
}
