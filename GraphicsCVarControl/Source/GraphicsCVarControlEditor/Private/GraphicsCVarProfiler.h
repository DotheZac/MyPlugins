#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

enum class EGraphicsCVarCaptureTarget : uint8
{
	Baseline,
	Candidate
};

struct FGraphicsCVarPassSnapshot
{
	FString Id;
	FString DisplayName;
	double AverageMs = 0.0;
	double MinMs = 0.0;
	double MaxMs = 0.0;
};

struct FGraphicsCVarSpikeSettings
{
	bool bEnabled = true;
	double FrameBudgetMs = 16.67;
	double DeltaThresholdMs = 2.0;
	int32 RollingWindowFrames = 120;
	int32 PreFrames = 30;
	int32 PostFrames = 60;
	int32 MaxEvents = 20;
};

struct FGraphicsCVarFramePassSample
{
	int32 FrameIndex = 0;
	double TotalGPUFrameMs = 0.0;
	bool bPassDataValid = false;
	TMap<FString, FGraphicsCVarPassSnapshot> Passes;
};

struct FGraphicsCVarSpikePassDelta
{
	FString Id;
	FString DisplayName;
	double BaselineMs = 0.0;
	double PeakMs = 0.0;
	double DeltaMs = 0.0;
	TOptional<double> ChangePercent;
};

struct FGraphicsCVarSpikeEvent
{
	int32 EventIndex = 0;
	int32 StartFrame = 0;
	int32 PeakFrame = 0;
	int32 LastSpikeFrame = 0;
	int32 WindowStartFrame = 0;
	int32 WindowEndFrame = 0;
	bool bHasAlignedPassSample = false;
	int32 PassSampleFrame = INDEX_NONE;
	int32 PassFrameOffset = 0;
	double BaselineTotalMs = 0.0;
	double PeakTotalMs = 0.0;
	double DeltaTotalMs = 0.0;
	TArray<FGraphicsCVarSpikePassDelta> PassDeltas;
	TArray<FGraphicsCVarFramePassSample> FrameSamples;
};

struct FGraphicsCVarSnapshot
{
	FString CaptureId;
	FString Label;
	FString CaptureMode;
	FDateTime CapturedAt;
	int32 SampleFrames = 0;
	int32 TargetFrames = 0;
	TMap<FString, FString> CVarValues;
	TArray<double> GPUFrameSamples;
	double AverageGPUFrameMs = 0.0;
	double MinGPUFrameMs = 0.0;
	double MaxGPUFrameMs = 0.0;
	int32 PassStatHistoryFrames = 20;
	int32 PassSampleAttempts = 0;
	int32 ValidPassSamples = 0;
	TMap<FString, FGraphicsCVarPassSnapshot> Passes;
	FGraphicsCVarSpikeSettings SpikeSettings;
	TArray<FGraphicsCVarSpikeEvent> SpikeEvents;
	bool bIsValid = false;
};

struct FGraphicsCVarPassComparison
{
	FString Id;
	FString DisplayName;
	bool bHasBaseline = false;
	bool bHasCandidate = false;
	double BaselineMs = 0.0;
	double BaselineMinMs = 0.0;
	double BaselineMaxMs = 0.0;
	double CandidateMs = 0.0;
	double CandidateMinMs = 0.0;
	double CandidateMaxMs = 0.0;
	double DeltaMs = 0.0;
	TOptional<double> ChangePercent;
};

struct FGraphicsCVarPassAccumulator
{
	FString Id;
	FString DisplayName;
	double SumMs = 0.0;
	double MinMs = TNumericLimits<double>::Max();
	double MaxMs = 0.0;
	int32 SampleCount = 0;
};

class FGraphicsCVarProfiler
{
public:
	static FGraphicsCVarProfiler& Get();

	bool StartCapture(
		EGraphicsCVarCaptureTarget Target,
		const TMap<FString, FString>& CVarValues,
		int32 SampleFrames);
	bool StartContinuousCapture(
		EGraphicsCVarCaptureTarget Target,
		const TMap<FString, FString>& CVarValues,
		int32 TargetFrames,
		const FGraphicsCVarSpikeSettings& SpikeSettings);
	void StopCapture();
	void ClearSnapshots();
	void Shutdown();

