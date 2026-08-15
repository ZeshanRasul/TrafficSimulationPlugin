#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNetwork/TrafficLaneTypes.h"
#include "RoadNetwork/TrafficLaneProvider.h"
#include "TrafficJunction.generated.h"

class ATrafficRoad;
class ATrafficRoadNetwork;
class USceneComponent;
class UBillboardComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

// A conflict between two connectors, together with how far along this one a
// vehicle must travel before it stops interfering with the other. Blocking a
// conflicting movement for the whole crossing serialises the junction far more
// than the geometry requires; in practice a turning vehicle only obstructs
// oncoming traffic until it is past the point where the paths meet.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficConnectorConflict
{
    GENERATED_BODY()

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    int32 OtherConnectorIndex = INDEX_NONE;

    // Measured along the connector that owns this entry.
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    float ClearDistanceCm = 0.0f;
};

// One drivable path through the junction box, from an approach lane's exit to
// a departure lane's entry. Connectors are lanes in their own right: they carry
// a handle whose RoadId is the junction's id, so vehicles traverse them with
// exactly the same code they use on roads.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficConnectorLane
{
    GENERATED_BODY()

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    FTrafficLane Lane;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    FTrafficLaneEndpointHandle SourceExit;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    FTrafficLaneEndpointHandle TargetEntry;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    ETrafficTurnType TurnType = ETrafficTurnType::Straight;

    // Connectors leaving the same approach road share an approach index, which
    // is what signal phases switch on.
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    int32 ApproachIndex = INDEX_NONE;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Traffic Connector")
    TArray<FTrafficConnectorConflict> Conflicts;

    bool ConflictsWith(int32 OtherConnectorIndex) const
    {
        return Conflicts.ContainsByPredicate(
            [OtherConnectorIndex](const FTrafficConnectorConflict& Entry)
            {
                return Entry.OtherConnectorIndex == OtherConnectorIndex;
            });
    }

    bool TryGetClearDistanceCm(
        int32 OtherConnectorIndex,
        float& OutClearDistanceCm) const
    {
        for (const FTrafficConnectorConflict& Entry : Conflicts)
        {
            if (Entry.OtherConnectorIndex == OtherConnectorIndex)
            {
                OutClearDistanceCm = Entry.ClearDistanceCm;
                return true;
            }
        }

        return false;
    }
};

// A set of approaches that are permitted to move simultaneously.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficSignalPhase
{
    GENERATED_BODY()

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Signal Phase")
    TArray<int32> GreenApproachIndices;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Signal Phase",
        meta = (ClampMin = "0.5", UIMin = "0.5", Units = "s"))
    float GreenDurationSeconds = 8.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Signal Phase",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
    float ClearanceDurationSeconds = 2.0f;
};

USTRUCT()
struct FTrafficJunctionReservation
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<AActor> Vehicle;

    UPROPERTY()
    int32 ConnectorIndex = INDEX_NONE;

    // Monotonic arrival order. Grants strictly respect ticket order, which is
    // what makes the arbitration deadlock-free.
    uint64 Ticket = 0;

    bool bGranted = false;
};

