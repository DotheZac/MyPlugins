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
	FGraphicsCVarSpikeSettings DisabledSpikeSettings;
	DisabledSpikeSettings.bEnabled = false;
	return StartCaptureInternal(
		Target,
		CVarValues,
		SampleFrames,
		false,
		DisabledSpikeSettings);
}

bool FGraphicsCVarProfiler::StartContinuousCapture(
	const EGraphicsCVarCaptureTarget Target,
	const TMap<FString, FString>& CVarValues,
	const int32 TargetFrames,
	const FGraphicsCVarSpikeSettings& SpikeSettings)
{
	return StartCaptureInternal(
		Target,
		CVarValues,
		TargetFrames,
		true,
		SpikeSettings);
}

bool FGraphicsCVarProfiler::StartCaptureInternal(
	const EGraphicsCVarCaptureTarget Target,
	const TMap<FString, FString>& CVarValues,
	const int32 SampleFrames,
	const bool bContinuous,
	const FGraphicsCVarSpikeSettings& SpikeSettings)
{
	if (bIsCapturing)
	{
		return false;
	}

	ActiveTarget = Target;
	bIsContinuousCapture = bContinuous;
	RequestedSampleFrames = bContinuous
		? SampleFrames > 0
			? FMath::Clamp(SampleFrames, 10, 36000)
			: 0
		: FMath::Clamp(SampleFrames, 10, 600);
	FramesElapsed = 0;
	GPUFrameSamples.Reset(
		bContinuous && RequestedSampleFrames == 0
			? 600
			: RequestedSampleFrames);
	PassAccumulators.Reset();
	RollingFrameSamples.Reset();
	ActiveSpikeEventIndex = INDEX_NONE;
	PendingSnapshot = {};
	PendingSnapshot.Label = Target == EGraphicsCVarCaptureTarget::Baseline
		? TEXT("Baseline")
		: TEXT("Candidate");
	const FDateTime CaptureStartedAt = FDateTime::Now();
	PendingSnapshot.CaptureId = FString::Printf(
		TEXT("%s_%s_%03d"),
		*PendingSnapshot.Label,
		*CaptureStartedAt.ToString(TEXT("%Y%m%d_%H%M%S")),
		CaptureStartedAt.GetMillisecond());
	PendingSnapshot.CaptureMode = !bContinuous
		? TEXT("Fixed Frames")
		: RequestedSampleFrames > 0
			? TEXT("Auto Stop")
			: TEXT("Manual Stop");
	PendingSnapshot.CVarValues = CVarValues;
	PendingSnapshot.PassStatHistoryFrames = bContinuous
		? 20
		: FMath::Clamp(RequestedSampleFrames, 20, 120);
	PendingSnapshot.SpikeSettings = SpikeSettings;
	PendingSnapshot.SpikeSettings.FrameBudgetMs = FMath::Clamp(
		PendingSnapshot.SpikeSettings.FrameBudgetMs,
		0.1,
		1000.0);
	PendingSnapshot.SpikeSettings.DeltaThresholdMs = FMath::Clamp(
		PendingSnapshot.SpikeSettings.DeltaThresholdMs,
		0.01,
		1000.0);
	PendingSnapshot.SpikeSettings.RollingWindowFrames = FMath::Clamp(
		PendingSnapshot.SpikeSettings.RollingWindowFrames,
		30,
		600);
	PendingSnapshot.SpikeSettings.PreFrames = FMath::Clamp(
		PendingSnapshot.SpikeSettings.PreFrames,
		0,
		300);
	PendingSnapshot.SpikeSettings.PostFrames = FMath::Clamp(
		PendingSnapshot.SpikeSettings.PostFrames,
		0,
		600);
	PendingSnapshot.SpikeSettings.MaxEvents = FMath::Clamp(
		PendingSnapshot.SpikeSettings.MaxEvents,
		1,
		100);
	PendingSnapshot.SampleFrames = RequestedSampleFrames;
	PendingSnapshot.TargetFrames = RequestedSampleFrames;
	LastStatus = FString::Printf(
		TEXT("%s warm-up: 0 / %d"),
		*PendingSnapshot.Label,
		WarmupFrames);

	SetGPUStatEnabledForCapture(PendingSnapshot.PassStatHistoryFrames);
	bIsCapturing = true;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FGraphicsCVarProfiler::Tick));
	return true;
}

