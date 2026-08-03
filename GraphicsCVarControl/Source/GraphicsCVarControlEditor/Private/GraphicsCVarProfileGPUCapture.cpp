#include "GraphicsCVarProfileGPUCapture.h"

#include "Editor.h"
#include "GPUProfiler.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/ScopeLock.h"

namespace
{
	const FName LogRHICategory(TEXT("LogRHI"));
	constexpr double CaptureTimeoutSeconds = 15.0;

	double ParseFloatingPointCell(FString Value)
	{
		Value.ReplaceInline(TEXT("ms"), TEXT(""));
		Value.ReplaceInline(TEXT("%"), TEXT(""));
		Value.TrimStartAndEndInline();
		return FCString::Atod(*Value);
	}

	int64 ParseIntegerCell(FString Value)
	{
		Value.TrimStartAndEndInline();
		return FCString::Atoi64(*Value);
	}

	FString MakeStablePassPathSegment(const FString& PassName)
	{
		static const FRegexPattern FramePattern(TEXT("^Frame [0-9]+$"));
		FRegexMatcher Matcher(FramePattern, PassName);
		return Matcher.FindNext() ? TEXT("Frame") : PassName;
	}
}

FGraphicsCVarProfileGPUCaptureService&
FGraphicsCVarProfileGPUCaptureService::Get()
{
	static FGraphicsCVarProfileGPUCaptureService Instance;
	return Instance;
}

bool FGraphicsCVarProfileGPUCaptureService::StartSingleCapture(
	const EGraphicsCVarProfileGPUCaptureTarget Target,
	const bool bShowVisualizer)

{
	return StartCapture(Target, 1, 0.0, bShowVisualizer, FString(), {});
}

bool FGraphicsCVarProfileGPUCaptureService::StartCapture(
	const EGraphicsCVarProfileGPUCaptureTarget Target,
	const int32 SampleCount,
	const double IntervalSeconds,
	const bool bShowVisualizer,
	const FString& Memo,
	const TArray<FGraphicsCVarProfileGPUContextObject>& ContextObjects)
{
	if (bIsCapturing || UE::RHI::GPUProfiler::IsProfiling())
	{
		Status = TEXT("Another ProfileGPU capture is already running");
		return false;
	}

	if (!GEditor || !GLog)
	{
		Status = TEXT("The editor or log output is not available");
		return false;
	}

	LastCapture = {};
	PendingTarget = Target;
	PendingSet = {};
	PendingSet.StartedAt = FDateTime::Now();
	PendingSet.Memo = Memo.TrimStartAndEnd();
	PendingSet.ContextObjects = ContextObjects;
	PendingSet.RequestedSamples = FMath::Clamp(SampleCount, 1, 20);
	PendingSet.IntervalSeconds = PendingSet.RequestedSamples > 1
		? FMath::Clamp(IntervalSeconds, 0.1, 10.0)
		: 0.0;
	PendingSet.Samples.Reserve(PendingSet.RequestedSamples);
	RestoreValues.Reset();
	CaptureElapsedSeconds = 0.0;
	IntervalRemainingSeconds = 0.0;
	bObservedProfilerActive = false;
	bWaitingForNextSample = false;
	bIsCapturing = true;

	ApplyCaptureCVars(bShowVisualizer && PendingSet.RequestedSamples == 1);
	GLog->AddOutputDevice(this);
	if (!StartNextSample())
	{
		FailCapture(TEXT("Failed to execute ProfileGPU"));
		return false;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FGraphicsCVarProfileGPUCaptureService::Tick));
	return true;
}

void FGraphicsCVarProfileGPUCaptureService::CancelCapture()
{
	RemoveTicker();
	bCollectLogLines.Store(false);
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	RestoreCaptureCVars();
	bIsCapturing = false;
	Status = TEXT("ProfileGPU capture cancelled");
}

