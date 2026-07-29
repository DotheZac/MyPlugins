#include "GraphicsCVarProfiler.h"

#include "DynamicRHI.h"
#include "Editor.h"
#include "GPUProfiler.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Stats/StatsData.h"
#include "Stats/StatsSystemTypes.h"

FGraphicsCVarProfiler& FGraphicsCVarProfiler::Get()
{
	static FGraphicsCVarProfiler Instance;
	return Instance;
}

bool FGraphicsCVarProfiler::StartCapture(
	const EGraphicsCVarCaptureTarget Target,
	const TMap<FString, FString>& CVarValues,
	const int32 SampleFrames)
{
	if (bIsCapturing)
	{
		return false;
	}

	ActiveTarget = Target;
	RequestedSampleFrames = FMath::Clamp(SampleFrames, 10, 600);
	FramesElapsed = 0;
	GPUFrameSamples.Reset(RequestedSampleFrames);
	PendingSnapshot = {};
	PendingSnapshot.Label = Target == EGraphicsCVarCaptureTarget::Baseline
		? TEXT("Baseline")
		: TEXT("Candidate");
	PendingSnapshot.CVarValues = CVarValues;
	PendingSnapshot.SampleFrames = RequestedSampleFrames;
	LastStatus = FString::Printf(
		TEXT("%s warm-up: 0 / %d"),
		*PendingSnapshot.Label,
		WarmupFrames);

	SetGPUStatEnabledForCapture(RequestedSampleFrames);
	bIsCapturing = true;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FGraphicsCVarProfiler::Tick));
	return true;
}

void FGraphicsCVarProfiler::ClearSnapshots()
{
	if (bIsCapturing)
	{
		return;
	}

	Baseline = {};
	Candidate = {};
	LastStatus = TEXT("Baseline and Candidate cleared");
	++ResultRevision;
}

void FGraphicsCVarProfiler::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (bIsCapturing)
	{
		RestoreGPUStatState();
	}

	bIsCapturing = false;
}

float FGraphicsCVarProfiler::GetProgress() const
{
	if (!bIsCapturing)
	{
		return 0.0f;
	}

	return FMath::Clamp(
		static_cast<float>(FramesElapsed) /
			static_cast<float>(WarmupFrames + RequestedSampleFrames),
		0.0f,
		1.0f);
}

FText FGraphicsCVarProfiler::GetStatusText() const
{
	if (bIsCapturing)
	{
		return FText::FromString(LastStatus);
	}

	if (!LastStatus.IsEmpty())
	{
		return FText::FromString(LastStatus);
	}

	return FText::FromString(TEXT("Ready"));
}

bool FGraphicsCVarProfiler::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	if (!bIsCapturing)
	{
		TickerHandle.Reset();
		return false;
	}

	++FramesElapsed;
	if (FramesElapsed <= WarmupFrames)
	{
		LastStatus = FString::Printf(
			TEXT("%s warm-up: %d / %d"),
			*PendingSnapshot.Label,
			FramesElapsed,
			WarmupFrames);
		return true;
	}

	const double GPUFrameMs = FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles());
	GPUFrameSamples.Add(GPUFrameMs);
	LastStatus = FString::Printf(
		TEXT("%s capture: %d / %d frames"),
		*PendingSnapshot.Label,
		GPUFrameSamples.Num(),
		RequestedSampleFrames);

	if (GPUFrameSamples.Num() >= RequestedSampleFrames)
	{
		FinishCapture();
		TickerHandle.Reset();
		return false;
	}

	return true;
}

