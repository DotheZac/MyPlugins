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

struct FGraphicsCVarSnapshot
{
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
	TMap<FString, FGraphicsCVarPassSnapshot> Passes;
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
		int32 TargetFrames);
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
	float GetProgress() const;
	FText GetStatusText() const;
	uint64 GetResultRevision() const { return ResultRevision; }

	const FGraphicsCVarSnapshot& GetBaseline() const { return Baseline; }
	const FGraphicsCVarSnapshot& GetCandidate() const { return Candidate; }
	TArray<FGraphicsCVarPassComparison> BuildComparison() const;

private:
	static constexpr int32 WarmupFrames = 10;

	bool StartCaptureInternal(
		EGraphicsCVarCaptureTarget Target,
		const TMap<FString, FString>& CVarValues,
		int32 SampleFrames,
		bool bContinuous);
	bool Tick(float DeltaTime);
	void FinishCapture();
	void AccumulateCurrentPassSamples();
	void BuildAccumulatedPassSnapshots(TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses) const;
	void SetGPUStatEnabledForCapture(int32 SampleFrames);
	void RestoreGPUStatState();
	static void ExecuteStatCommand(const FString& Command);
	static void ReadGPUPassSnapshot(TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses);

	bool bIsCapturing = false;
	bool bIsContinuousCapture = false;
	bool bGPUStatWasEnabled = false;
	EGraphicsCVarCaptureTarget ActiveTarget = EGraphicsCVarCaptureTarget::Baseline;
	int32 RequestedSampleFrames = 60;
	int32 FramesElapsed = 0;
	TArray<double> GPUFrameSamples;
	TMap<FString, FGraphicsCVarPassAccumulator> PassAccumulators;
	FGraphicsCVarSnapshot PendingSnapshot;
	FGraphicsCVarSnapshot Baseline;
	FGraphicsCVarSnapshot Candidate;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 ResultRevision = 0;
	FString LastStatus;
};
