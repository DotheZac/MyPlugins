#include "GraphicsCVarReportExporter.h"

#include "GraphicsCVarProfiler.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 ReportSchemaVersion = 1;

	FString EscapeMarkdownCell(FString Value)
	{
		Value.ReplaceInline(TEXT("|"), TEXT("\\|"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value;
	}

	FString FormatMilliseconds(const double Value)
	{
		return FString::Printf(TEXT("%.3f"), Value);
	}

	FString FormatPercentage(const TOptional<double>& Value)
	{
		return Value.IsSet()
			? FString::Printf(TEXT("%+.1f%%"), Value.GetValue())
			: TEXT("N/A");
	}

	FString GetPassPresence(
		const FString& PassId,
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate)
	{
		if (PassId == TEXT("__TotalGPU"))
		{
			return TEXT("Both");
		}

		const bool bInBaseline = Baseline.Passes.Contains(PassId);
		const bool bInCandidate = Candidate.Passes.Contains(PassId);
		if (bInBaseline && bInCandidate)
		{
			return TEXT("Both");
		}
		if (bInBaseline)
		{
			return TEXT("Baseline only");
		}
		if (bInCandidate)
		{
			return TEXT("Candidate only");
		}
		return TEXT("Neither");
	}

	TSharedRef<FJsonObject> MakeTimingJson(
		const double AverageMs,
		const double MinMs,
		const double MaxMs)
	{
		TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
		Timing->SetNumberField(TEXT("average_ms"), AverageMs);
		Timing->SetNumberField(TEXT("min_ms"), MinMs);
		Timing->SetNumberField(TEXT("max_ms"), MaxMs);
		return Timing;
	}

	TSharedRef<FJsonObject> MakeSnapshotJson(const FGraphicsCVarSnapshot& Snapshot)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("label"), Snapshot.Label);
		Json->SetStringField(TEXT("captured_at"), Snapshot.CapturedAt.ToIso8601());
		Json->SetStringField(TEXT("capture_mode"), Snapshot.CaptureMode);
		Json->SetNumberField(TEXT("sample_frames"), Snapshot.SampleFrames);
		Json->SetNumberField(TEXT("target_frames"), Snapshot.TargetFrames);
		Json->SetObjectField(
			TEXT("total_gpu_frame"),
			MakeTimingJson(
				Snapshot.AverageGPUFrameMs,
				Snapshot.MinGPUFrameMs,
				Snapshot.MaxGPUFrameMs));

		TArray<FString> PassIds;
		Snapshot.Passes.GetKeys(PassIds);
		PassIds.Sort();

		TArray<TSharedPtr<FJsonValue>> Passes;
		Passes.Reserve(PassIds.Num());
		for (const FString& PassId : PassIds)
		{
			const FGraphicsCVarPassSnapshot& Pass = Snapshot.Passes.FindChecked(PassId);
			TSharedRef<FJsonObject> PassJson = MakeShared<FJsonObject>();
			PassJson->SetStringField(TEXT("id"), Pass.Id);
			PassJson->SetStringField(TEXT("display_name"), Pass.DisplayName);
			PassJson->SetObjectField(
				TEXT("timing"),
				MakeTimingJson(Pass.AverageMs, Pass.MinMs, Pass.MaxMs));
			Passes.Add(MakeShared<FJsonValueObject>(PassJson));
		}
		Json->SetArrayField(TEXT("gpu_passes"), MoveTemp(Passes));

		TArray<FString> CVarNames;
		Snapshot.CVarValues.GetKeys(CVarNames);
		CVarNames.Sort();

		TSharedRef<FJsonObject> CVars = MakeShared<FJsonObject>();
		for (const FString& CVarName : CVarNames)
		{
			CVars->SetStringField(CVarName, Snapshot.CVarValues.FindChecked(CVarName));
		}
		Json->SetObjectField(TEXT("cvars"), CVars);
		return Json;
	}

	TArray<FString> BuildChangedCVarNames(
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate)
	{
		TSet<FString> Names;
		for (const TPair<FString, FString>& Pair : Baseline.CVarValues)
		{
			Names.Add(Pair.Key);
		}
		for (const TPair<FString, FString>& Pair : Candidate.CVarValues)
		{
			Names.Add(Pair.Key);
		}

		TArray<FString> ChangedNames;
		for (const FString& Name : Names)
		{
			const FString* BaselineValue = Baseline.CVarValues.Find(Name);
			const FString* CandidateValue = Candidate.CVarValues.Find(Name);
			if (!BaselineValue || !CandidateValue || *BaselineValue != *CandidateValue)
			{
				ChangedNames.Add(Name);
			}
		}
		ChangedNames.Sort();
		return ChangedNames;
	}

	TSharedRef<FJsonObject> MakeComparisonRowJson(
		const FGraphicsCVarPassComparison& Row,
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate,
		const double HighlightThresholdMs)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Row.Id);
		Json->SetStringField(TEXT("display_name"), Row.DisplayName);
		Json->SetStringField(
			TEXT("presence"),
			GetPassPresence(Row.Id, Baseline, Candidate));
		Json->SetObjectField(
			TEXT("baseline"),
			MakeTimingJson(Row.BaselineMs, Row.BaselineMinMs, Row.BaselineMaxMs));
		Json->SetObjectField(
			TEXT("candidate"),
			MakeTimingJson(Row.CandidateMs, Row.CandidateMinMs, Row.CandidateMaxMs));
		Json->SetNumberField(TEXT("delta_ms"), Row.DeltaMs);
		Json->SetBoolField(
			TEXT("exceeds_highlight_threshold"),
			FMath::Abs(Row.DeltaMs) >= HighlightThresholdMs);
		if (Row.ChangePercent.IsSet())
		{
			Json->SetNumberField(TEXT("change_percent"), Row.ChangePercent.GetValue());
		}
		else
		{
			Json->SetField(TEXT("change_percent"), MakeShared<FJsonValueNull>());
		}
		return Json;
	}

	TArray<FGraphicsCVarPassComparison> SortByDelta(
		const TArray<FGraphicsCVarPassComparison>& Rows,
		const bool bDescending)
	{
		TArray<FGraphicsCVarPassComparison> Sorted;
		for (const FGraphicsCVarPassComparison& Row : Rows)
		{
			if (Row.Id != TEXT("__TotalGPU") && !FMath::IsNearlyZero(Row.DeltaMs, 0.001))
			{
				Sorted.Add(Row);
			}
		}
		Sorted.Sort([bDescending](
			const FGraphicsCVarPassComparison& A,
			const FGraphicsCVarPassComparison& B)
		{
			return bDescending ? A.DeltaMs > B.DeltaMs : A.DeltaMs < B.DeltaMs;
		});
		return Sorted;
	}

	FString BuildMarkdownReport(
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate,
		const TArray<FGraphicsCVarPassComparison>& Rows,
		const TArray<FString>& ChangedCVarNames,
		const double HighlightThresholdMs)
	{
		FString Report;
		Report += TEXT("# Unreal Engine GPU Baseline/Candidate Report\n\n");
		Report += FString::Printf(
			TEXT("- Generated: `%s`\n- Project: `%s`\n- Engine: `%s`\n- Platform: `%hs`\n- Highlight threshold: `%.3f ms`\n\n"),
			*FDateTime::Now().ToIso8601(),
			FApp::GetProjectName(),
			*FEngineVersion::Current().ToString(),
			FPlatformProperties::PlatformName(),
			HighlightThresholdMs);

		Report += TEXT("## Instructions for AI Analysis\n\n");
		Report += TEXT("- Treat a positive `Delta` as a GPU regression and a negative `Delta` as an improvement.\n");
		Report += TEXT("- Prioritize `Total GPU Frame`, then the passes with the largest absolute average-time changes.\n");
		Report += TEXT("- Correlate changed CVars with affected GPU passes, but distinguish correlation from confirmed causation.\n");
		Report += TEXT("- A pass present in only one capture is represented as `0 ms` on the missing side; use the `Presence` column to identify it.\n");
		Report += TEXT("- Consider capture duration, min/max variance, intermittent passes, and editor measurement overhead before drawing conclusions.\n");
		Report += TEXT("- Respond with: executive summary, likely causes, confidence level, recommended verification steps, and optimization priorities.\n\n");

		Report += TEXT("## Capture Summary\n\n");
		Report += TEXT("| Capture | Time | Mode | Frames | Target | Avg ms | Min ms | Max ms | GPU Passes |\n");
		Report += TEXT("|---|---|---|---:|---:|---:|---:|---:|---:|\n");
		auto AppendCaptureSummary = [&Report](const FGraphicsCVarSnapshot& Snapshot)
		{
			Report += FString::Printf(
				TEXT("| %s | %s | %s | %d | %d | %.3f | %.3f | %.3f | %d |\n"),
				*EscapeMarkdownCell(Snapshot.Label),
				*EscapeMarkdownCell(Snapshot.CapturedAt.ToIso8601()),
				*EscapeMarkdownCell(Snapshot.CaptureMode),
				Snapshot.SampleFrames,
				Snapshot.TargetFrames,
				Snapshot.AverageGPUFrameMs,
				Snapshot.MinGPUFrameMs,
				Snapshot.MaxGPUFrameMs,
				Snapshot.Passes.Num());
		};
		AppendCaptureSummary(Baseline);
		AppendCaptureSummary(Candidate);

		Report += TEXT("\n## Changed CVars\n\n");
		if (ChangedCVarNames.IsEmpty())
		{
			Report += TEXT("No managed CVar differences were captured.\n");
		}
		else
		{
			Report += TEXT("| CVar | Baseline | Candidate |\n|---|---|---|\n");
			for (const FString& Name : ChangedCVarNames)
			{
				const FString BaselineValue = Baseline.CVarValues.FindRef(Name);
				const FString CandidateValue = Candidate.CVarValues.FindRef(Name);
				Report += FString::Printf(
					TEXT("| `%s` | `%s` | `%s` |\n"),
					*EscapeMarkdownCell(Name),
					*EscapeMarkdownCell(BaselineValue.IsEmpty() ? TEXT("<missing>") : BaselineValue),
					*EscapeMarkdownCell(CandidateValue.IsEmpty() ? TEXT("<missing>") : CandidateValue));
			}
		}

		const TArray<FGraphicsCVarPassComparison> Regressions = SortByDelta(Rows, true);
		const TArray<FGraphicsCVarPassComparison> Improvements = SortByDelta(Rows, false);
		auto AppendTopChanges = [&Report](
			const FString& Heading,
			const TArray<FGraphicsCVarPassComparison>& Changes,
			const bool bPositive)
		{
			Report += FString::Printf(TEXT("\n## %s\n\n"), *Heading);
			int32 Added = 0;
			for (const FGraphicsCVarPassComparison& Row : Changes)
			{
				if ((bPositive && Row.DeltaMs <= 0.0) || (!bPositive && Row.DeltaMs >= 0.0))
				{
					continue;
				}
				Report += FString::Printf(
					TEXT("- `%s`: %+.3f ms (%s)\n"),
					*EscapeMarkdownCell(Row.DisplayName),
					Row.DeltaMs,
					*FormatPercentage(Row.ChangePercent));
				if (++Added >= 10)
				{
					break;
				}
			}
			if (Added == 0)
			{
				Report += TEXT("None.\n");
			}
		};
		AppendTopChanges(TEXT("Top GPU Regressions"), Regressions, true);
		AppendTopChanges(TEXT("Top GPU Improvements"), Improvements, false);

		Report += TEXT("\n## Full GPU Pass Comparison\n\n");
		Report += TEXT("| GPU Pass | Presence | Baseline Avg | Baseline Min | Baseline Max | Candidate Avg | Candidate Min | Candidate Max | Delta | Change | Highlighted |\n");
		Report += TEXT("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|\n");
		for (const FGraphicsCVarPassComparison& Row : Rows)
		{
			Report += FString::Printf(
				TEXT("| %s | %s | %s | %s | %s | %s | %s | %s | %+.3f | %s | %s |\n"),
				*EscapeMarkdownCell(Row.DisplayName),
				*GetPassPresence(Row.Id, Baseline, Candidate),
				*FormatMilliseconds(Row.BaselineMs),
				*FormatMilliseconds(Row.BaselineMinMs),
				*FormatMilliseconds(Row.BaselineMaxMs),
				*FormatMilliseconds(Row.CandidateMs),
				*FormatMilliseconds(Row.CandidateMinMs),
				*FormatMilliseconds(Row.CandidateMaxMs),
				Row.DeltaMs,
				*FormatPercentage(Row.ChangePercent),
				FMath::Abs(Row.DeltaMs) >= HighlightThresholdMs ? TEXT("Yes") : TEXT("No"));
		}

		Report += TEXT("\n## Full Managed CVar Snapshots\n\n");
		Report += TEXT("| CVar | Baseline | Candidate |\n|---|---|---|\n");
		TSet<FString> AllCVarNameSet;
		for (const TPair<FString, FString>& Pair : Baseline.CVarValues)
		{
			AllCVarNameSet.Add(Pair.Key);
		}
		for (const TPair<FString, FString>& Pair : Candidate.CVarValues)
		{
			AllCVarNameSet.Add(Pair.Key);
		}
		TArray<FString> AllCVarNames = AllCVarNameSet.Array();
		AllCVarNames.Sort();
		for (const FString& Name : AllCVarNames)
		{
			const FString BaselineValue = Baseline.CVarValues.FindRef(Name);
			const FString CandidateValue = Candidate.CVarValues.FindRef(Name);
			Report += FString::Printf(
				TEXT("| `%s` | `%s` | `%s` |\n"),
				*EscapeMarkdownCell(Name),
				*EscapeMarkdownCell(BaselineValue.IsEmpty() ? TEXT("<missing>") : BaselineValue),
				*EscapeMarkdownCell(CandidateValue.IsEmpty() ? TEXT("<missing>") : CandidateValue));
		}
		return Report;
	}

	FString BuildJsonReport(
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate,
		const TArray<FGraphicsCVarPassComparison>& Rows,
		const TArray<FString>& ChangedCVarNames,
		const double HighlightThresholdMs)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), ReportSchemaVersion);
		Root->SetStringField(
			TEXT("report_type"),
			TEXT("unreal_engine_gpu_baseline_candidate_comparison"));
		Root->SetStringField(TEXT("generated_at"), FDateTime::Now().ToIso8601());
		Root->SetNumberField(TEXT("highlight_threshold_ms"), HighlightThresholdMs);

		TSharedRef<FJsonObject> Environment = MakeShared<FJsonObject>();
		Environment->SetStringField(TEXT("project"), FApp::GetProjectName());
		Environment->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Environment->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
		Root->SetObjectField(TEXT("environment"), Environment);

		TArray<TSharedPtr<FJsonValue>> Instructions;
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Positive delta_ms means regression; negative delta_ms means improvement.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Prioritize Total GPU Frame and the largest absolute pass deltas.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Correlate changed CVars with passes, but do not claim causation without verification.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("A pass missing from one capture uses 0 ms; inspect the presence field.")));
		Root->SetArrayField(TEXT("analysis_instructions"), MoveTemp(Instructions));

		Root->SetObjectField(TEXT("baseline"), MakeSnapshotJson(Baseline));
		Root->SetObjectField(TEXT("candidate"), MakeSnapshotJson(Candidate));

		TArray<TSharedPtr<FJsonValue>> ChangedCVars;
		for (const FString& Name : ChangedCVarNames)
		{
			TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
			Change->SetStringField(TEXT("name"), Name);
			Change->SetStringField(TEXT("baseline"), Baseline.CVarValues.FindRef(Name));
			Change->SetStringField(TEXT("candidate"), Candidate.CVarValues.FindRef(Name));
			ChangedCVars.Add(MakeShared<FJsonValueObject>(Change));
		}
		Root->SetArrayField(TEXT("changed_cvars"), MoveTemp(ChangedCVars));

		TArray<TSharedPtr<FJsonValue>> Comparisons;
		Comparisons.Reserve(Rows.Num());
		for (const FGraphicsCVarPassComparison& Row : Rows)
		{
			Comparisons.Add(MakeShared<FJsonValueObject>(
				MakeComparisonRowJson(
					Row,
					Baseline,
					Candidate,
					HighlightThresholdMs)));
		}
		Root->SetArrayField(TEXT("gpu_comparison"), MoveTemp(Comparisons));

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Root, Writer);
		return JsonText;
	}
}