void FGraphicsCVarProfiler::FinishCapture()
{
	if (GPUFrameSamples.IsEmpty())
	{
		LastStatus = FString::Printf(TEXT("%s capture failed: no GPU samples"), *PendingSnapshot.Label);
		RestoreGPUStatState();
		bIsCapturing = false;
		return;
	}

	double Sum = 0.0;
	double Min = TNumericLimits<double>::Max();
	double Max = 0.0;
	for (const double Sample : GPUFrameSamples)
	{
		Sum += Sample;
		Min = FMath::Min(Min, Sample);
		Max = FMath::Max(Max, Sample);
	}

	PendingSnapshot.AverageGPUFrameMs = Sum / static_cast<double>(GPUFrameSamples.Num());
	PendingSnapshot.MinGPUFrameMs = Min;
	PendingSnapshot.MaxGPUFrameMs = Max;
	PendingSnapshot.CapturedAt = FDateTime::Now();
	ReadGPUPassSnapshot(PendingSnapshot.Passes);
	PendingSnapshot.bIsValid = true;

	if (ActiveTarget == EGraphicsCVarCaptureTarget::Baseline)
	{
		Baseline = MoveTemp(PendingSnapshot);
	}
	else
	{
		Candidate = MoveTemp(PendingSnapshot);
	}

	const FGraphicsCVarSnapshot& Completed =
		ActiveTarget == EGraphicsCVarCaptureTarget::Baseline ? Baseline : Candidate;
	LastStatus = FString::Printf(
		TEXT("%s captured: %.3f ms, %d GPU passes"),
		*Completed.Label,
		Completed.AverageGPUFrameMs,
		Completed.Passes.Num());

	RestoreGPUStatState();
	bIsCapturing = false;
	++ResultRevision;
}

void FGraphicsCVarProfiler::SetGPUStatEnabledForCapture(const int32 SampleFrames)
{
	bool bCurrentEnabled = false;
	bool bOthersEnabled = false;
	FCoreDelegates::StatCheckEnabled.Broadcast(
		TEXT("STATGROUP_GPU"),
		bCurrentEnabled,
		bOthersEnabled);
	bGPUStatWasEnabled = bCurrentEnabled;

	if (bCurrentEnabled)
	{
		ExecuteStatCommand(TEXT("stat gpu"));
	}

	ExecuteStatCommand(FString::Printf(
		TEXT("stat gpu -maxhistoryframes=%d"),
		SampleFrames));
}

void FGraphicsCVarProfiler::RestoreGPUStatState()
{
	if (!bGPUStatWasEnabled)
	{
		ExecuteStatCommand(TEXT("stat gpu"));
	}
}

void FGraphicsCVarProfiler::ExecuteStatCommand(const FString& Command)
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	GEditor->Exec(World, *Command);
}

void FGraphicsCVarProfiler::ReadGPUPassSnapshot(
	TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses)
{
	OutPasses.Reset();

#if STATS && RHI_NEW_GPU_PROFILER
	const FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest;
	if (!StatsData)
	{
		return;
	}

	for (int32 GroupIndex = 0; GroupIndex < StatsData->ActiveStatGroups.Num(); ++GroupIndex)
	{
		const FActiveStatGroupInfo& Group = StatsData->ActiveStatGroups[GroupIndex];
		FString QueueDescription;
		if (StatsData->GroupDescriptions.IsValidIndex(GroupIndex))
		{
			QueueDescription = StatsData->GroupDescriptions[GroupIndex];
			QueueDescription.RemoveFromEnd(TEXT(" Timing"));
		}

		for (const FComplexStatMessage& Message : Group.GpuStatsAggregate)
		{
			FName ShortName = Message.GetShortName();
			const int32 TypeNumber = ShortName.GetNumber();
			ShortName.SetNumber(0);

			using EGPUStatType = UE::RHI::GPUProfiler::FGPUStat::EType;
			if (static_cast<EGPUStatType>(TypeNumber) != EGPUStatType::Busy)
			{
				continue;
			}

			const FString PassId = ShortName.GetPlainNameString();
			if (OutPasses.Contains(PassId))
			{
				continue;
			}

			FGraphicsCVarPassSnapshot Pass;
			Pass.Id = PassId;
			Pass.DisplayName = Message.GetDescription();
			if (Pass.DisplayName.IsEmpty())
			{
				Pass.DisplayName = PassId;
			}
			else if (Pass.DisplayName == TEXT("Queue Total") && !QueueDescription.IsEmpty())
			{
				Pass.DisplayName = FString::Printf(
					TEXT("Queue Total [%s]"),
					*QueueDescription);
			}

			if (Message.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double)
			{
				Pass.AverageMs = Message.GetValue_double(EComplexStatField::IncAve);
				Pass.MinMs = Message.GetValue_double(EComplexStatField::IncMin);
				Pass.MaxMs = Message.GetValue_double(EComplexStatField::IncMax);
			}
			else if (Message.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_int64)
			{
				Pass.AverageMs = static_cast<double>(Message.GetValue_int64(EComplexStatField::IncAve));
				Pass.MinMs = static_cast<double>(Message.GetValue_int64(EComplexStatField::IncMin));
				Pass.MaxMs = static_cast<double>(Message.GetValue_int64(EComplexStatField::IncMax));
			}

			OutPasses.Add(PassId, MoveTemp(Pass));
		}
	}
#endif
}

