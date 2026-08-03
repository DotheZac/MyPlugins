#include "GraphicsCVarProfileGPUReportExporter.h"

#include "GraphicsCVarProfileGPUCapture.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 ProfileGPUReportSchemaVersion = 1;
	constexpr int32 MarkdownTopRowCount = 50;

	struct FValueStats
	{
		double Median = 0.0;
		double Average = 0.0;
		double Min = 0.0;
		double Max = 0.0;
	};

	struct FAggregateRow
	{
		FString Key;
		FString Queue;
		FString PassName;
		FString PassPath;
		TArray<double> ExclusiveSamples;
		TArray<double> InclusiveSamples;
		int32 Seen = 0;
	};

	FString EscapeMarkdown(FString Value)
	{
		Value.ReplaceInline(TEXT("|"), TEXT("\\|"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT("<br>"));
		return Value;
	}

	FString QueueLabel(const EGraphicsCVarProfileGPUQueueType Type)
	{
		switch (Type)
		{
		case EGraphicsCVarProfileGPUQueueType::Graphics: return TEXT("Graphics");
		case EGraphicsCVarProfileGPUQueueType::Compute: return TEXT("Compute");
		case EGraphicsCVarProfileGPUQueueType::Copy: return TEXT("Copy");
		default: return TEXT("Unknown");
		}
	}

	FString QueueIdentity(
		const EGraphicsCVarProfileGPUQueueType Type,
		const int32 QueueIndex,
		const int32 GPUIndex)
	{
		return FString::Printf(
			TEXT("%s Queue %d / GPU %d"),
			*QueueLabel(Type),
			QueueIndex,
			GPUIndex);
	}

	FValueStats CalculateReportStats(const TArray<double>& Values)
	{
		FValueStats Result;
		if (Values.IsEmpty())
		{
			return Result;
		}
		TArray<double> Sorted = Values;
		Sorted.Sort();
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		Result.Average = Sum / Values.Num();
		Result.Min = Sorted[0];
		Result.Max = Sorted.Last();
		const int32 Middle = Sorted.Num() / 2;
		Result.Median = Sorted.Num() % 2 == 0
			? (Sorted[Middle - 1] + Sorted[Middle]) * 0.5
			: Sorted[Middle];
		return Result;
	}

	TMap<FString, FAggregateRow> AggregateCaptureSet(
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		TMap<FString, FAggregateRow> Rows;
		const int32 SampleCount = CaptureSet.Samples.Num();
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			for (const FGraphicsCVarProfileGPUQueueCapture& Queue :
				CaptureSet.Samples[SampleIndex].Queues)
			{
				const FString QueueName = QueueIdentity(
					Queue.Type, Queue.QueueIndex, Queue.GPUIndex);
				const FString QueueKey = FString::Printf(
					TEXT("queue|%d|%d|%d"),
					static_cast<int32>(Queue.Type), Queue.QueueIndex, Queue.GPUIndex);
				FAggregateRow& QueueRow = Rows.FindOrAdd(QueueKey);
				QueueRow.Key = QueueKey;
				QueueRow.Queue = QueueName;
				QueueRow.PassName = TEXT("<Queue Frame Time>");
				QueueRow.PassPath = TEXT("<queue>");
				if (QueueRow.ExclusiveSamples.IsEmpty())
				{
					QueueRow.ExclusiveSamples.SetNumZeroed(SampleCount);
					QueueRow.InclusiveSamples.SetNumZeroed(SampleCount);
				}
				QueueRow.ExclusiveSamples[SampleIndex] = Queue.FrameTimeMs;
				QueueRow.InclusiveSamples[SampleIndex] = Queue.FrameTimeMs;
				++QueueRow.Seen;

				for (const FGraphicsCVarProfileGPUPass& Pass : Queue.Passes)
				{
					if (Pass.Name == TEXT("<root>"))
					{
						continue;
					}
					const FString Key = QueueKey + TEXT("|") + Pass.Path;
					FAggregateRow& Row = Rows.FindOrAdd(Key);
					Row.Key = Key;
					Row.Queue = QueueName;
					Row.PassName = Pass.Name;
					Row.PassPath = Pass.Path;
					if (Row.ExclusiveSamples.IsEmpty())
					{
						Row.ExclusiveSamples.SetNumZeroed(SampleCount);
						Row.InclusiveSamples.SetNumZeroed(SampleCount);
					}
					Row.ExclusiveSamples[SampleIndex] += Pass.ExclusiveMs;
					Row.InclusiveSamples[SampleIndex] += Pass.InclusiveMs;
					++Row.Seen;
				}
			}
		}
		return Rows;
	}

	FString DeriveBlueprintAssetPath(const FString& ActorClassPath)
	{
		if (ActorClassPath.EndsWith(TEXT("_C")))
		{
			return ActorClassPath.LeftChop(2);
		}
		return FString();
	}

	const FGraphicsCVarProfileGPUContextObject* FindSourceAsset(
		const FGraphicsCVarProfileGPUContextObject& Actor,
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		const FString ExpectedPath = DeriveBlueprintAssetPath(Actor.ClassPath);
		if (!ExpectedPath.IsEmpty())
		{
			if (const FGraphicsCVarProfileGPUContextObject* BlueprintAsset =
				CaptureSet.ContextObjects.FindByPredicate(
					[&ExpectedPath](const FGraphicsCVarProfileGPUContextObject& Item)
					{
						return Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset &&
							Item.ObjectPath == ExpectedPath;
					}))
			{
				return BlueprintAsset;
			}
		}

		static const TArray<FString> ReferencePrefixes = {
			TEXT("Niagara System reference: "),
			TEXT("Static Mesh reference: "),
			TEXT("Skeletal Mesh reference: ")
		};
		for (const FString& Detail : Actor.AnalysisDetails)
		{
			for (const FString& Prefix : ReferencePrefixes)
			{
				if (Detail.StartsWith(Prefix))
				{
					const FString ReferencedPath = Detail.RightChop(Prefix.Len());
					if (const FGraphicsCVarProfileGPUContextObject* ReferencedAsset =
						CaptureSet.ContextObjects.FindByPredicate(
							[&ReferencedPath](const FGraphicsCVarProfileGPUContextObject& Item)
							{
								return Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset &&
									Item.ObjectPath == ReferencedPath;
							}))
					{
						return ReferencedAsset;
					}
				}
			}
		}
		return nullptr;
	}

	struct FContextQuantityChange
	{
		const FGraphicsCVarProfileGPUContextObject* Baseline = nullptr;
		const FGraphicsCVarProfileGPUContextObject* Candidate = nullptr;
	};

	struct FContextLinkedInstance
	{
		FString Capture;
		const FGraphicsCVarProfileGPUContextObject* Actor = nullptr;
		const FGraphicsCVarProfileGPUContextObject* SourceAsset = nullptr;
	};

	struct FContextChanges
	{
		bool bComparisonAvailable = false;
		TArray<const FGraphicsCVarProfileGPUContextObject*> Added;
		TArray<const FGraphicsCVarProfileGPUContextObject*> Removed;
		TArray<FContextQuantityChange> QuantityChanged;
		TArray<FContextQuantityChange> InternalAnalysisChanged;
		TArray<FContextLinkedInstance> LinkedInstances;
	};

	FString MakeContextObjectKey(const FGraphicsCVarProfileGPUContextObject& Item)
	{
		return FString::Printf(
			TEXT("%s|%s"),
			Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset
				? TEXT("asset") : TEXT("actor"),
			*Item.ObjectPath);
	}

	void BuildComparableContextMap(
		const FString& CaptureLabel,
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet,
		TMap<FString, const FGraphicsCVarProfileGPUContextObject*>& OutMap,
		TArray<FContextLinkedInstance>& OutLinkedInstances)
	{
		for (const FGraphicsCVarProfileGPUContextObject& Item : CaptureSet.ContextObjects)
		{
			if (Item.Type == EGraphicsCVarProfileGPUContextObjectType::Actor)
			{
				if (const FGraphicsCVarProfileGPUContextObject* SourceAsset =
					FindSourceAsset(Item, CaptureSet))
				{
					FContextLinkedInstance& Link = OutLinkedInstances.AddDefaulted_GetRef();
					Link.Capture = CaptureLabel;
					Link.Actor = &Item;
					Link.SourceAsset = SourceAsset;
					continue;
				}
			}
			OutMap.Add(MakeContextObjectKey(Item), &Item);
		}
	}

	FContextChanges BuildContextChanges(
		const FGraphicsCVarProfileGPUCaptureSet& Baseline,
		const FGraphicsCVarProfileGPUCaptureSet& Candidate)
	{
		FContextChanges Changes;
		Changes.bComparisonAvailable = Baseline.bIsValid && Candidate.bIsValid;
		if (!Changes.bComparisonAvailable)
		{
			return Changes;
		}

		TMap<FString, const FGraphicsCVarProfileGPUContextObject*> BaselineMap;
		TMap<FString, const FGraphicsCVarProfileGPUContextObject*> CandidateMap;
		BuildComparableContextMap(
			TEXT("Baseline"), Baseline, BaselineMap, Changes.LinkedInstances);
		BuildComparableContextMap(
			TEXT("Candidate"), Candidate, CandidateMap, Changes.LinkedInstances);

		for (const TPair<FString, const FGraphicsCVarProfileGPUContextObject*>& Pair :
			CandidateMap)
		{
			const FGraphicsCVarProfileGPUContextObject* const* BaselineItem =
				BaselineMap.Find(Pair.Key);
			if (!BaselineItem)
			{
				Changes.Added.Add(Pair.Value);
				continue;
			}
			if ((*BaselineItem)->Quantity != Pair.Value->Quantity)
			{
				FContextQuantityChange& Change =
					Changes.QuantityChanged.AddDefaulted_GetRef();
				Change.Baseline = *BaselineItem;
				Change.Candidate = Pair.Value;
			}
			if ((*BaselineItem)->AnalysisDetails != Pair.Value->AnalysisDetails)
			{
				FContextQuantityChange& Change =
					Changes.InternalAnalysisChanged.AddDefaulted_GetRef();
				Change.Baseline = *BaselineItem;
				Change.Candidate = Pair.Value;
			}
		}

		for (const TPair<FString, const FGraphicsCVarProfileGPUContextObject*>& Pair :
			BaselineMap)
		{
			if (!CandidateMap.Contains(Pair.Key))
			{
				Changes.Removed.Add(Pair.Value);
			}
		}
		return Changes;
	}

	FString ContextObjectTypeText(const FGraphicsCVarProfileGPUContextObject& Item)
	{
		return Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset
			? TEXT("Asset") : TEXT("Actor");
	}

	void AppendContextChangesMarkdown(
		FString& Report,
		const FContextChanges& Changes)
	{
		Report += TEXT("\n## Capture Context Changes\n\n");
		if (!Changes.bComparisonAvailable)
		{
			Report += TEXT("Candidate capture is unavailable, so context changes cannot be calculated.\n");
			return;
		}

		auto AppendObjectList = [&Report](
			const FString& Heading,
			const TArray<const FGraphicsCVarProfileGPUContextObject*>& Items)
		{
			Report += FString::Printf(TEXT("### %s\n\n"), *Heading);
			if (Items.IsEmpty())
			{
				Report += TEXT("- None\n\n");
				return;
			}
			for (const FGraphicsCVarProfileGPUContextObject* Item : Items)
			{
				Report += FString::Printf(
					TEXT("- [%s] %s x%d — `%s`\n"),
					*ContextObjectTypeText(*Item),
					*Item->DisplayName,
					Item->Quantity,
					*Item->ObjectPath);
			}
			Report += TEXT("\n");
		};

		AppendObjectList(TEXT("Added"), Changes.Added);
		AppendObjectList(TEXT("Removed"), Changes.Removed);
		Report += TEXT("### Quantity Changed\n\n");
		if (Changes.QuantityChanged.IsEmpty())
		{
			Report += TEXT("- None\n\n");
		}
		else
		{
			for (const FContextQuantityChange& Change : Changes.QuantityChanged)
			{
				Report += FString::Printf(
					TEXT("- [%s] %s: %d -> %d (%+d)\n"),
					*ContextObjectTypeText(*Change.Candidate),
					*Change.Candidate->DisplayName,
					Change.Baseline->Quantity,
					Change.Candidate->Quantity,
					Change.Candidate->Quantity - Change.Baseline->Quantity);
			}
			Report += TEXT("\n");
		}

		Report += TEXT("### Internal Analysis Changed\n\n");
		if (Changes.InternalAnalysisChanged.IsEmpty())
		{
			Report += TEXT("- None\n\n");
		}
		else
		{
			for (const FContextQuantityChange& Change : Changes.InternalAnalysisChanged)
			{
				Report += FString::Printf(
					TEXT("- [%s] %s — compare the full Baseline and Candidate internal analysis sections.\n"),
					*ContextObjectTypeText(*Change.Candidate),
					*Change.Candidate->DisplayName);
			}
			Report += TEXT("\n");
		}

		Report += TEXT("### Linked Instances Excluded from Subject Count\n\n");
		if (Changes.LinkedInstances.IsEmpty())
		{
			Report += TEXT("- None\n\n");
		}
		else
		{
			for (const FContextLinkedInstance& Link : Changes.LinkedInstances)
			{
				Report += FString::Printf(
					TEXT("- %s: Actor `%s` -> Source Asset `%s`; do not count as a separate added subject.\n"),
					*Link.Capture,
					*Link.Actor->ObjectPath,
					*Link.SourceAsset->ObjectPath);
			}
			Report += TEXT("\n");
		}
	}

	void AppendContextMarkdown(
		FString& Report,
		const FString& Heading,
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		Report += FString::Printf(TEXT("\n## %s Capture Context\n\n"), *Heading);
		Report += FString::Printf(
			TEXT("- Memo: %s\n- Samples: %d\n- Interval: %.3f seconds\n\n"),
			CaptureSet.Memo.IsEmpty() ? TEXT("None") : *CaptureSet.Memo,
			CaptureSet.Samples.Num(),
			CaptureSet.IntervalSeconds);
		if (CaptureSet.ContextObjects.IsEmpty())
		{
			Report += TEXT("No related assets or Actors were registered.\n");
			return;
		}

		for (const FGraphicsCVarProfileGPUContextObject& Item : CaptureSet.ContextObjects)
		{
			Report += FString::Printf(
				TEXT("### [%s] %s\n\n- Object Path: `%s`\n- Class Path: `%s`\n- Quantity: %d\n"),
				Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset
					? TEXT("Asset") : TEXT("Actor"),
				*Item.DisplayName,
				*Item.ObjectPath,
				*Item.ClassPath,
				Item.Quantity);
			if (Item.Type == EGraphicsCVarProfileGPUContextObjectType::Actor)
			{
				if (const FGraphicsCVarProfileGPUContextObject* SourceAsset =
					FindSourceAsset(Item, CaptureSet))
				{
					Report += FString::Printf(
						TEXT("- Source Asset: `%s`\n- Duplicate Subject: true; this Actor and Asset describe the same rendering subject and must not be counted as two additions.\n"),
						*SourceAsset->ObjectPath);
				}
			}
			Report += TEXT("- Internal Analysis:\n");
			if (Item.AnalysisDetails.IsEmpty())
			{
				Report += TEXT("  - No analysis details.\n");
			}
			else
			{
				for (const FString& Detail : Item.AnalysisDetails)
				{
					Report += FString::Printf(TEXT("  - %s\n"), *Detail);
				}
			}
			Report += TEXT("\n");
		}
	}

	TSharedRef<FJsonObject> MakeContextJson(
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("memo"), CaptureSet.Memo);
		Json->SetNumberField(TEXT("sample_count"), CaptureSet.Samples.Num());
		Json->SetNumberField(TEXT("interval_seconds"), CaptureSet.IntervalSeconds);
		TArray<TSharedPtr<FJsonValue>> Objects;
		for (const FGraphicsCVarProfileGPUContextObject& Item : CaptureSet.ContextObjects)
		{
			TSharedRef<FJsonObject> ObjectJson = MakeShared<FJsonObject>();
			ObjectJson->SetStringField(
				TEXT("type"),
				Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset
					? TEXT("asset") : TEXT("actor"));
			ObjectJson->SetStringField(TEXT("display_name"), Item.DisplayName);
			ObjectJson->SetStringField(TEXT("object_path"), Item.ObjectPath);
			ObjectJson->SetStringField(TEXT("class_path"), Item.ClassPath);
			ObjectJson->SetNumberField(TEXT("quantity"), Item.Quantity);
			TArray<TSharedPtr<FJsonValue>> Details;
			for (const FString& Detail : Item.AnalysisDetails)
			{
				Details.Add(MakeShared<FJsonValueString>(Detail));
			}
			ObjectJson->SetArrayField(TEXT("internal_analysis"), MoveTemp(Details));
			if (Item.Type == EGraphicsCVarProfileGPUContextObjectType::Actor)
			{
				if (const FGraphicsCVarProfileGPUContextObject* SourceAsset =
					FindSourceAsset(Item, CaptureSet))
				{
					ObjectJson->SetStringField(TEXT("source_asset_path"), SourceAsset->ObjectPath);
					ObjectJson->SetBoolField(TEXT("duplicate_rendering_subject"), true);
				}
			}
			Objects.Add(MakeShared<FJsonValueObject>(ObjectJson));
		}
		Json->SetArrayField(TEXT("related_objects"), MoveTemp(Objects));
		return Json;
	}

	TSharedRef<FJsonObject> MakeContextObjectReferenceJson(
		const FGraphicsCVarProfileGPUContextObject& Item)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(
			TEXT("type"),
			Item.Type == EGraphicsCVarProfileGPUContextObjectType::Asset
				? TEXT("asset") : TEXT("actor"));
		Json->SetStringField(TEXT("display_name"), Item.DisplayName);
		Json->SetStringField(TEXT("object_path"), Item.ObjectPath);
		Json->SetStringField(TEXT("class_path"), Item.ClassPath);
		Json->SetNumberField(TEXT("quantity"), Item.Quantity);
		return Json;
	}

	TSharedRef<FJsonObject> MakeContextChangesJson(const FContextChanges& Changes)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("comparison_available"), Changes.bComparisonAvailable);
		TArray<TSharedPtr<FJsonValue>> Added;
		for (const FGraphicsCVarProfileGPUContextObject* Item : Changes.Added)
		{
			Added.Add(MakeShared<FJsonValueObject>(MakeContextObjectReferenceJson(*Item)));
		}
		Json->SetArrayField(TEXT("added"), MoveTemp(Added));
		TArray<TSharedPtr<FJsonValue>> Removed;
		for (const FGraphicsCVarProfileGPUContextObject* Item : Changes.Removed)
		{
			Removed.Add(MakeShared<FJsonValueObject>(MakeContextObjectReferenceJson(*Item)));
		}
		Json->SetArrayField(TEXT("removed"), MoveTemp(Removed));

		TArray<TSharedPtr<FJsonValue>> QuantityChanged;
		for (const FContextQuantityChange& Change : Changes.QuantityChanged)
		{
			TSharedRef<FJsonObject> ChangeJson =
				MakeContextObjectReferenceJson(*Change.Candidate);
			ChangeJson->SetNumberField(TEXT("baseline_quantity"), Change.Baseline->Quantity);
			ChangeJson->SetNumberField(TEXT("candidate_quantity"), Change.Candidate->Quantity);
			ChangeJson->SetNumberField(
				TEXT("quantity_delta"),
				Change.Candidate->Quantity - Change.Baseline->Quantity);
			QuantityChanged.Add(MakeShared<FJsonValueObject>(ChangeJson));
		}
		Json->SetArrayField(TEXT("quantity_changed"), MoveTemp(QuantityChanged));

		TArray<TSharedPtr<FJsonValue>> InternalAnalysisChanged;
		for (const FContextQuantityChange& Change : Changes.InternalAnalysisChanged)
		{
			TSharedRef<FJsonObject> ChangeJson =
				MakeContextObjectReferenceJson(*Change.Candidate);
			ChangeJson->SetBoolField(TEXT("internal_analysis_changed"), true);
			InternalAnalysisChanged.Add(MakeShared<FJsonValueObject>(ChangeJson));
		}
		Json->SetArrayField(
			TEXT("internal_analysis_changed"),
			MoveTemp(InternalAnalysisChanged));

		TArray<TSharedPtr<FJsonValue>> LinkedInstances;
		for (const FContextLinkedInstance& Link : Changes.LinkedInstances)
		{
			TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
			LinkJson->SetStringField(TEXT("capture"), Link.Capture);
			LinkJson->SetStringField(TEXT("actor_name"), Link.Actor->DisplayName);
			LinkJson->SetStringField(TEXT("actor_path"), Link.Actor->ObjectPath);
			LinkJson->SetStringField(TEXT("source_asset_path"), Link.SourceAsset->ObjectPath);
			LinkJson->SetBoolField(TEXT("excluded_from_subject_count"), true);
			LinkedInstances.Add(MakeShared<FJsonValueObject>(LinkJson));
		}
		Json->SetArrayField(TEXT("linked_instances"), MoveTemp(LinkedInstances));
		return Json;
	}

	TArray<FString> BuildUnionKeys(
		const TMap<FString, FAggregateRow>& BaselineRows,
		const TMap<FString, FAggregateRow>& CandidateRows)
	{
		TSet<FString> Keys;
		for (const TPair<FString, FAggregateRow>& Pair : BaselineRows) Keys.Add(Pair.Key);
		for (const TPair<FString, FAggregateRow>& Pair : CandidateRows) Keys.Add(Pair.Key);
		TArray<FString> Result = Keys.Array();
		Result.Sort([&BaselineRows, &CandidateRows](const FString& A, const FString& B)
		{
			const FAggregateRow* ARow = CandidateRows.Find(A);
			if (!ARow) ARow = BaselineRows.Find(A);
			const FAggregateRow* BRow = CandidateRows.Find(B);
			if (!BRow) BRow = BaselineRows.Find(B);
			const FValueStats ABase = BaselineRows.Contains(A)
				? CalculateReportStats(BaselineRows[A].ExclusiveSamples) : FValueStats();
			const FValueStats ACandidate = CandidateRows.Contains(A)
				? CalculateReportStats(CandidateRows[A].ExclusiveSamples) : FValueStats();
			const FValueStats BBase = BaselineRows.Contains(B)
				? CalculateReportStats(BaselineRows[B].ExclusiveSamples) : FValueStats();
			const FValueStats BCandidate = CandidateRows.Contains(B)
				? CalculateReportStats(CandidateRows[B].ExclusiveSamples) : FValueStats();
			const double ADelta = FMath::Abs(ACandidate.Median - ABase.Median);
			const double BDelta = FMath::Abs(BCandidate.Median - BBase.Median);
			if (!FMath::IsNearlyEqual(ADelta, BDelta)) return ADelta > BDelta;
			return ARow && BRow ? ARow->Key < BRow->Key : A < B;
		});
		return Result;
	}

	FString BuildMarkdown(
		const FGraphicsCVarProfileGPUCaptureSet& Baseline,
		const FGraphicsCVarProfileGPUCaptureSet& Candidate)
	{
		const TMap<FString, FAggregateRow> BaselineRows = AggregateCaptureSet(Baseline);
		const TMap<FString, FAggregateRow> CandidateRows = AggregateCaptureSet(Candidate);
		const TArray<FString> Keys = BuildUnionKeys(BaselineRows, CandidateRows);
		FString Report;
		Report += TEXT("# Unreal Engine ProfileGPU AI Report\n\n");
		Report += FString::Printf(TEXT("Generated: %s\n\n"), *FDateTime::Now().ToIso8601());
		Report += FString::Printf(
			TEXT("Report Mode: %s\n\n"),
			Candidate.bIsValid ? TEXT("Baseline / Candidate Comparison") : TEXT("Baseline Only"));
		Report += TEXT("## Instructions for AI Analysis\n\n");
		Report += TEXT("- Compare GPU Pass changes with the registered asset and Actor internal analysis to narrow likely causes.\n");
		Report += TEXT("- Correlation is evidence for a candidate cause, not proof. GPU passes may overlap or be nested.\n");
		Report += TEXT("- Quantity is scene context and does not imply linear cost scaling.\n");
		Report += TEXT("- If an Actor and its source Blueprint Asset are both registered, treat them as two descriptions of one rendering subject; do not add their quantities or costs together.\n");
		Report += TEXT("- A pass absent from a sample is represented as 0 ms in aggregate statistics.\n");
		Report += TEXT("- Baseline and Candidate are only comparable when camera, scene state, timing, resolution, CVars, lighting, and animation are otherwise identical.\n");
		if (!Candidate.bIsValid)
		{
			Report += TEXT("- Candidate was not captured. Candidate and Delta columns are placeholders; do not interpret them as measured improvements. Use Baseline values to identify the largest current GPU costs.\n");
		}
		AppendContextChangesMarkdown(Report, BuildContextChanges(Baseline, Candidate));
		AppendContextMarkdown(Report, TEXT("Baseline"), Baseline);
		if (Candidate.bIsValid) AppendContextMarkdown(Report, TEXT("Candidate"), Candidate);
		const int32 MarkdownRowCount = FMath::Min(MarkdownTopRowCount, Keys.Num());
		Report += TEXT("\n## ProfileGPU Top Changes\n\n");
		Report += FString::Printf(
			TEXT("The Markdown report shows the top %d of %d rows ranked by absolute Median change. The JSON report retains every row and all sample arrays.\n\n"),
			MarkdownRowCount,
			Keys.Num());
		Report += TEXT("| Queue | Pass | B Median | C Median | Delta | B Avg | C Avg | B Range | C Range | Seen B/C |\n");
		Report += TEXT("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
		for (int32 Index = 0; Index < MarkdownRowCount; ++Index)
		{
			const FString& Key = Keys[Index];
			const FAggregateRow* B = BaselineRows.Find(Key);
			const FAggregateRow* C = CandidateRows.Find(Key);
			const FAggregateRow& Display = C ? *C : *B;
			const FValueStats BS = B ? CalculateReportStats(B->ExclusiveSamples) : FValueStats();
			const FValueStats CS = C ? CalculateReportStats(C->ExclusiveSamples) : FValueStats();
			Report += FString::Printf(
				TEXT("| %s | %s | %.3f | %.3f | %+.3f | %.3f | %.3f | %.3f-%.3f | %.3f-%.3f | %d/%d / %d/%d |\n"),
				*EscapeMarkdown(Display.Queue), *EscapeMarkdown(Display.PassName),
				BS.Median, CS.Median, CS.Median - BS.Median,
				BS.Average, CS.Average, BS.Min, BS.Max, CS.Min, CS.Max,
				B ? B->Seen : 0, Baseline.Samples.Num(),
				C ? C->Seen : 0, Candidate.Samples.Num());
		}
		return Report;
	}

	TArray<TSharedPtr<FJsonValue>> MakeSamplesJson(const TArray<double>& Samples)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const double Sample : Samples)
		{
			Result.Add(MakeShared<FJsonValueNumber>(Sample));
		}
		return Result;
	}

	FString BuildJson(
		const FGraphicsCVarProfileGPUCaptureSet& Baseline,
		const FGraphicsCVarProfileGPUCaptureSet& Candidate)
	{
		const TMap<FString, FAggregateRow> BaselineRows = AggregateCaptureSet(Baseline);
		const TMap<FString, FAggregateRow> CandidateRows = AggregateCaptureSet(Candidate);
		const TArray<FString> Keys = BuildUnionKeys(BaselineRows, CandidateRows);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), ProfileGPUReportSchemaVersion);
		Root->SetStringField(TEXT("report_type"), TEXT("unreal_profilegpu_ai_report"));
		Root->SetStringField(TEXT("generated_at"), FDateTime::Now().ToIso8601());
		Root->SetBoolField(TEXT("candidate_available"), Candidate.bIsValid);
		TArray<TSharedPtr<FJsonValue>> Instructions;
		Instructions.Add(MakeShared<FJsonValueString>(TEXT("Do not double-count an Actor and its linked source Blueprint Asset.")));
		Instructions.Add(MakeShared<FJsonValueString>(TEXT("Quantity does not imply linear GPU cost scaling.")));
		Instructions.Add(MakeShared<FJsonValueString>(TEXT("Treat pass and asset correlation as a candidate cause, not proof.")));
		Instructions.Add(MakeShared<FJsonValueString>(TEXT("Compare only captures whose remaining conditions are identical.")));
		if (!Candidate.bIsValid)
		{
			Instructions.Add(MakeShared<FJsonValueString>(TEXT("Candidate values are missing-data placeholders. Do not interpret their zero values or deltas as measured improvements; rank current Baseline costs instead.")));
		}
		Root->SetArrayField(TEXT("ai_instructions"), MoveTemp(Instructions));
		Root->SetObjectField(TEXT("baseline_context"), MakeContextJson(Baseline));
		if (Candidate.bIsValid)
		{
			Root->SetObjectField(TEXT("candidate_context"), MakeContextJson(Candidate));
		}
		Root->SetObjectField(
			TEXT("context_changes"),
			MakeContextChangesJson(BuildContextChanges(Baseline, Candidate)));
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FString& Key : Keys)
		{
			const FAggregateRow* B = BaselineRows.Find(Key);
			const FAggregateRow* C = CandidateRows.Find(Key);
			const FAggregateRow& Display = C ? *C : *B;
			const FValueStats BS = B ? CalculateReportStats(B->ExclusiveSamples) : FValueStats();
			const FValueStats CS = C ? CalculateReportStats(C->ExclusiveSamples) : FValueStats();
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("queue"), Display.Queue);
			Row->SetStringField(TEXT("pass_name"), Display.PassName);
			Row->SetStringField(TEXT("pass_path"), Display.PassPath);
			Row->SetNumberField(TEXT("baseline_median_ms"), BS.Median);
			Row->SetNumberField(TEXT("candidate_median_ms"), CS.Median);
			Row->SetNumberField(TEXT("median_delta_ms"), CS.Median - BS.Median);
			Row->SetNumberField(TEXT("baseline_average_ms"), BS.Average);
			Row->SetNumberField(TEXT("candidate_average_ms"), CS.Average);
			Row->SetNumberField(TEXT("baseline_min_ms"), BS.Min);
			Row->SetNumberField(TEXT("baseline_max_ms"), BS.Max);
			Row->SetNumberField(TEXT("candidate_min_ms"), CS.Min);
			Row->SetNumberField(TEXT("candidate_max_ms"), CS.Max);
			Row->SetNumberField(TEXT("baseline_seen"), B ? B->Seen : 0);
			Row->SetNumberField(TEXT("candidate_seen"), C ? C->Seen : 0);
			Row->SetArrayField(TEXT("baseline_exclusive_samples_ms"),
				B ? MakeSamplesJson(B->ExclusiveSamples) : TArray<TSharedPtr<FJsonValue>>());
			Row->SetArrayField(TEXT("candidate_exclusive_samples_ms"),
				C ? MakeSamplesJson(C->ExclusiveSamples) : TArray<TSharedPtr<FJsonValue>>());
			Row->SetArrayField(TEXT("baseline_inclusive_samples_ms"),
				B ? MakeSamplesJson(B->InclusiveSamples) : TArray<TSharedPtr<FJsonValue>>());
			Row->SetArrayField(TEXT("candidate_inclusive_samples_ms"),
				C ? MakeSamplesJson(C->InclusiveSamples) : TArray<TSharedPtr<FJsonValue>>());
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Root->SetArrayField(TEXT("profilegpu_rows"), MoveTemp(Rows));
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Root, Writer);
		return Output;
	}
}

