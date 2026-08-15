#include "Demo/TrafficBenchmarkRunner.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Debug/TrafficDebugOverlay.h"
#include "Demo/TrafficDemoSceneBuilder.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Junctions/TrafficJunction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RoadNetwork/TrafficRoadNetwork.h"
#include "TrafficRoad.h"

ATrafficBenchmarkRunner::ATrafficBenchmarkRunner()
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
}

void ATrafficBenchmarkRunner::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStartOnBeginPlay)
    {
        StartBenchmark();
    }
}

void ATrafficBenchmarkRunner::SetDebugDrawingEnabled(bool bEnabled) const
{
    if (!GetWorld())
    {
        return;
    }

    for (TActorIterator<ATrafficDebugOverlay> It(GetWorld()); It; ++It)
    {
        if (ATrafficDebugOverlay* Overlay = *It)
        {
            Overlay->SetActorTickEnabled(bEnabled);
        }
    }

    for (TActorIterator<ATrafficRoad> It(GetWorld()); It; ++It)
    {
        if (ATrafficRoad* Road = *It)
        {
            Road->SetDebugDrawEnabled(bEnabled);
        }
    }

    for (TActorIterator<ATrafficJunction> It(GetWorld()); It; ++It)
    {
        if (ATrafficJunction* Junction = *It)
        {
            Junction->SetDebugDrawEnabled(bEnabled);
        }
    }
}

void ATrafficBenchmarkRunner::ApplyFrameRateOverride()
{
    if (bFrameRateOverridden)
    {
        return;
    }

    IConsoleVariable* MaxFpsVar =
        IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));

    IConsoleVariable* VSyncVar =
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));

    if (MaxFpsVar)
    {
        PreviousMaxFps = MaxFpsVar->GetFloat();
        MaxFpsVar->Set(0.0f, ECVF_SetByConsole);
    }

    if (VSyncVar)
    {
        PreviousVSync = VSyncVar->GetInt();
        VSyncVar->Set(0, ECVF_SetByConsole);
    }

    bFrameRateOverridden = true;

    UE_LOG(
        LogTemp,
        Display,
        TEXT(
            "%s lifted the frame rate cap for the run "
            "(was t.MaxFPS %.0f, r.VSync %d)."),
        *GetName(),
        PreviousMaxFps,
        PreviousVSync);
}

void ATrafficBenchmarkRunner::RestoreFrameRateOverride()
{
    if (!bFrameRateOverridden)
    {
        return;
    }

    if (IConsoleVariable* MaxFpsVar =
        IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
    {
        MaxFpsVar->Set(PreviousMaxFps, ECVF_SetByConsole);
    }

    if (IConsoleVariable* VSyncVar =
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
    {
        VSyncVar->Set(PreviousVSync, ECVF_SetByConsole);
    }

    bFrameRateOverridden = false;
}

void ATrafficBenchmarkRunner::StartBenchmark()
{
    if (!IsValid(SceneBuilder))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s has no SceneBuilder assigned."),
            *GetName());

        return;
    }

    if (VehicleCounts.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s has no vehicle counts to sweep."),
            *GetName());

        return;
    }

    Results.Reset();

    if (bDisableDebugDrawingDuringRun)
    {
        SetDebugDrawingEnabled(false);
    }

    if (bRemoveFrameRateCapDuringRun)
    {
        ApplyFrameRateOverride();
    }

    BeginConfiguration(0);
}

void ATrafficBenchmarkRunner::StopBenchmark()
{
    if (Phase == ETrafficBenchmarkPhase::Idle ||
        Phase == ETrafficBenchmarkPhase::Finished)
    {
        return;
    }

    Phase = ETrafficBenchmarkPhase::Finished;

    if (bDisableDebugDrawingDuringRun)
    {
        SetDebugDrawingEnabled(true);
    }

    RestoreFrameRateOverride();

    if (bExportCsvOnFinish)
    {
        ExportCsv();
    }
}