UCLASS()
class TRAFFICSIMULATION_API ATrafficJunction
    : public AActor
    , public ITrafficLaneProvider
{
    GENERATED_BODY()

public:
    ATrafficJunction();

    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;

    virtual void PostActorCreated() override;
    virtual void PostLoad() override;
    virtual void PostDuplicate(bool bDuplicateForPIE) override;

    // ITrafficLaneProvider
    virtual const FGuid& GetRoadId() const override;
    virtual int32 GetLaneCount() const override;
    virtual FTrafficLaneHandle GetLaneHandle(int32 LaneIndex) const override;

    virtual bool GetLaneLength(
        FTrafficLaneHandle LaneHandle,
        float& OutLengthCm) const override;

    virtual bool EvaluateLaneAtDistance(
        FTrafficLaneHandle LaneHandle,
        float DistanceAlongLaneCm,
        FTransform& OutTransform) const override;

    UFUNCTION(
        BlueprintCallable,
        CallInEditor,
        Category = "Traffic Junction")
    void RebuildJunction();

    UFUNCTION(BlueprintPure, Category = "Traffic Junction")
    int32 GetConnectorCount() const;

    bool GetConnector(
        int32 ConnectorIndex,
        FTrafficConnectorLane& OutConnector) const;

    // Emits both halves of every path through the junction:
    // approach lane -> connector, and connector -> departure lane.
    void GetSuccessorLinks(
        TArray<TPair<FTrafficLaneHandle, FTrafficLaneSuccessor>>&
            OutLinks) const;

    // Returns true once the vehicle may enter the junction box. Safe to call
    // every frame; the request is queued on first call and re-evaluated after.
    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Arbitration")
    bool RequestEntry(AActor* Vehicle, int32 ConnectorIndex);

    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Arbitration")
    void ReleaseEntry(AActor* Vehicle);

    UFUNCTION(BlueprintPure, Category = "Traffic Junction|Signals")
    bool IsConnectorSignalGreen(int32 ConnectorIndex) const;

    UFUNCTION(BlueprintPure, Category = "Traffic Junction|Signals")
    int32 GetActivePhaseIndex() const;

    // Total vehicles cleared into the junction since it was built. Sampling
    // the delta over time gives the junction's actual throughput, which is
    // the ceiling on how fast any queue can drain.
    UFUNCTION(BlueprintPure, Category = "Traffic Junction|Arbitration")
    int32 GetTotalGrantsIssued() const
    {
        return TotalGrantsIssued;
    }

    // Red/Yellow/Green for a whole approach, including the all-red clearance
    // window. Drives both IsConnectorSignalGreen and the visible light meshes.
    UFUNCTION(BlueprintPure, Category = "Traffic Junction|Signals")
    ETrafficSignalState GetApproachSignalState(int32 ApproachIndex) const;

    // Lets a procedural builder size the approach search radius to match a
    // generated layout before the first RebuildJunction call.
    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Generation")
    void SetJunctionRadiusCm(float NewRadiusCm);

    // Lets the debug overlay silence connector lines from one place. Signal
    // indicator meshes are unaffected; they are scene dressing, not debug.
    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Debug")
    void SetDebugDrawEnabled(bool bNewEnabled);

    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Signals")
    void ConfigureSignals(
        bool bEnable,
        const TArray<FTrafficSignalPhase>& NewPhases);

    // Lets a procedural builder supply signal visuals so they survive a
    // rebuild instead of being lost with the old junction instance.
    UFUNCTION(BlueprintCallable, Category = "Traffic Junction|Signals")
    void SetSignalVisuals(
        UStaticMesh* NewSignalMesh,
        UMaterialInterface* NewRedMaterial,
        UMaterialInterface* NewYellowMaterial,
        UMaterialInterface* NewGreenMaterial);

    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadOnly,
        Category = "Traffic Junction|Network")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadOnly,
        Category = "Traffic Junction|Network")
    TArray<TObjectPtr<ATrafficRoad>> ApproachRoads;

private:
    UPROPERTY(VisibleAnywhere, Category = "Traffic Junction")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    // The junction has no mesh, so this is the only thing that makes it
    // visible and click-selectable in the editor viewport.
    UPROPERTY(VisibleAnywhere, Category = "Traffic Junction")
    TObjectPtr<UBillboardComponent> EditorIcon;