TArray<FGraphicsCVarPassComparison> FGraphicsCVarProfiler::BuildComparison() const
{
	TArray<FGraphicsCVarPassComparison> Result;
	if (!Baseline.bIsValid && !Candidate.bIsValid)
	{
		return Result;
	}

	{
		FGraphicsCVarPassComparison Total;
		Total.Id = TEXT("__TotalGPU");
		Total.DisplayName = TEXT("Total GPU Frame");
		Total.bHasBaseline = Baseline.bIsValid;
		Total.bHasCandidate = Candidate.bIsValid;
		Total.BaselineMs = Baseline.AverageGPUFrameMs;
		Total.CandidateMs = Candidate.AverageGPUFrameMs;
		Total.DeltaMs = Total.CandidateMs - Total.BaselineMs;
		if (Total.bHasBaseline && Total.bHasCandidate && !FMath::IsNearlyZero(Total.BaselineMs))
		{
			Total.ChangePercent = Total.DeltaMs / Total.BaselineMs * 100.0;
		}
		Result.Add(MoveTemp(Total));
	}

	TSet<FString> PassIds;
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : Baseline.Passes)
	{
		PassIds.Add(Pair.Key);
	}
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : Candidate.Passes)
	{
		PassIds.Add(Pair.Key);
	}

	TArray<FGraphicsCVarPassComparison> PassRows;
	PassRows.Reserve(PassIds.Num());
	for (const FString& PassId : PassIds)
	{
		const FGraphicsCVarPassSnapshot* BaselinePass = Baseline.Passes.Find(PassId);
		const FGraphicsCVarPassSnapshot* CandidatePass = Candidate.Passes.Find(PassId);

		FGraphicsCVarPassComparison Row;
		Row.Id = PassId;
		Row.DisplayName = BaselinePass
			? BaselinePass->DisplayName
			: CandidatePass->DisplayName;
		Row.bHasBaseline = BaselinePass != nullptr;
		Row.bHasCandidate = CandidatePass != nullptr;
		Row.BaselineMs = BaselinePass ? BaselinePass->AverageMs : 0.0;
		Row.CandidateMs = CandidatePass ? CandidatePass->AverageMs : 0.0;
		Row.DeltaMs = Row.CandidateMs - Row.BaselineMs;
		if (Row.bHasBaseline && Row.bHasCandidate && !FMath::IsNearlyZero(Row.BaselineMs))
		{
			Row.ChangePercent = Row.DeltaMs / Row.BaselineMs * 100.0;
		}
		PassRows.Add(MoveTemp(Row));
	}

	PassRows.Sort([](
		const FGraphicsCVarPassComparison& A,
		const FGraphicsCVarPassComparison& B)
	{
		return FMath::Abs(A.DeltaMs) > FMath::Abs(B.DeltaMs);
	});
	Result.Append(MoveTemp(PassRows));
	return Result;
}