void FGraphicsCVarProfiler::StopCapture()
{
	if (!IsContinuousCapture())
	{
		return;
	}

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	FinishCapture();
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
	bIsContinuousCapture = false;
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
	if (bIsContinuousCapture)
	{
		TMap<FString, FGraphicsCVarPassSnapshot> CurrentPasses;
		ReadGPUPassSnapshot(CurrentPasses);
		++PendingSnapshot.PassSampleAttempts;
		const bool bPassSnapshotValid =
			IsGPUPassSnapshotValid(CurrentPasses);
		if (bPassSnapshotValid)
		{
			++PendingSnapshot.ValidPassSamples;
		}
		else
		{
			CurrentPasses.Reset();
		}
		AccumulateCurrentPassSamples(CurrentPasses);
		ProcessSpikeSample(GPUFrameMs, CurrentPasses);
		if (RequestedSampleFrames > 0)
		{
			LastStatus = FString::Printf(
				TEXT("%s recording: %d / %d frames"),
				*PendingSnapshot.Label,
				GPUFrameSamples.Num(),
				RequestedSampleFrames);
		}
		else
		{
			LastStatus = FString::Printf(
				TEXT("%s recording: %d frames (press Stop to finish)"),
				*PendingSnapshot.Label,
				GPUFrameSamples.Num());
		}
	}
	else
	{
		LastStatus = FString::Printf(
			TEXT("%s capture: %d / %d frames"),
			*PendingSnapshot.Label,
			GPUFrameSamples.Num(),
			RequestedSampleFrames);
	}

	const bool bReachedTarget =
		RequestedSampleFrames > 0 &&
		GPUFrameSamples.Num() >= RequestedSampleFrames;
	if (bReachedTarget)
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
		bIsContinuousCapture = false;
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
	PendingSnapshot.GPUFrameSamples = MoveTemp(GPUFrameSamples);
	PendingSnapshot.SampleFrames = PendingSnapshot.GPUFrameSamples.Num();
	PendingSnapshot.CapturedAt = FDateTime::Now();
	if (bIsContinuousCapture)
	{
		BuildAccumulatedPassSnapshots(PendingSnapshot.Passes);
	}
	else
	{
		ReadGPUPassSnapshot(PendingSnapshot.Passes);
	}
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
		TEXT("%s captured: %.3f ms, %d GPU passes, %.1f%% valid pass samples"),
		*Completed.Label,
		Completed.AverageGPUFrameMs,
		Completed.Passes.Num(),
		Completed.PassSampleAttempts > 0
			? static_cast<double>(Completed.ValidPassSamples) /
				static_cast<double>(Completed.PassSampleAttempts) * 100.0
			: 0.0);

	RestoreGPUStatState();
	bIsCapturing = false;
	bIsContinuousCapture = false;
	++ResultRevision;
}

void FGraphicsCVarProfiler::AccumulateCurrentPassSamples(
	const TMap<FString, FGraphicsCVarPassSnapshot>& CurrentPasses)
{
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : CurrentPasses)
	{
		const FGraphicsCVarPassSnapshot& Current = Pair.Value;
		FGraphicsCVarPassAccumulator& Accumulator = PassAccumulators.FindOrAdd(Pair.Key);
		Accumulator.Id = Current.Id;
		Accumulator.DisplayName = Current.DisplayName;
		Accumulator.SumMs += Current.AverageMs;
		Accumulator.MinMs = FMath::Min(Accumulator.MinMs, Current.AverageMs);
		Accumulator.MaxMs = FMath::Max(Accumulator.MaxMs, Current.AverageMs);
		++Accumulator.SampleCount;
	}
}

