#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrafficBenchmarkRunner.generated.h"

class ATrafficDemoSceneBuilder;
class ATrafficRoadNetwork;
class USceneComponent;
class UBillboardComponent;

// One measured configuration.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficBenchmarkResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    int32 RequestedVehicles = 0;

    // What the network could actually hold, which may be lower.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    int32 ActualVehicles = 0;

    // Cost of the traffic simulation itself. Unlike frame time this is not
    // masked by a frame rate cap, so it is the figure worth quoting.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MeanSimulationMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float P95SimulationMs = 0.0f;

    // Microseconds of simulation per vehicle per frame; near-flat means the
    // cost scales linearly with population, rising means it does not.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MicrosecondsPerVehicle = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MeanFrameTimeMs = 0.0f;

    // The number that matters for smoothness: the worst 5% of frames.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float P95FrameTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MaxFrameTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MeanFps = 0.0f;

    // Recorded alongside the timings because a saturated network is not
    // measuring the same workload as a free-flowing one; a frame time without
    // this number beside it is not interpretable.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MeanFlowFraction = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Benchmark")
    float MeanStoppedVehicles = 0.0f;
};

UENUM()
enum class ETrafficBenchmarkPhase : uint8
{
    Idle,
    Building,
    WarmUp,
    Measuring,
    Finished
};

// Sweeps a series of vehicle populations, rebuilding the scene at each one and
// recording frame timings once it has settled. Writes a CSV summary so the
// numbers can be quoted rather than estimated.
UCLASS()
class TRAFFICSIMULATION_API ATrafficBenchmarkRunner : public AActor
{
    GENERATED_BODY()

public:
    ATrafficBenchmarkRunner();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Benchmark")
    void StartBenchmark();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Benchmark")
    void StopBenchmark();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Benchmark")
    void ExportCsv();

    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadWrite,
        Category = "Traffic Benchmark|Setup")
    TObjectPtr<ATrafficDemoSceneBuilder> SceneBuilder;

private:
    void BeginConfiguration(int32 ConfigurationIndex);

    void FinishConfiguration();

    void DrawStatus() const;

    // Debug drawing costs real frame time, so it is silenced during a run to
    // keep the measurement about the simulation rather than the visualisation.
    void SetDebugDrawingEnabled(bool bEnabled) const;

    void ApplyFrameRateOverride();

    void RestoreFrameRateOverride();

    UPROPERTY(VisibleAnywhere, Category = "Traffic Benchmark")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Traffic Benchmark")
    TObjectPtr<UBillboardComponent> EditorIcon;
#endif

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Setup")
    bool bAutoStartOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Setup")
    TArray<int32> VehicleCounts = { 50, 100, 200, 350, 500 };

    // Time after each rebuild before measuring, so spawn transients and the
    // first signal cycle do not land in the numbers.
    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Benchmark|Setup",
        meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
    float WarmUpSeconds = 8.0f;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Benchmark|Setup",
        meta = (ClampMin = "1.0", UIMin = "1.0", Units = "s"))
    float MeasureSeconds = 20.0f;

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Setup")
    bool bDisableDebugDrawingDuringRun = true;

    // Lifts any frame rate limit for the duration of the run and restores it
    // afterwards. Without this the engine idles to hit the cap and every
    // frame time reads the same regardless of load, which silently makes the
    // whole benchmark meaningless.
    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Setup")
    bool bRemoveFrameRateCapDuringRun = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Output")
    bool bExportCsvOnFinish = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Output")
    FString CsvFileName = TEXT("Benchmark.csv");

    UPROPERTY(EditAnywhere, Category = "Traffic Benchmark|Output")
    bool bShowStatus = true;

    UPROPERTY(
        VisibleInstanceOnly,
        Category = "Traffic Benchmark|Results",
        meta = (AllowPrivateAccess = "true"))
    TArray<FTrafficBenchmarkResult> Results;

    UPROPERTY(Transient)
    TObjectPtr<ATrafficRoadNetwork> CurrentNetwork;

    ETrafficBenchmarkPhase Phase = ETrafficBenchmarkPhase::Idle;
    int32 CurrentConfigurationIndex = INDEX_NONE;
    float PhaseElapsedSeconds = 0.0f;

    TArray<float> FrameTimesMs;
    TArray<float> SimulationTimesMs;
    double FlowFractionTotal = 0.0;
    double StoppedTotal = 0.0;
    int32 StatSampleCount = 0;
    int32 PeakVehicleCount = 0;

    float PreviousMaxFps = -1.0f;
    int32 PreviousVSync = 0;
    bool bFrameRateOverridden = false;

    static constexpr uint64 StatusMessageKey = 0x7A11C002;
};
