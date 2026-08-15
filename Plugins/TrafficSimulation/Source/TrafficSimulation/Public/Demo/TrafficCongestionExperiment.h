#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Junctions/TrafficJunction.h"
#include "TrafficCongestionExperiment.generated.h"

class ATrafficRoadNetwork;
class USceneComponent;
class UBillboardComponent;

// One step of the scripted timeline. Entering a stage swaps the junction's
// signal plan wholesale, which is the lever used to induce and then relieve
// congestion without touching the simulation itself.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficCongestionStage
{
    GENERATED_BODY()

    // Shown on screen while the stage runs, and written into the CSV.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic Congestion")
    FString Name;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Traffic Congestion",
        meta = (ClampMin = "1.0", UIMin = "1.0", Units = "s"))
    float DurationSeconds = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic Congestion")
    TArray<FTrafficSignalPhase> Phases;
};

// One row of the measured time series.
USTRUCT(BlueprintType)
struct TRAFFICSIMULATION_API FTrafficCongestionSample
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    float TimeSeconds = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    FString StageName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    int32 TotalVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    int32 StoppedVehicles = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    int32 VehiclesYielding = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    float MeanSpeedCmPerSecond = 0.0f;

    // The headline congestion number: 1.0 is unimpeded, 0.0 is gridlock.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    float MeanSpeedFraction = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    float FrameTimeMs = 0.0f;

    // Vehicles per minute clearing the junction, measured over the sampling
    // interval. This is the hard ceiling on how fast a queue can drain.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic Congestion")
    float JunctionThroughputPerMinute = 0.0f;
};

// Runs a scripted congestion demonstration: balanced signals, then a plan
// that starves one pair of approaches until a queue builds and backs up, then
// balanced signals again so the queue drains. Samples network statistics
// throughout and can write them out as CSV.
UCLASS()
class TRAFFICSIMULATION_API ATrafficCongestionExperiment : public AActor
{
    GENERATED_BODY()

public:
    ATrafficCongestionExperiment();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Congestion")
    void StartExperiment();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Congestion")
    void StopExperiment();

    // Writes the samples collected so far to Saved/TrafficSim/<CsvFileName>.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Traffic Congestion")
    void ExportCsv();

    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadWrite,
        Category = "Traffic Congestion|Setup")
    TObjectPtr<ATrafficRoadNetwork> RoadNetwork;

    // Leave unset to use the first junction found that belongs to RoadNetwork.
    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadWrite,
        Category = "Traffic Congestion|Setup")
    TObjectPtr<ATrafficJunction> Junction;

private:
    void EnterStage(int32 StageIndex);

    void RecordSample(float DeltaSeconds);

    void DrawStatus() const;

    bool ResolveJunction();

    UPROPERTY(VisibleAnywhere, Category = "Traffic Congestion")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Traffic Congestion")
    TObjectPtr<UBillboardComponent> EditorIcon;
#endif

    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Setup")
    bool bAutoStartOnBeginPlay = true;

    // Restart from the first stage instead of stopping at the end. Useful for
    // leaving the scene running while recording.
    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Setup")
    bool bLoop = false;

    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Schedule")
    TArray<FTrafficCongestionStage> Stages;

    UPROPERTY(
        EditAnywhere,
        Category = "Traffic Congestion|Measurement",
        meta = (ClampMin = "0.05", UIMin = "0.05", Units = "s"))
    float SampleIntervalSeconds = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Measurement")
    bool bExportCsvOnFinish = true;

    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Measurement")
    FString CsvFileName = TEXT("CongestionExperiment.csv");

    UPROPERTY(EditAnywhere, Category = "Traffic Congestion|Display")
    bool bShowStatus = true;

    UPROPERTY(
        VisibleInstanceOnly,
        Category = "Traffic Congestion|Runtime",
        meta = (AllowPrivateAccess = "true"))
    TArray<FTrafficCongestionSample> Samples;

    // Grant count at the previous sample, so throughput can be derived from
    // the delta rather than needing its own instrumentation.
    int32 LastSampledGrantCount = 0;
    float LastSampleTimeSeconds = 0.0f;

    int32 CurrentStageIndex = INDEX_NONE;
    float StageElapsedSeconds = 0.0f;
    float TotalElapsedSeconds = 0.0f;
    float SampleAccumulatorSeconds = 0.0f;
    bool bRunning = false;

    // Stable key so the status line replaces itself each frame rather than
    // stacking. Distinct from the debug overlay's own key.
    static constexpr uint64 StatusMessageKey = 0x7A11C001;
};