FGraphicsCVarReportExportResult FGraphicsCVarReportExporter::ExportComparisonReport(
	const FGraphicsCVarProfiler& Profiler,
	const double HighlightThresholdMs)
{
	FGraphicsCVarReportExportResult Result;
	const FGraphicsCVarSnapshot& Baseline = Profiler.GetBaseline();
	const FGraphicsCVarSnapshot& Candidate = Profiler.GetCandidate();
	if (!Baseline.bIsValid || !Candidate.bIsValid)
	{
		Result.ErrorMessage = TEXT("Capture both Baseline and Candidate before exporting.");
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
		TEXT("Reports"));
	IFileManager::Get().MakeDirectory(*ReportDirectory, true);
	if (!IFileManager::Get().DirectoryExists(*ReportDirectory))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Failed to create report directory: %s"),
			*ReportDirectory);
		return Result;
	}

	const FDateTime Now = FDateTime::Now();
	const FString Timestamp = FString::Printf(
		TEXT("%s_%03d"),
		*Now.ToString(TEXT("%Y%m%d_%H%M%S")),
		Now.GetMillisecond());
	const FString BaseFileName = FString::Printf(
		TEXT("GPUProfile_%s"),
		*Timestamp);
	Result.MarkdownPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".md"));
	Result.JsonPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".json"));

	const TArray<FGraphicsCVarPassComparison> Rows = Profiler.BuildComparison();
	const TArray<FString> ChangedCVarNames = BuildChangedCVarNames(Baseline, Candidate);
	const FString Markdown = BuildMarkdownReport(
		Baseline,
		Candidate,
		Rows,
		ChangedCVarNames,
		HighlightThresholdMs);
	const FString Json = BuildJsonReport(
		Baseline,
		Candidate,
		Rows,
		ChangedCVarNames,
		HighlightThresholdMs);

	const bool bMarkdownSaved = FFileHelper::SaveStringToFile(
		Markdown,
		*Result.MarkdownPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const bool bJsonSaved = FFileHelper::SaveStringToFile(
		Json,
		*Result.JsonPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bMarkdownSaved || !bJsonSaved)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Failed to save %s%s."),
			bMarkdownSaved ? TEXT("") : TEXT("Markdown"),
			bJsonSaved ? TEXT("") : bMarkdownSaved ? TEXT("JSON") : TEXT(" and JSON"));
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}