void FGraphicsCVarProfiler::ProcessSpikeSample(
	const double GPUFrameMs,
	const TMap<FString, FGraphicsCVarPassSnapshot>& CurrentPasses)
{
	const FGraphicsCVarSpikeSettings& Settings = PendingSnapshot.SpikeSettings;
	if (!Settings.bEnabled)
	{
		return;
	}

	FGraphicsCVarFramePassSample CurrentFrame;
	CurrentFrame.FrameIndex = GPUFrameSamples.Num() - 1;
	CurrentFrame.TotalGPUFrameMs = GPUFrameMs;
	CurrentFrame.bPassDataValid = !CurrentPasses.IsEmpty();
	CurrentFrame.Passes = CurrentPasses;

	const bool bHasRollingBaseline =
		RollingFrameSamples.Num() >= Settings.RollingWindowFrames;
	const double RollingMedianMs =
		bHasRollingBaseline ? CalculateRollingMedian() : 0.0;
	const bool bIsSpike =
		bHasRollingBaseline &&
		GPUFrameMs >= Settings.FrameBudgetMs &&
		GPUFrameMs >= RollingMedianMs + Settings.DeltaThresholdMs;

	if (CurrentFrame.bPassDataValid)
	{
		for (FGraphicsCVarSpikeEvent& Event : PendingSnapshot.SpikeEvents)
		{
			TryAlignSpikePassSample(Event, CurrentFrame);
		}
	}

	if (ActiveSpikeEventIndex != INDEX_NONE &&
		PendingSnapshot.SpikeEvents.IsValidIndex(ActiveSpikeEventIndex))
	{
		FGraphicsCVarSpikeEvent& ActiveEvent =
			PendingSnapshot.SpikeEvents[ActiveSpikeEventIndex];
		AppendTotalOnlyFrameSample(ActiveEvent, CurrentFrame);
		ActiveEvent.WindowEndFrame = CurrentFrame.FrameIndex;

		if (bIsSpike)
		{
			ActiveEvent.LastSpikeFrame = CurrentFrame.FrameIndex;
			if (GPUFrameMs > ActiveEvent.PeakTotalMs)
			{
				UpdateSpikePeak(ActiveEvent, CurrentFrame, RollingMedianMs);
			}
		}
		else if (
			CurrentFrame.FrameIndex - ActiveEvent.LastSpikeFrame >=
			Settings.PostFrames)
		{
			ActiveSpikeEventIndex = INDEX_NONE;
		}
	}
	else if (
		bIsSpike &&
		PendingSnapshot.SpikeEvents.Num() < Settings.MaxEvents)
	{
		FGraphicsCVarSpikeEvent NewEvent;
		NewEvent.EventIndex = PendingSnapshot.SpikeEvents.Num() + 1;
		NewEvent.StartFrame = CurrentFrame.FrameIndex;
		NewEvent.LastSpikeFrame = CurrentFrame.FrameIndex;
		NewEvent.WindowStartFrame = FMath::Max(
			0,
			CurrentFrame.FrameIndex - Settings.PreFrames);

		const int32 FirstPreFrame = FMath::Max(
			0,
			RollingFrameSamples.Num() - Settings.PreFrames);
		for (int32 Index = FirstPreFrame; Index < RollingFrameSamples.Num(); ++Index)
		{
			AppendTotalOnlyFrameSample(NewEvent, RollingFrameSamples[Index]);
		}
		AppendTotalOnlyFrameSample(NewEvent, CurrentFrame);
		NewEvent.WindowEndFrame = CurrentFrame.FrameIndex;
		UpdateSpikePeak(NewEvent, CurrentFrame, RollingMedianMs);

		ActiveSpikeEventIndex = PendingSnapshot.SpikeEvents.Add(MoveTemp(NewEvent));
	}

	RollingFrameSamples.Add(MoveTemp(CurrentFrame));
	const int32 HistoryLimit = FMath::Max(
		Settings.RollingWindowFrames,
		Settings.PreFrames);
	if (RollingFrameSamples.Num() > HistoryLimit)
	{
		RollingFrameSamples.RemoveAt(
			0,
			RollingFrameSamples.Num() - HistoryLimit,
			EAllowShrinking::No);
	}
}

void FGraphicsCVarProfiler::UpdateSpikePeak(
	FGraphicsCVarSpikeEvent& Event,
	const FGraphicsCVarFramePassSample& CurrentFrame,
	const double RollingMedianMs)
{
	Event.PeakFrame = CurrentFrame.FrameIndex;
	Event.PeakTotalMs = CurrentFrame.TotalGPUFrameMs;
	Event.BaselineTotalMs = RollingMedianMs;
	Event.DeltaTotalMs = Event.PeakTotalMs - Event.BaselineTotalMs;
	Event.bHasAlignedPassSample = false;
	Event.PassSampleFrame = INDEX_NONE;
	Event.PassFrameOffset = 0;
	Event.PassDeltas.Reset();

	TryAlignSpikePassSample(Event, CurrentFrame);
	for (int32 Index = RollingFrameSamples.Num() - 1;
		Index >= 0 && !Event.bHasAlignedPassSample;
		--Index)
	{
		TryAlignSpikePassSample(Event, RollingFrameSamples[Index]);
	}
}

