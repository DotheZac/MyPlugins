#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

enum class EGraphicsCVarProfileGPUQueueType : uint8
{
	Copy,
	Compute,
	Graphics,
	Unknown
};

enum class EGraphicsCVarProfileGPUCaptureTarget : uint8
{
	Baseline,
	Candidate
};

enum class EGraphicsCVarProfileGPUContextObjectType : uint8
{
	Asset,
	Actor
};

struct FGraphicsCVarProfileGPUContextObject
{
	EGraphicsCVarProfileGPUContextObjectType Type =
		EGraphicsCVarProfileGPUContextObjectType::Asset;
	FString DisplayName;
	FString ObjectPath;
	FString ClassPath;
	int32 Quantity = 1;
	TArray<FString> AnalysisDetails;
};

struct FGraphicsCVarProfileGPUPass
{
	FString Name;
	FString Path;
	int32 Depth = 0;
	double ExclusiveMs = 0.0;
	double InclusiveMs = 0.0;
	double ExclusivePercent = 0.0;
	double InclusivePercent = 0.0;
	int64 ExclusiveDraws = 0;
	int64 ExclusiveDispatches = 0;
	int64 ExclusivePrimitives = 0;
	int64 ExclusiveVertices = 0;
	int64 InclusiveDraws = 0;
	int64 InclusiveDispatches = 0;
	int64 InclusivePrimitives = 0;
	int64 InclusiveVertices = 0;
};

struct FGraphicsCVarProfileGPUQueueCapture
{
	EGraphicsCVarProfileGPUQueueType Type =
		EGraphicsCVarProfileGPUQueueType::Unknown;
	int32 QueueIndex = 0;
	int32 GPUIndex = 0;
	double FrameTimeMs = 0.0;
	TArray<FGraphicsCVarProfileGPUPass> Passes;
};

struct FGraphicsCVarProfileGPUCapture
{
	FDateTime CapturedAt;
	int32 FrameNumber = INDEX_NONE;
	TArray<FGraphicsCVarProfileGPUQueueCapture> Queues;
	bool bIsValid = false;
};

struct FGraphicsCVarProfileGPUCaptureSet
{
	FDateTime StartedAt;
	FString Memo;
	TArray<FGraphicsCVarProfileGPUContextObject> ContextObjects;
	int32 RequestedSamples = 1;
	double IntervalSeconds = 0.0;
	TArray<FGraphicsCVarProfileGPUCapture> Samples;
	bool bIsValid = false;
};

class FGraphicsCVarProfileGPUCaptureService final : public FOutputDevice
{
public:
	static FGraphicsCVarProfileGPUCaptureService& Get();

	bool StartSingleCapture(
		EGraphicsCVarProfileGPUCaptureTarget Target,
		bool bShowVisualizer);
	bool StartCapture(
		EGraphicsCVarProfileGPUCaptureTarget Target,
		int32 SampleCount,
		double IntervalSeconds,
		bool bShowVisualizer,
		const FString& Memo,
		const TArray<FGraphicsCVarProfileGPUContextObject>& ContextObjects);
	void CancelCapture();
	void ClearCaptures();
	void Shutdown();

	bool IsCapturing() const { return bIsCapturing; }
	const FString& GetStatus() const { return Status; }
	const FGraphicsCVarProfileGPUCapture& GetLastCapture() const
	{
		return LastCapture;
	}
	const FGraphicsCVarProfileGPUCaptureSet& GetBaseline() const { return Baseline; }
	const FGraphicsCVarProfileGPUCaptureSet& GetCandidate() const { return Candidate; }
	EGraphicsCVarProfileGPUCaptureTarget GetLastTarget() const { return LastTarget; }
	int32 GetCompletedSampleCount() const { return PendingSet.Samples.Num(); }
	int32 GetRequestedSampleCount() const { return PendingSet.RequestedSamples; }
	uint64 GetResultRevision() const { return ResultRevision; }

	virtual void Serialize(
		const TCHAR* Message,
		ELogVerbosity::Type Verbosity,
		const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }

private:
	struct FCVarRestoreValue
	{
		FString Name;
		FString Value;
		uint32 SetByFlags = 0;
	};

	bool Tick(float DeltaTime);
	bool StartNextSample();
	bool CompleteCurrentSample();
	void FinishCaptureSet();
	void FailCapture(const FString& Error);
	void RemoveTicker();
	void ApplyCaptureCVars(bool bShowVisualizer);
	void RestoreCaptureCVars();
	void SetTemporaryCVar(const TCHAR* Name, const TCHAR* Value);
	bool ParseCapturedLines(
		const TArray<FString>& Lines,
		FGraphicsCVarProfileGPUCapture& OutCapture,
		FString& OutError) const;

	static bool ParseProfileHeader(
		const FString& Line,
		int32& OutFrameNumber,
		EGraphicsCVarProfileGPUQueueType& OutQueueType,
		int32& OutQueueIndex,
		int32& OutGPUIndex);
	static bool ParseFrameTime(const FString& Line, double& OutFrameTimeMs);
	static bool ParsePassRow(
		const FString& Line,
		FGraphicsCVarProfileGPUPass& OutPass);

	TAtomic<bool> bCollectLogLines{ false };
	bool bIsCapturing = false;
	bool bObservedProfilerActive = false;
	bool bWaitingForNextSample = false;
	EGraphicsCVarProfileGPUCaptureTarget PendingTarget =
		EGraphicsCVarProfileGPUCaptureTarget::Baseline;
	EGraphicsCVarProfileGPUCaptureTarget LastTarget =
		EGraphicsCVarProfileGPUCaptureTarget::Baseline;
	double CaptureElapsedSeconds = 0.0;
	double IntervalRemainingSeconds = 0.0;
	FString Status = TEXT("Ready");
	FGraphicsCVarProfileGPUCapture LastCapture;
	FGraphicsCVarProfileGPUCaptureSet PendingSet;
	FGraphicsCVarProfileGPUCaptureSet Baseline;
	FGraphicsCVarProfileGPUCaptureSet Candidate;
	TArray<FString> CapturedLines;
	FCriticalSection CapturedLinesMutex;
	TArray<FCVarRestoreValue> RestoreValues;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 ResultRevision = 0;
};