void ATrafficBenchmarkRunner::BeginConfiguration(int32 ConfigurationIndex)
{
    if (!VehicleCounts.IsValidIndex(ConfigurationIndex) ||
        !IsValid(SceneBuilder))
    {
        StopBenchmark();
        return;
    }

    CurrentConfigurationIndex = ConfigurationIndex;

    const int32 RequestedCount = VehicleCounts[ConfigurationIndex];

    SceneBuilder->SetTotalVehicleCount(RequestedCount);
    SceneBuilder->BuildDemoScene();

    // Rebuilding can replace the network actor, so it has to be re-read.
    CurrentNetwork = SceneBuilder->GetBuiltNetwork();

    if (bDisableDebugDrawingDuringRun)
    {
        // The rebuild spawns fresh roads, junctions and an overlay, so the
        // quiet settings have to be reapplied to them.
        SetDebugDrawingEnabled(false);
    }

    FrameTimesMs.Reset();
    SimulationTimesMs.Reset();
    FlowFractionTotal = 0.0;
    StoppedTotal = 0.0;
    StatSampleCount = 0;
    PeakVehicleCount = 0;

    PhaseElapsedSeconds = 0.0f;
    Phase = ETrafficBenchmarkPhase::WarmUp;

    UE_LOG(
        LogTemp,
        Display,
        TEXT("%s starting configuration %d: %d vehicles requested."),
        *GetName(),
        ConfigurationIndex,
        RequestedCount);
}

void ATrafficBenchmarkRunner::FinishConfiguration()
{
    FTrafficBenchmarkResult& Result = Results.AddDefaulted_GetRef();

    Result.RequestedVehicles =
        VehicleCounts.IsValidIndex(CurrentConfigurationIndex)
        ? VehicleCounts[CurrentConfigurationIndex]
        : 0;

    Result.ActualVehicles = PeakVehicleCount;

    if (FrameTimesMs.Num() > 0)
    {
        float TotalMs = 0.0f;

        for (const float FrameTimeMs : FrameTimesMs)
        {
            TotalMs += FrameTimeMs;
            Result.MaxFrameTimeMs =
                FMath::Max(Result.MaxFrameTimeMs, FrameTimeMs);
        }

        Result.MeanFrameTimeMs =
            TotalMs / static_cast<float>(FrameTimesMs.Num());

        Result.MeanFps = Result.MeanFrameTimeMs > KINDA_SMALL_NUMBER
            ? 1000.0f / Result.MeanFrameTimeMs
            : 0.0f;

        TArray<float> Sorted = FrameTimesMs;
        Sorted.Sort();

        const int32 P95Index = FMath::Clamp(
            FMath::FloorToInt(Sorted.Num() * 0.95f),
            0,
            Sorted.Num() - 1);

        Result.P95FrameTimeMs = Sorted[P95Index];
    }

    if (SimulationTimesMs.Num() > 0)
    {
        float TotalSimulationMs = 0.0f;

        for (const float SimulationMs : SimulationTimesMs)
        {
            TotalSimulationMs += SimulationMs;
        }

        Result.MeanSimulationMs =
            TotalSimulationMs / static_cast<float>(SimulationTimesMs.Num());

        TArray<float> SortedSimulation = SimulationTimesMs;
        SortedSimulation.Sort();

        const int32 P95Index = FMath::Clamp(
            FMath::FloorToInt(SortedSimulation.Num() * 0.95f),
            0,
            SortedSimulation.Num() - 1);

        Result.P95SimulationMs = SortedSimulation[P95Index];

        if (Result.ActualVehicles > 0)
        {
            Result.MicrosecondsPerVehicle =
                Result.MeanSimulationMs * 1000.0f /
                static_cast<float>(Result.ActualVehicles);
        }
    }

    if (StatSampleCount > 0)
    {
        Result.MeanFlowFraction =
            static_cast<float>(FlowFractionTotal / StatSampleCount);

        Result.MeanStoppedVehicles =
            static_cast<float>(StoppedTotal / StatSampleCount);
    }

    UE_LOG(
        LogTemp,
        Display,
        TEXT(
            "%s configuration %d: %d vehicles, sim %.3f ms "
            "(%.1f us/vehicle), frame %.2f ms (%.0f fps), flow %.0f%%."),
        *GetName(),
        CurrentConfigurationIndex,
        Result.ActualVehicles,
        Result.MeanSimulationMs,
        Result.MicrosecondsPerVehicle,
        Result.MeanFrameTimeMs,
        Result.MeanFps,
        Result.MeanFlowFraction * 100.0f);

    const int32 NextIndex = CurrentConfigurationIndex + 1;

    if (VehicleCounts.IsValidIndex(NextIndex))
    {
        BeginConfiguration(NextIndex);
        return;
    }

    StopBenchmark();
}

void ATrafficBenchmarkRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bShowStatus)
    {
        DrawStatus();
    }

    if (Phase != ETrafficBenchmarkPhase::WarmUp &&
        Phase != ETrafficBenchmarkPhase::Measuring)
    {
        return;
    }

    PhaseElapsedSeconds += DeltaSeconds;

    if (Phase == ETrafficBenchmarkPhase::WarmUp)
    {
        if (PhaseElapsedSeconds >= WarmUpSeconds)
        {
            PhaseElapsedSeconds = 0.0f;
            Phase = ETrafficBenchmarkPhase::Measuring;
        }

        return;
    }

    FrameTimesMs.Add(DeltaSeconds * 1000.0f);

    if (IsValid(CurrentNetwork))
    {
        SimulationTimesMs.Add(
            static_cast<float>(
                CurrentNetwork->ConsumeSimulationTimeSeconds() * 1000.0));

        const FTrafficNetworkStats Stats = CurrentNetwork->GetNetworkStats();

        FlowFractionTotal += Stats.MeanSpeedFraction;
        StoppedTotal += Stats.StoppedVehicles;
        ++StatSampleCount;

        PeakVehicleCount =
            FMath::Max(PeakVehicleCount, Stats.TotalVehicles);
    }

    if (PhaseElapsedSeconds >= MeasureSeconds)
    {
        FinishConfiguration();
    }
}

void ATrafficBenchmarkRunner::DrawStatus() const
{
    if (!GEngine)
    {
        return;
    }

    TStringBuilder<256> Status;

    switch (Phase)
    {
    case ETrafficBenchmarkPhase::WarmUp:
        Status.Appendf(
            TEXT("BENCHMARK  config %d/%d  warm-up %.0f / %.0fs"),
            CurrentConfigurationIndex + 1,
            VehicleCounts.Num(),
            PhaseElapsedSeconds,
            WarmUpSeconds);
        break;

    case ETrafficBenchmarkPhase::Measuring:
        Status.Appendf(
            TEXT("BENCHMARK  config %d/%d  measuring %.0f / %.0fs  %d frames"),
            CurrentConfigurationIndex + 1,
            VehicleCounts.Num(),
            PhaseElapsedSeconds,
            MeasureSeconds,
            FrameTimesMs.Num());
        break;

    case ETrafficBenchmarkPhase::Finished:
        Status.Appendf(
            TEXT("BENCHMARK  finished, %d configurations"),
            Results.Num());
        break;

    default:
        return;
    }

    GEngine->AddOnScreenDebugMessage(
        StatusMessageKey,
        0.0f,
        FColor::Cyan,
        Status.ToString());
}

void ATrafficBenchmarkRunner::ExportCsv()
{
    if (Results.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("%s has no results to export."),
            *GetName());

        return;
    }

    TStringBuilder<2048> Csv;

    Csv.Append(
        TEXT("RequestedVehicles,ActualVehicles,MeanSimulationMs,")
        TEXT("P95SimulationMs,MicrosecondsPerVehicle,MeanFrameTimeMs,")
        TEXT("P95FrameTimeMs,MaxFrameTimeMs,MeanFps,MeanFlowFraction,")
        TEXT("MeanStoppedVehicles\n"));

    for (const FTrafficBenchmarkResult& Result : Results)
    {
        Csv.Appendf(
            TEXT("%d,%d,%.4f,%.4f,%.2f,%.3f,%.3f,%.3f,%.1f,%.4f,%.1f\n"),
            Result.RequestedVehicles,
            Result.ActualVehicles,
            Result.MeanSimulationMs,
            Result.P95SimulationMs,
            Result.MicrosecondsPerVehicle,
            Result.MeanFrameTimeMs,
            Result.P95FrameTimeMs,
            Result.MaxFrameTimeMs,
            Result.MeanFps,
            Result.MeanFlowFraction,
            Result.MeanStoppedVehicles);
    }

    const FString OutputPath =
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("TrafficSim"),
            CsvFileName.IsEmpty()
                ? TEXT("Benchmark.csv")
                : CsvFileName);

    if (FFileHelper::SaveStringToFile(Csv.ToString(), *OutputPath))
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("%s wrote %d results to %s."),
            *GetName(),
            Results.Num(),
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