FGraphicsCVarProfileGPUReportExportResult
FGraphicsCVarProfileGPUReportExporter::ExportReport(
	const FGraphicsCVarProfileGPUCaptureSet& Baseline,
	const FGraphicsCVarProfileGPUCaptureSet& Candidate)
{
	FGraphicsCVarProfileGPUReportExportResult Result;
	if (!Baseline.bIsValid)
	{
		Result.ErrorMessage = TEXT("ProfileGPU Baseline capture is required.");
		return Result;
	}
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("GraphicsCVarControl"));
	if (!Plugin.IsValid())
	{
		Result.ErrorMessage = TEXT("GraphicsCVarControl plugin directory was not found.");
		return Result;
	}
	const FString ReportDirectory = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Reports"),
		TEXT("ProfileGPU"));
	IFileManager::Get().MakeDirectory(*ReportDirectory, true);
	if (!IFileManager::Get().DirectoryExists(*ReportDirectory))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Could not create report directory: %s"), *ReportDirectory);
		return Result;
	}
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString BaseName = FString::Printf(TEXT("ProfileGPU_AIReport_%s"), *Timestamp);
	Result.MarkdownPath = FPaths::Combine(ReportDirectory, BaseName + TEXT(".md"));
	Result.JsonPath = FPaths::Combine(ReportDirectory, BaseName + TEXT(".json"));
	const bool bMarkdownSaved = FFileHelper::SaveStringToFile(
		BuildMarkdown(Baseline, Candidate), *Result.MarkdownPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const bool bJsonSaved = FFileHelper::SaveStringToFile(
		BuildJson(Baseline, Candidate), *Result.JsonPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bMarkdownSaved || !bJsonSaved)
	{
		Result.ErrorMessage = TEXT("Failed to save one or more ProfileGPU report files.");
		return Result;
	}
	Result.bSuccess = true;
	return Result;
}