#endif

    void EnsureJunctionId();

    void BuildConnectorGeometry(
        FTrafficConnectorLane& Connector,
        const FTransform& ExitTransform,
        const FTransform& EntryTransform,
        int32 ConnectorIndex) const;

    void BuildConflictMatrix();

    // Returns true when the two paths interfere, and how far along each one a
    // vehicle must get before it is clear of the other.
    bool ComputeConnectorConflict(
        const FTrafficConnectorLane& First,
        const FTrafficConnectorLane& Second,
        float& OutFirstClearCm,
        float& OutSecondClearCm) const;

    // Whether the connector's departure lane can currently receive a vehicle.
    // Also used to decide that a waiting vehicle which could not move anyway
    // must not hold up others behind it in the queue.
    bool HasExitSpace(int32 ConnectorIndex, AActor* Vehicle) const;

    void AdvanceSignals(float DeltaSeconds);

    void RebuildSignalIndicators();

    void UpdateSignalIndicatorColours();

    void DrawDebugJunction() const;

    static ETrafficTurnType ClassifyTurn(
        const FVector& ExitForward,
        const FVector& EntryForward);

    UPROPERTY(
        VisibleInstanceOnly,
        BlueprintReadOnly,
        Category = "Traffic Junction|Identity",
        meta = (AllowPrivateAccess = "true"))
    FGuid JunctionId;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Transient,
        Category = "Traffic Junction|Generated",
        meta = (AllowPrivateAccess = "true"))
    TArray<FTrafficConnectorLane> Connectors;

    // Lane endpoints are treated as belonging to this junction when they fall
    // inside this radius of the junction actor.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Generation",
        meta = (ClampMin = "50.0", UIMin = "50.0", Units = "cm"))
    float JunctionRadiusCm = 1200.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Generation",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float ConnectorSampleSpacingCm = 100.0f;

    // Paths closer than this at any point conflict even if their centrelines
    // never literally cross. Should be roughly a lane width.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Generation",
        meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
    float ConflictClearanceCm = 350.0f;

    // Connectors leaving one lane are not conflicts - a driver takes one or
    // the other - but two different vehicles from that lane still share the
    // stretch of tarmac where those paths have not yet diverged. A vehicle
    // may not be released from a stop line until the previous vehicle from
    // the same lane is this far into its own connector. Set to 0 to disable.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Arbitration",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float EntryHeadwayCm = 550.0f;

    // Extra distance a vehicle must travel past a conflict point before the
    // blocked movement is released, covering its own length plus a margin.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Arbitration",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float ConflictReleaseMarginCm = 350.0f;

    // Refuse entry unless the departure lane has room to receive the vehicle.
    // Without this a queue that has backed up to the junction lets vehicles
    // in that then cannot leave, so they stall mid-crossing and hold their
    // conflicts indefinitely - the junction gridlocks and never recovers.
    UPROPERTY(EditAnywhere, Category = "Traffic Junction|Arbitration")
    bool bRequireExitSpace = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Arbitration",
        meta = (
            EditCondition = "bRequireExitSpace",
            ClampMin = "0.0",
            UIMin = "0.0",
            Units = "cm"))
    float RequiredExitSpaceCm = 700.0f;

    // Bezier handle length as a fraction of the straight-line chord. 0.55
    // approximates a circular arc; lower values tighten the turn.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Generation",
        meta = (ClampMin = "0.1", UIMin = "0.1", ClampMax = "1.0"))
    float ConnectorTangentScale = 0.55f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Generation")
    bool bAllowUTurns = false;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals")
    bool bUseTrafficSignals = false;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TArray<FTrafficSignalPhase> SignalPhases;

    // One small mesh per approach, placed at its stop line, recoloured as the
    // active phase changes. Only spawned when bUseTrafficSignals is set.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UStaticMesh> SignalMesh;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> RedSignalMaterial;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> YellowSignalMaterial;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (EditCondition = "bUseTrafficSignals"))
    TObjectPtr<UMaterialInterface> GreenSignalMaterial;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (
            EditCondition = "bUseTrafficSignals",
            ClampMin = "50.0",
            UIMin = "50.0",
            Units = "cm"))
    float SignalHeightCm = 350.0f;

    // How far back from the stop line the light sits, so it reads to an
    // approaching driver instead of hovering over the intersection itself.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (
            EditCondition = "bUseTrafficSignals",
            ClampMin = "0.0",
            UIMin = "0.0",
            Units = "cm"))
    float SignalSetbackCm = 250.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Signals",
        meta = (
            EditCondition = "bUseTrafficSignals",
            ClampMin = "10.0",
            UIMin = "10.0",
            Units = "cm"))
    float SignalMeshScaleCm = 50.0f;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> SignalIndicators;

    // Parallel to SignalIndicators: approaches with no connector (a dead-end
    // spur) get no indicator, so the two arrays cannot be assumed to line up
    // with ApproachRoads by index alone.
    UPROPERTY(Transient)
    TArray<int32> SignalIndicatorApproachIndices;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Debug")
    bool bDrawDebugConnectors = true;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Debug")
    bool bDrawDebugConflicts = false;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Junction|Debug",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
    float DebugHeightOffsetCm = 30.0f;

    UPROPERTY(Transient)
    TArray<FTrafficJunctionReservation> Reservations;

    uint64 NextTicket = 1;

    int32 TotalGrantsIssued = 0;

    int32 ActivePhaseIndex = 0;
    float PhaseElapsedSeconds = 0.0f;
    bool bPhaseInClearance = false;
};