	bool IsCapturing() const { return bIsCapturing; }
	bool IsContinuousCapture() const { return bIsCapturing && bIsContinuousCapture; }
	bool HasTargetFrameLimit() const
	{
		return IsContinuousCapture() && RequestedSampleFrames > 0;
	}
	EGraphicsCVarCaptureTarget GetActiveTarget() const { return ActiveTarget; }
	const TArray<double>& GetActiveGPUFrameSamples() const { return GPUFrameSamples; }
	const TArray<FGraphicsCVarSpikeEvent>& GetActiveSpikeEvents() const
	{
		return PendingSnapshot.SpikeEvents;
	}
	float GetProgress() const;
	FText GetStatusText() const;
	uint64 GetResultRevision() const { return ResultRevision; }

	const FGraphicsCVarSnapshot& GetBaseline() const { return Baseline; }
	const FGraphicsCVarSnapshot& GetCandidate() const { return Candidate; }
	TArray<FGraphicsCVarPassComparison> BuildComparison() const;

private:
	static constexpr int32 WarmupFrames = 10;
	static constexpr int32 SpikePassAlignmentFrames = 5;

	bool StartCaptureInternal(
		EGraphicsCVarCaptureTarget Target,
		const TMap<FString, FString>& CVarValues,
		int32 SampleFrames,
		bool bContinuous,
		const FGraphicsCVarSpikeSettings& SpikeSettings);
	bool Tick(float DeltaTime);
	void FinishCapture();
	void AccumulateCurrentPassSamples(
		const TMap<FString, FGraphicsCVarPassSnapshot>& CurrentPasses);
	void ProcessSpikeSample(
		double GPUFrameMs,
		const TMap<FString, FGraphicsCVarPassSnapshot>& CurrentPasses);
	void UpdateSpikePeak(
		FGraphicsCVarSpikeEvent& Event,
		const FGraphicsCVarFramePassSample& CurrentFrame,
		double RollingMedianMs);
	void TryAlignSpikePassSample(
		FGraphicsCVarSpikeEvent& Event,
		const FGraphicsCVarFramePassSample& CandidateFrame);
	void BuildSpikePassDeltas(
		FGraphicsCVarSpikeEvent& Event,
		const FGraphicsCVarFramePassSample& PassFrame);
	static void AppendTotalOnlyFrameSample(
		FGraphicsCVarSpikeEvent& Event,
		const FGraphicsCVarFramePassSample& Frame);
	double CalculateRollingMedian() const;
	void BuildRollingPassAverages(
		TMap<FString, FGraphicsCVarPassSnapshot>& OutAverages,
		int32& OutValidFrameCount) const;
	void BuildAccumulatedPassSnapshots(TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses) const;
	void SetGPUStatEnabledForCapture(int32 SampleFrames);
	void RestoreGPUStatState();
	static void ExecuteStatCommand(const FString& Command);
	static void ReadGPUPassSnapshot(TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses);
	static bool IsGPUPassSnapshotValid(
		const TMap<FString, FGraphicsCVarPassSnapshot>& Passes);

	bool bIsCapturing = false;
	bool bIsContinuousCapture = false;
	bool bGPUStatWasEnabled = false;
	EGraphicsCVarCaptureTarget ActiveTarget = EGraphicsCVarCaptureTarget::Baseline;
	int32 RequestedSampleFrames = 60;
	int32 FramesElapsed = 0;
	TArray<double> GPUFrameSamples;
	TMap<FString, FGraphicsCVarPassAccumulator> PassAccumulators;
	TArray<FGraphicsCVarFramePassSample> RollingFrameSamples;
	int32 ActiveSpikeEventIndex = INDEX_NONE;
	FGraphicsCVarSnapshot PendingSnapshot;
	FGraphicsCVarSnapshot Baseline;
	FGraphicsCVarSnapshot Candidate;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 ResultRevision = 0;
	FString LastStatus;
};