void FGraphicsCVarProfileGPUCaptureService::ClearCaptures()
{
	if (bIsCapturing)
	{
		return;
	}

	LastCapture = {};
	Baseline = {};
	Candidate = {};
	Status = TEXT("Baseline and Candidate cleared");
	++ResultRevision;
}

void FGraphicsCVarProfileGPUCaptureService::Shutdown()
{
	if (bIsCapturing)
	{
		CancelCapture();
	}
	else
	{
		RemoveTicker();
		RestoreCaptureCVars();
	}
}

void FGraphicsCVarProfileGPUCaptureService::Serialize(
	const TCHAR* Message,
	const ELogVerbosity::Type Verbosity,
	const FName& Category)
{
	(void)Verbosity;
	if (!bCollectLogLines.Load() || Category != LogRHICategory || !Message)
	{
		return;
	}

	FScopeLock Lock(&CapturedLinesMutex);
	CapturedLines.Emplace(Message);
}

bool FGraphicsCVarProfileGPUCaptureService::Tick(const float DeltaTime)
{
	if (bWaitingForNextSample)
	{
		IntervalRemainingSeconds -= DeltaTime;
		Status = FString::Printf(
			TEXT("%s %d / %d captured. Next capture in %.1f sec"),
			PendingTarget == EGraphicsCVarProfileGPUCaptureTarget::Baseline
				? TEXT("Baseline")
				: TEXT("Candidate"),
			PendingSet.Samples.Num(),
			PendingSet.RequestedSamples,
			FMath::Max(0.0, IntervalRemainingSeconds));
		if (IntervalRemainingSeconds <= 0.0)
		{
			bWaitingForNextSample = false;
			if (!StartNextSample())
			{
				TickerHandle.Reset();
				FailCapture(TEXT("Failed to execute the next ProfileGPU sample"));
				return false;
			}
		}
		return true;
	}

	CaptureElapsedSeconds += DeltaTime;
	const bool bProfilerActive = UE::RHI::GPUProfiler::IsProfiling();
	bObservedProfilerActive |= bProfilerActive;

	if (bObservedProfilerActive && !bProfilerActive)
	{
		if (!CompleteCurrentSample())
		{
			TickerHandle.Reset();
			return false;
		}

		if (PendingSet.Samples.Num() >= PendingSet.RequestedSamples)
		{
			TickerHandle.Reset();
			FinishCaptureSet();
			return false;
		}

		bWaitingForNextSample = true;
		IntervalRemainingSeconds = PendingSet.IntervalSeconds;
		return true;
	}

	if (CaptureElapsedSeconds >= CaptureTimeoutSeconds)
	{
		TickerHandle.Reset();
		FailCapture(TEXT("ProfileGPU capture timed out"));
		return false;
	}

	return true;
}

bool FGraphicsCVarProfileGPUCaptureService::StartNextSample()
{
	{
		FScopeLock Lock(&CapturedLinesMutex);
		CapturedLines.Reset();
	}

	CaptureElapsedSeconds = 0.0;
	bObservedProfilerActive = false;
	bCollectLogLines.Store(true);
	Status = FString::Printf(
		TEXT("Capturing %s ProfileGPU sample %d / %d..."),
		PendingTarget == EGraphicsCVarProfileGPUCaptureTarget::Baseline
			? TEXT("Baseline")
			: TEXT("Candidate"),
		PendingSet.Samples.Num() + 1,
		PendingSet.RequestedSamples);

	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (!World && GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!GEditor || !GEditor->Exec(World, TEXT("ProfileGPU")))
	{
		bCollectLogLines.Store(false);
		return false;
	}

	bObservedProfilerActive = UE::RHI::GPUProfiler::IsProfiling();
	return true;
}