void FGraphicsCVarProfiler::TryAlignSpikePassSample(
	FGraphicsCVarSpikeEvent& Event,
	const FGraphicsCVarFramePassSample& CandidateFrame)
{
	if (!CandidateFrame.bPassDataValid || Event.PeakFrame < 0)
	{
		return;
	}

	const int32 Offset = CandidateFrame.FrameIndex - Event.PeakFrame;
	if (FMath::Abs(Offset) > SpikePassAlignmentFrames)
	{
		return;
	}
	if (Event.bHasAlignedPassSample &&
		FMath::Abs(Event.PassFrameOffset) <= FMath::Abs(Offset))
	{
		return;
	}

	Event.bHasAlignedPassSample = true;
	Event.PassSampleFrame = CandidateFrame.FrameIndex;
	Event.PassFrameOffset = Offset;
	BuildSpikePassDeltas(Event, CandidateFrame);
}

void FGraphicsCVarProfiler::BuildSpikePassDeltas(
	FGraphicsCVarSpikeEvent& Event,
	const FGraphicsCVarFramePassSample& PassFrame)
{
	TMap<FString, FGraphicsCVarPassSnapshot> RollingAverages;
	int32 ValidRollingFrames = 0;
	BuildRollingPassAverages(RollingAverages, ValidRollingFrames);
	if (ValidRollingFrames <= 0)
	{
		Event.PassDeltas.Reset();
		return;
	}

	TSet<FString> PassIds;
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : RollingAverages)
	{
		PassIds.Add(Pair.Key);
	}
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : PassFrame.Passes)
	{
		PassIds.Add(Pair.Key);
	}

	Event.PassDeltas.Reset();
	Event.PassDeltas.Reserve(PassIds.Num());
	for (const FString& PassId : PassIds)
	{
		const FGraphicsCVarPassSnapshot* BaselinePass = RollingAverages.Find(PassId);
		const FGraphicsCVarPassSnapshot* PeakPass = PassFrame.Passes.Find(PassId);

		FGraphicsCVarSpikePassDelta Delta;
		Delta.Id = PassId;
		Delta.DisplayName = PeakPass
			? PeakPass->DisplayName
			: BaselinePass->DisplayName;
		Delta.BaselineMs = BaselinePass ? BaselinePass->AverageMs : 0.0;
		Delta.PeakMs = PeakPass ? PeakPass->AverageMs : 0.0;
		Delta.DeltaMs = Delta.PeakMs - Delta.BaselineMs;
		if (!FMath::IsNearlyZero(Delta.BaselineMs))
		{
			Delta.ChangePercent = Delta.DeltaMs / Delta.BaselineMs * 100.0;
		}
		Event.PassDeltas.Add(MoveTemp(Delta));
	}

	Event.PassDeltas.Sort([](
		const FGraphicsCVarSpikePassDelta& A,
		const FGraphicsCVarSpikePassDelta& B)
	{
		const int32 ARank =
			A.DeltaMs > 0.001 ? 0 : A.DeltaMs < -0.001 ? 2 : 1;
		const int32 BRank =
			B.DeltaMs > 0.001 ? 0 : B.DeltaMs < -0.001 ? 2 : 1;
		return ARank != BRank
			? ARank < BRank
			: FMath::Abs(A.DeltaMs) > FMath::Abs(B.DeltaMs);
	});
}

void FGraphicsCVarProfiler::AppendTotalOnlyFrameSample(
	FGraphicsCVarSpikeEvent& Event,
	const FGraphicsCVarFramePassSample& Frame)
{
	FGraphicsCVarFramePassSample TotalOnlyFrame;
	TotalOnlyFrame.FrameIndex = Frame.FrameIndex;
	TotalOnlyFrame.TotalGPUFrameMs = Frame.TotalGPUFrameMs;
	TotalOnlyFrame.bPassDataValid = Frame.bPassDataValid;
	Event.FrameSamples.Add(MoveTemp(TotalOnlyFrame));
}

double FGraphicsCVarProfiler::CalculateRollingMedian() const
{
	TArray<double> Values;
	Values.Reserve(RollingFrameSamples.Num());
	for (const FGraphicsCVarFramePassSample& Frame : RollingFrameSamples)
	{
		Values.Add(Frame.TotalGPUFrameMs);
	}
	Values.Sort();
	if (Values.IsEmpty())
	{
		return 0.0;
	}

	const int32 Middle = Values.Num() / 2;
	return Values.Num() % 2 == 0
		? (Values[Middle - 1] + Values[Middle]) * 0.5
		: Values[Middle];
}

