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
	FDateTime CapturedAt;
	int32 SampleFrames = 0;
	TMap<FString, FString> CVarValues;
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
	double CandidateMs = 0.0;
	double DeltaMs = 0.0;
	TOptional<double> ChangePercent;
};

class FGraphicsCVarProfiler
{
public:
	static FGraphicsCVarProfiler& Get();

	bool StartCapture(
		EGraphicsCVarCaptureTarget Target,
		const TMap<FString, FString>& CVarValues,
		int32 SampleFrames);
	void ClearSnapshots();
	void Shutdown();

	bool IsCapturing() const { return bIsCapturing; }
	float GetProgress() const;
	FText GetStatusText() const;
	uint64 GetResultRevision() const { return ResultRevision; }

	const FGraphicsCVarSnapshot& GetBaseline() const { return Baseline; }
	const FGraphicsCVarSnapshot& GetCandidate() const { return Candidate; }
	TArray<FGraphicsCVarPassComparison> BuildComparison() const;

private:
	static constexpr int32 WarmupFrames = 10;

	bool Tick(float DeltaTime);
	void FinishCapture();
	void SetGPUStatEnabledForCapture(int32 SampleFrames);
	void RestoreGPUStatState();
	static void ExecuteStatCommand(const FString& Command);
	static void ReadGPUPassSnapshot(TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses);

	bool bIsCapturing = false;
	bool bGPUStatWasEnabled = false;
	EGraphicsCVarCaptureTarget ActiveTarget = EGraphicsCVarCaptureTarget::Baseline;
	int32 RequestedSampleFrames = 60;
	int32 FramesElapsed = 0;
	TArray<double> GPUFrameSamples;
	FGraphicsCVarSnapshot PendingSnapshot;
	FGraphicsCVarSnapshot Baseline;
	FGraphicsCVarSnapshot Candidate;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 ResultRevision = 0;
	FString LastStatus;
};