bool FGraphicsCVarProfileGPUCaptureService::CompleteCurrentSample()
{
	bCollectLogLines.Store(false);

	TArray<FString> Lines;
	{
		FScopeLock Lock(&CapturedLinesMutex);
		Lines = MoveTemp(CapturedLines);
	}

	FString ParseError;
	FGraphicsCVarProfileGPUCapture ParsedCapture;
	if (ParseCapturedLines(Lines, ParsedCapture, ParseError))
	{
		LastCapture = ParsedCapture;
		PendingSet.Samples.Add(MoveTemp(ParsedCapture));
		return true;
	}

	FailCapture(FString::Printf(TEXT("ProfileGPU parse failed: %s"), *ParseError));
	return false;
}

void FGraphicsCVarProfileGPUCaptureService::FinishCaptureSet()
{
	bCollectLogLines.Store(false);
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	RestoreCaptureCVars();

	PendingSet.bIsValid = !PendingSet.Samples.IsEmpty();
	LastTarget = PendingTarget;
	if (PendingTarget == EGraphicsCVarProfileGPUCaptureTarget::Baseline)
	{
		Baseline = PendingSet;
	}
	else
	{
		Candidate = PendingSet;
	}
	Status = FString::Printf(
		TEXT("Captured %s ProfileGPU set (%d samples)"),
		PendingTarget == EGraphicsCVarProfileGPUCaptureTarget::Baseline
			? TEXT("Baseline")
			: TEXT("Candidate"),
		PendingSet.Samples.Num());

	bIsCapturing = false;
	++ResultRevision;
}

void FGraphicsCVarProfileGPUCaptureService::FailCapture(const FString& Error)
{
	bCollectLogLines.Store(false);
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	RestoreCaptureCVars();
	bIsCapturing = false;
	bWaitingForNextSample = false;
	Status = Error;
	++ResultRevision;
}

void FGraphicsCVarProfileGPUCaptureService::RemoveTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void FGraphicsCVarProfileGPUCaptureService::ApplyCaptureCVars(
	const bool bShowVisualizer)
{
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowUI"), bShowVisualizer ? TEXT("1") : TEXT("0"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.UnicodeOutput"), TEXT("0"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowHeader"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowEmptyQueues"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowStats"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowPercentColumn"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowInclusive"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowExclusive"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ShowLeafEvents"), TEXT("1"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.ThresholdPercent"), TEXT("0"));
	SetTemporaryCVar(TEXT("r.ProfileGPU.Root"), TEXT("*"));
}

void FGraphicsCVarProfileGPUCaptureService::RestoreCaptureCVars()
{
	for (const FCVarRestoreValue& RestoreValue : RestoreValues)
	{
		IConsoleVariable* Variable =
			IConsoleManager::Get().FindConsoleVariable(*RestoreValue.Name);
		if (!Variable)
		{
			continue;
		}

		IConsoleVariable::FSetContext Context;
		Context.Flags = static_cast<EConsoleVariableFlags>(RestoreValue.SetByFlags);
		Context.PriorityMode = IConsoleVariable::FSetContext::EPriorityMode::ReplaceCurrent;
		Variable->Set(*RestoreValue.Value, Variable->ResolveContext(Context));
	}
	RestoreValues.Reset();
}

void FGraphicsCVarProfileGPUCaptureService::SetTemporaryCVar(
	const TCHAR* Name,
	const TCHAR* Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!Variable)
	{
		return;
	}

	FCVarRestoreValue& RestoreValue = RestoreValues.Emplace_GetRef();
	RestoreValue.Name = Name;
	RestoreValue.Value = Variable->GetString();
	RestoreValue.SetByFlags = Variable->GetFlags() & ECVF_SetByMask;

	IConsoleVariable::FSetContext Context;
	Context.Flags = static_cast<EConsoleVariableFlags>(RestoreValue.SetByFlags);
	Context.PriorityMode = IConsoleVariable::FSetContext::EPriorityMode::ReplaceCurrent;
	Variable->Set(Value, Variable->ResolveContext(Context));
}