void FGraphicsCVarProfiler::BuildRollingPassAverages(
	TMap<FString, FGraphicsCVarPassSnapshot>& OutAverages,
	int32& OutValidFrameCount) const
{
	OutAverages.Reset();
	OutValidFrameCount = 0;
	if (RollingFrameSamples.IsEmpty())
	{
		return;
	}

	TMap<FString, double> Sums;
	TMap<FString, FString> DisplayNames;
	for (const FGraphicsCVarFramePassSample& Frame : RollingFrameSamples)
	{
		if (!Frame.bPassDataValid)
		{
			continue;
		}
		++OutValidFrameCount;
		for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : Frame.Passes)
		{
			Sums.FindOrAdd(Pair.Key) += Pair.Value.AverageMs;
			DisplayNames.FindOrAdd(Pair.Key) = Pair.Value.DisplayName;
		}
	}

	if (OutValidFrameCount <= 0)
	{
		return;
	}

	for (const TPair<FString, double>& Pair : Sums)
	{
		FGraphicsCVarPassSnapshot Average;
		Average.Id = Pair.Key;
		Average.DisplayName = DisplayNames.FindRef(Pair.Key);
		Average.AverageMs =
			Pair.Value / static_cast<double>(OutValidFrameCount);
		Average.MinMs = Average.AverageMs;
		Average.MaxMs = Average.AverageMs;
		OutAverages.Add(Pair.Key, MoveTemp(Average));
	}
}

void FGraphicsCVarProfiler::BuildAccumulatedPassSnapshots(
	TMap<FString, FGraphicsCVarPassSnapshot>& OutPasses) const
{
	OutPasses.Reset();
	for (const TPair<FString, FGraphicsCVarPassAccumulator>& Pair : PassAccumulators)
	{
		const FGraphicsCVarPassAccumulator& Accumulator = Pair.Value;
		if (Accumulator.SampleCount <= 0)
		{
			continue;
		}

		FGraphicsCVarPassSnapshot Pass;
		Pass.Id = Accumulator.Id;
		Pass.DisplayName = Accumulator.DisplayName;
		const int32 ValidFrameCount = FMath::Max(
			Accumulator.SampleCount,
			PendingSnapshot.ValidPassSamples);
		Pass.AverageMs =
			Accumulator.SumMs / static_cast<double>(ValidFrameCount);
		Pass.MinMs = Accumulator.SampleCount < ValidFrameCount
			? 0.0
			: Accumulator.MinMs;
		Pass.MaxMs = Accumulator.MaxMs;
		OutPasses.Add(Pair.Key, MoveTemp(Pass));
	}
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

bool FGraphicsCVarProfiler::IsGPUPassSnapshotValid(
	const TMap<FString, FGraphicsCVarPassSnapshot>& Passes)
{
	for (const TPair<FString, FGraphicsCVarPassSnapshot>& Pair : Passes)
	{
		if (Pair.Value.DisplayName.StartsWith(TEXT("Queue Total")))
		{
			return true;
		}
	}
	return false;
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
		Total.BaselineMinMs = Baseline.MinGPUFrameMs;
		Total.BaselineMaxMs = Baseline.MaxGPUFrameMs;
		Total.CandidateMs = Candidate.AverageGPUFrameMs;
		Total.CandidateMinMs = Candidate.MinGPUFrameMs;
		Total.CandidateMaxMs = Candidate.MaxGPUFrameMs;
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
		Row.bHasBaseline = Baseline.bIsValid;
		Row.bHasCandidate = Candidate.bIsValid;
		Row.BaselineMs = BaselinePass ? BaselinePass->AverageMs : 0.0;
		Row.BaselineMinMs = BaselinePass ? BaselinePass->MinMs : 0.0;
		Row.BaselineMaxMs = BaselinePass ? BaselinePass->MaxMs : 0.0;
		Row.CandidateMs = CandidatePass ? CandidatePass->AverageMs : 0.0;
		Row.CandidateMinMs = CandidatePass ? CandidatePass->MinMs : 0.0;
		Row.CandidateMaxMs = CandidatePass ? CandidatePass->MaxMs : 0.0;
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
