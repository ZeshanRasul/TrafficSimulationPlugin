#include "Demo/TrafficCongestionExperiment.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RoadNetwork/TrafficRoadNetwork.h"

namespace
{
    // Approach 0 and 2 are the opposing pair on one axis, 1 and 3 the other.
    // The demo builder assigns approach indices in that order.
    FTrafficSignalPhase MakePhase(
        const TArray<int32>& GreenApproaches,
        float GreenSeconds,
        float ClearanceSeconds)
    {
        FTrafficSignalPhase Phase;

        Phase.GreenApproachIndices = GreenApproaches;
        Phase.GreenDurationSeconds = GreenSeconds;
        Phase.ClearanceDurationSeconds = ClearanceSeconds;

        return Phase;
    }
}

ATrafficCongestionExperiment::ATrafficCongestionExperiment()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
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

    // A three-act schedule: establish a baseline, starve one axis until the
    // queue backs up, then restore the baseline and let it drain. All three
    // stages are editable in the details panel.
    FTrafficCongestionStage Baseline;
    Baseline.Name = TEXT("Baseline");
    Baseline.DurationSeconds = 30.0f;
    Baseline.Phases.Add(MakePhase({ 0, 2 }, 8.0f, 2.0f));
    Baseline.Phases.Add(MakePhase({ 1, 3 }, 8.0f, 2.0f));
    Stages.Add(Baseline);

    FTrafficCongestionStage Restricted;
    Restricted.Name = TEXT("Restricted");
    Restricted.DurationSeconds = 60.0f;
    // Approaches 1 and 3 get barely any green, so their queues grow and
    // eventually spill back up the approach roads.
    Restricted.Phases.Add(MakePhase({ 0, 2 }, 22.0f, 2.0f));
    Restricted.Phases.Add(MakePhase({ 1, 3 }, 3.0f, 2.0f));
    Stages.Add(Restricted);

    // Returning straight to a balanced plan does not clear a backlog: the
    // queued approaches only regain half the capacity, while traffic from the
    // favoured pair keeps circulating and arriving, so flow settles at
    // whatever level the queue imposes. The congested axis is given extra
    // green first, which is what a signal engineer would actually do, and is
    // what makes the queue visibly work off on camera.
    FTrafficCongestionStage Clearing;
    Clearing.Name = TEXT("Clearing");
    Clearing.DurationSeconds = 45.0f;
    Clearing.Phases.Add(MakePhase({ 0, 2 }, 4.0f, 2.0f));
    Clearing.Phases.Add(MakePhase({ 1, 3 }, 16.0f, 2.0f));
    Stages.Add(Clearing);

    FTrafficCongestionStage Recovered;
    Recovered.Name = TEXT("Recovered");
    Recovered.DurationSeconds = 45.0f;
    Recovered.Phases.Add(MakePhase({ 0, 2 }, 8.0f, 2.0f));
    Recovered.Phases.Add(MakePhase({ 1, 3 }, 8.0f, 2.0f));
    Stages.Add(Recovered);
}

void ATrafficCongestionExperiment::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStartOnBeginPlay)
    {
        StartExperiment();
    }
}

bool ATrafficCongestionExperiment::ResolveJunction()
{
    if (IsValid(Junction))
    {
        return true;
    }

    if (!GetWorld())
    {
        return false;
    }

    for (TActorIterator<ATrafficJunction> It(GetWorld()); It; ++It)
    {
        ATrafficJunction* Candidate = *It;

        if (!IsValid(Candidate))
        {
            continue;
        }

        // When no network is set, take the first junction found; otherwise
        // only one belonging to the network under test.
        if (!IsValid(RoadNetwork) || Candidate->RoadNetwork == RoadNetwork)
        {
            Junction = Candidate;
            return true;
        }
    }

    return false;
}

void ATrafficCongestionExperiment::StartExperiment()
{
    if (Stages.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s has no stages configured."),
            *GetName());

        return;
    }

    if (!ResolveJunction())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s could not resolve a junction to drive."),
            *GetName());

        return;
    }

    Samples.Reset();
    TotalElapsedSeconds = 0.0f;
    SampleAccumulatorSeconds = 0.0f;
    LastSampleTimeSeconds = 0.0f;

    LastSampledGrantCount =
        IsValid(Junction) ? Junction->GetTotalGrantsIssued() : 0;

    bRunning = true;

    EnterStage(0);
}

void ATrafficCongestionExperiment::StopExperiment()
{
    if (!bRunning)
    {
        return;
    }

    bRunning = false;

    if (bExportCsvOnFinish)
    {
        ExportCsv();
    }
}

void ATrafficCongestionExperiment::EnterStage(int32 StageIndex)
{
    if (!Stages.IsValidIndex(StageIndex) || !IsValid(Junction))
    {
        return;
    }

    CurrentStageIndex = StageIndex;
    StageElapsedSeconds = 0.0f;

    const FTrafficCongestionStage& Stage = Stages[StageIndex];

    Junction->ConfigureSignals(true, Stage.Phases);

    UE_LOG(
        LogTemp,
        Display,
        TEXT("%s entering stage '%s' for %.1fs."),
        *GetName(),
        *Stage.Name,
        Stage.DurationSeconds);
}