bool FGraphicsCVarProfileGPUCaptureService::ParseCapturedLines(
	const TArray<FString>& Lines,
	FGraphicsCVarProfileGPUCapture& OutCapture,
	FString& OutError) const
{
	OutCapture = {};
	OutCapture.CapturedAt = FDateTime::Now();
	FGraphicsCVarProfileGPUQueueCapture* CurrentQueue = nullptr;
	TArray<FString> PassPathStack;

	for (const FString& RawLine : Lines)
	{
		FString Line = RawLine;
		Line.TrimStartAndEndInline();

		int32 FrameNumber = INDEX_NONE;
		int32 QueueIndex = 0;
		int32 GPUIndex = 0;
		EGraphicsCVarProfileGPUQueueType QueueType =
			EGraphicsCVarProfileGPUQueueType::Unknown;
		if (ParseProfileHeader(
			Line,
			FrameNumber,
			QueueType,
			QueueIndex,
			GPUIndex))
		{
			if (OutCapture.FrameNumber == INDEX_NONE)
			{
				OutCapture.FrameNumber = FrameNumber;
			}
			else if (OutCapture.FrameNumber != FrameNumber)
			{
				OutError = TEXT("multiple ProfileGPU frames were mixed in one capture");
				return false;
			}

			CurrentQueue = &OutCapture.Queues.Emplace_GetRef();
			CurrentQueue->Type = QueueType;
			CurrentQueue->QueueIndex = QueueIndex;
			CurrentQueue->GPUIndex = GPUIndex;
			PassPathStack.Reset();
			continue;
		}

		if (!CurrentQueue)
		{
			continue;
		}

		double FrameTimeMs = 0.0;
		if (ParseFrameTime(Line, FrameTimeMs))
		{
			CurrentQueue->FrameTimeMs = FrameTimeMs;
			continue;
		}

		FGraphicsCVarProfileGPUPass Pass;
		if (ParsePassRow(Line, Pass))
		{
			while (PassPathStack.Num() > Pass.Depth)
			{
				PassPathStack.Pop();
			}

			const FString ParentPath = Pass.Depth > 0 &&
				PassPathStack.IsValidIndex(Pass.Depth - 1)
				? PassPathStack[Pass.Depth - 1]
				: FString();
			const FString StablePathSegment = MakeStablePassPathSegment(Pass.Name);
			const FString BasePath = ParentPath.IsEmpty()
				? StablePathSegment
				: ParentPath + TEXT("/") + StablePathSegment;
			Pass.Path = BasePath;
			int32 DuplicateIndex = 1;
			while (CurrentQueue->Passes.ContainsByPredicate(
				[&Pass](const FGraphicsCVarProfileGPUPass& Existing)
				{
					return Existing.Path == Pass.Path;
				}))
			{
				++DuplicateIndex;
				Pass.Path = FString::Printf(TEXT("%s#%d"), *BasePath, DuplicateIndex);
			}

			if (PassPathStack.Num() == Pass.Depth)
			{
				PassPathStack.Add(Pass.Path);
			}
			else if (PassPathStack.IsValidIndex(Pass.Depth))
			{
				PassPathStack[Pass.Depth] = Pass.Path;
			}
			CurrentQueue->Passes.Add(MoveTemp(Pass));
		}
	}

	const bool bHasGraphicsQueue = OutCapture.Queues.ContainsByPredicate(
		[](const FGraphicsCVarProfileGPUQueueCapture& Queue)
		{
			return Queue.Type == EGraphicsCVarProfileGPUQueueType::Graphics &&
				!Queue.Passes.IsEmpty();
		});

	if (OutCapture.FrameNumber == INDEX_NONE || !bHasGraphicsQueue)
	{
		OutError = TEXT("no complete Graphics queue table was found");
		return false;
	}

	OutCapture.bIsValid = true;
	return true;
}

bool FGraphicsCVarProfileGPUCaptureService::ParseProfileHeader(
	const FString& Line,
	int32& OutFrameNumber,
	EGraphicsCVarProfileGPUQueueType& OutQueueType,
	int32& OutQueueIndex,
	int32& OutGPUIndex)
{
	static const FRegexPattern Pattern(
		TEXT("^GPU Profile for Frame ([0-9]+) - (Copy|Compute|Graphics) ([0-9]+) - GPU ([0-9]+)$"));
	FRegexMatcher Matcher(Pattern, Line);
	if (!Matcher.FindNext())
	{
		return false;
	}

	OutFrameNumber = FCString::Atoi(*Matcher.GetCaptureGroup(1));
	const FString QueueName = Matcher.GetCaptureGroup(2);
	OutQueueType = QueueName == TEXT("Copy")
		? EGraphicsCVarProfileGPUQueueType::Copy
		: QueueName == TEXT("Compute")
			? EGraphicsCVarProfileGPUQueueType::Compute
			: EGraphicsCVarProfileGPUQueueType::Graphics;
	OutQueueIndex = FCString::Atoi(*Matcher.GetCaptureGroup(3));
	OutGPUIndex = FCString::Atoi(*Matcher.GetCaptureGroup(4));
	return true;
}

bool FGraphicsCVarProfileGPUCaptureService::ParseFrameTime(
	const FString& Line,
	double& OutFrameTimeMs)
{
	static const FRegexPattern Pattern(
		TEXT("^- Frame Time[ ]*: ([0-9]+(?:\\.[0-9]+)?)ms$"));
	FRegexMatcher Matcher(Pattern, Line);
	if (!Matcher.FindNext())
	{
		return false;
	}

	OutFrameTimeMs = FCString::Atod(*Matcher.GetCaptureGroup(1));
	return true;
}

bool FGraphicsCVarProfileGPUCaptureService::ParsePassRow(
	const FString& Line,
	FGraphicsCVarProfileGPUPass& OutPass)
{
	if (!Line.StartsWith(TEXT("|")) || !Line.EndsWith(TEXT("|")))
	{
		return false;
	}

	const FString RowContents = Line.Mid(1, Line.Len() - 2);
	TArray<FString> Cells;
	RowContents.ParseIntoArray(Cells, TEXT("|"), false);
	if (Cells.Num() != 13 || !Cells[5].Contains(TEXT("ms")) ||
		!Cells[11].Contains(TEXT("ms")))
	{
		return false;
	}

	FString EventCell = Cells[12];
	int32 LeadingSpaces = 0;
	while (LeadingSpaces < EventCell.Len() && EventCell[LeadingSpaces] == TEXT(' '))
	{
		++LeadingSpaces;
	}

	EventCell.TrimStartAndEndInline();
	if (EventCell.IsEmpty())
	{
		return false;
	}

	OutPass.Name = MoveTemp(EventCell);
	OutPass.Depth = FMath::Max(0, (LeadingSpaces - 1) / 3);
	OutPass.ExclusiveDraws = ParseIntegerCell(Cells[0]);
	OutPass.ExclusiveDispatches = ParseIntegerCell(Cells[1]);
	OutPass.ExclusivePrimitives = ParseIntegerCell(Cells[2]);
	OutPass.ExclusiveVertices = ParseIntegerCell(Cells[3]);
	OutPass.ExclusivePercent = ParseFloatingPointCell(Cells[4]);
	OutPass.ExclusiveMs = ParseFloatingPointCell(Cells[5]);
	OutPass.InclusiveDraws = ParseIntegerCell(Cells[6]);
	OutPass.InclusiveDispatches = ParseIntegerCell(Cells[7]);
	OutPass.InclusivePrimitives = ParseIntegerCell(Cells[8]);
	OutPass.InclusiveVertices = ParseIntegerCell(Cells[9]);
	OutPass.InclusivePercent = ParseFloatingPointCell(Cells[10]);
	OutPass.InclusiveMs = ParseFloatingPointCell(Cells[11]);
	return true;
}