void ATrafficCongestionExperiment::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bRunning)
    {
        return;
    }

    TotalElapsedSeconds += DeltaSeconds;
    StageElapsedSeconds += DeltaSeconds;

    SampleAccumulatorSeconds += DeltaSeconds;

    if (SampleAccumulatorSeconds >= SampleIntervalSeconds)
    {
        SampleAccumulatorSeconds = 0.0f;
        RecordSample(DeltaSeconds);
    }

    if (bShowStatus)
    {
        DrawStatus();
    }

    if (!Stages.IsValidIndex(CurrentStageIndex))
    {
        return;
    }

    if (StageElapsedSeconds < Stages[CurrentStageIndex].DurationSeconds)
    {
        return;
    }

    const int32 NextStageIndex = CurrentStageIndex + 1;

    if (Stages.IsValidIndex(NextStageIndex))
    {
        EnterStage(NextStageIndex);
        return;
    }

    if (bLoop)
    {
        EnterStage(0);
        return;
    }

    StopExperiment();
}

void ATrafficCongestionExperiment::RecordSample(float DeltaSeconds)
{
    if (!IsValid(RoadNetwork))
    {
        return;
    }

    const FTrafficNetworkStats Stats = RoadNetwork->GetNetworkStats();

    FTrafficCongestionSample& Sample = Samples.AddDefaulted_GetRef();

    Sample.TimeSeconds = TotalElapsedSeconds;

    Sample.StageName = Stages.IsValidIndex(CurrentStageIndex)
        ? Stages[CurrentStageIndex].Name
        : TEXT("-");

    Sample.TotalVehicles = Stats.TotalVehicles;
    Sample.StoppedVehicles = Stats.StoppedVehicles;
    Sample.VehiclesYielding = Stats.VehiclesYielding;
    Sample.MeanSpeedCmPerSecond = Stats.MeanSpeedCmPerSecond;
    Sample.MeanSpeedFraction = Stats.MeanSpeedFraction;
    Sample.FrameTimeMs = DeltaSeconds * 1000.0f;

    if (IsValid(Junction))
    {
        const int32 GrantCount = Junction->GetTotalGrantsIssued();

        const float ElapsedSinceLastSample =
            TotalElapsedSeconds - LastSampleTimeSeconds;

        if (ElapsedSinceLastSample > KINDA_SMALL_NUMBER)
        {
            Sample.JunctionThroughputPerMinute =
                (GrantCount - LastSampledGrantCount) *
                60.0f / ElapsedSinceLastSample;
        }

        LastSampledGrantCount = GrantCount;
        LastSampleTimeSeconds = TotalElapsedSeconds;
    }
}

void ATrafficCongestionExperiment::DrawStatus() const
{
    if (!GEngine || !IsValid(RoadNetwork))
    {
        return;
    }

    const FTrafficNetworkStats Stats = RoadNetwork->GetNetworkStats();

    const FString StageName = Stages.IsValidIndex(CurrentStageIndex)
        ? Stages[CurrentStageIndex].Name
        : TEXT("-");

    const float StageDuration = Stages.IsValidIndex(CurrentStageIndex)
        ? Stages[CurrentStageIndex].DurationSeconds
        : 0.0f;

    TStringBuilder<256> Status;

    Status.Appendf(
        TEXT("EXPERIMENT  %s  %.0f / %.0fs\n"),
        *StageName,
        StageElapsedSeconds,
        StageDuration);

    Status.Appendf(
        TEXT("flow %.0f%%   stopped %d / %d"),
        Stats.MeanSpeedFraction * 100.0f,
        Stats.StoppedVehicles,
        Stats.TotalVehicles);

    if (Samples.Num() > 0)
    {
        Status.Appendf(
            TEXT("\njunction %.0f veh/min"),
            Samples.Last().JunctionThroughputPerMinute);
    }

    // Amber while the restriction is biting, so the moment congestion sets in
    // is obvious on camera without reading the numbers.
    const FColor StatusColour =
        Stats.MeanSpeedFraction < 0.5f
        ? FColor::Orange
        : FColor::Green;

    GEngine->AddOnScreenDebugMessage(
        StatusMessageKey,
        0.0f,
        StatusColour,
        Status.ToString());
}

void ATrafficCongestionExperiment::ExportCsv()
{
    if (Samples.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s has no samples to export."),
            *GetName());

        return;
    }

    TStringBuilder<4096> Csv;

    Csv.Append(
        TEXT("TimeSeconds,Stage,TotalVehicles,StoppedVehicles,")
        TEXT("VehiclesYielding,MeanSpeedCmPerSecond,MeanSpeedFraction,")
        TEXT("FrameTimeMs,JunctionThroughputPerMinute\n"));

    for (const FTrafficCongestionSample& Sample : Samples)
    {
        Csv.Appendf(
            TEXT("%.3f,%s,%d,%d,%d,%.2f,%.4f,%.3f,%.1f\n"),
            Sample.TimeSeconds,
            *Sample.StageName,
            Sample.TotalVehicles,
            Sample.StoppedVehicles,
            Sample.VehiclesYielding,
            Sample.MeanSpeedCmPerSecond,
            Sample.MeanSpeedFraction,
            Sample.FrameTimeMs,
            Sample.JunctionThroughputPerMinute);
    }

    const FString OutputPath =
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("TrafficSim"),
            CsvFileName.IsEmpty()
                ? TEXT("CongestionExperiment.csv")
                : CsvFileName);

    if (FFileHelper::SaveStringToFile(Csv.ToString(), *OutputPath))
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("%s wrote %d samples to %s."),
            *GetName(),
            Samples.Num(),
            *OutputPath);
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s failed to write %s."),
            *GetName(),
            *OutputPath);
    }
}
