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
	constexpr int32 ReportSchemaVersion = 3;

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

	TSharedRef<FJsonObject> MakeSpikeEventJson(
		const FGraphicsCVarSpikeEvent& Event)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("event_index"), Event.EventIndex);
		Json->SetNumberField(TEXT("start_frame"), Event.StartFrame);
		Json->SetNumberField(TEXT("peak_frame"), Event.PeakFrame);
		Json->SetNumberField(TEXT("last_spike_frame"), Event.LastSpikeFrame);
		Json->SetNumberField(TEXT("window_start_frame"), Event.WindowStartFrame);
		Json->SetNumberField(TEXT("window_end_frame"), Event.WindowEndFrame);
		Json->SetBoolField(
			TEXT("has_aligned_pass_sample"),
			Event.bHasAlignedPassSample);
		if (Event.bHasAlignedPassSample)
		{
			Json->SetNumberField(TEXT("pass_sample_frame"), Event.PassSampleFrame);
			Json->SetNumberField(TEXT("pass_frame_offset"), Event.PassFrameOffset);
		}
		else
		{
			Json->SetField(TEXT("pass_sample_frame"), MakeShared<FJsonValueNull>());
			Json->SetField(TEXT("pass_frame_offset"), MakeShared<FJsonValueNull>());
		}
		Json->SetNumberField(TEXT("rolling_baseline_total_ms"), Event.BaselineTotalMs);
		Json->SetNumberField(TEXT("peak_total_ms"), Event.PeakTotalMs);
		Json->SetNumberField(TEXT("delta_total_ms"), Event.DeltaTotalMs);

		TArray<TSharedPtr<FJsonValue>> PassDeltas;
		PassDeltas.Reserve(Event.PassDeltas.Num());
		for (const FGraphicsCVarSpikePassDelta& Delta : Event.PassDeltas)
		{
			TSharedRef<FJsonObject> DeltaJson = MakeShared<FJsonObject>();
			DeltaJson->SetStringField(TEXT("id"), Delta.Id);
			DeltaJson->SetStringField(TEXT("display_name"), Delta.DisplayName);
			DeltaJson->SetNumberField(TEXT("rolling_average_ms"), Delta.BaselineMs);
			DeltaJson->SetNumberField(TEXT("peak_ms"), Delta.PeakMs);
			DeltaJson->SetNumberField(TEXT("delta_ms"), Delta.DeltaMs);
			if (Delta.ChangePercent.IsSet())
			{
				DeltaJson->SetNumberField(
					TEXT("change_percent"),
					Delta.ChangePercent.GetValue());
			}
			else
			{
				DeltaJson->SetField(
					TEXT("change_percent"),
					MakeShared<FJsonValueNull>());
			}
			PassDeltas.Add(MakeShared<FJsonValueObject>(DeltaJson));
		}
		Json->SetArrayField(TEXT("pass_deltas"), MoveTemp(PassDeltas));

		TArray<TSharedPtr<FJsonValue>> FrameSamples;
		FrameSamples.Reserve(Event.FrameSamples.Num());
		for (const FGraphicsCVarFramePassSample& Frame : Event.FrameSamples)
		{
			TSharedRef<FJsonObject> FrameJson = MakeShared<FJsonObject>();
			FrameJson->SetNumberField(TEXT("frame"), Frame.FrameIndex);
			FrameJson->SetNumberField(
				TEXT("total_gpu_ms"),
				Frame.TotalGPUFrameMs);
			FrameJson->SetBoolField(
				TEXT("pass_data_valid"),
				Frame.bPassDataValid);
			FrameSamples.Add(MakeShared<FJsonValueObject>(FrameJson));
		}
		Json->SetArrayField(TEXT("frame_samples"), MoveTemp(FrameSamples));
		return Json;
	}

	TSharedRef<FJsonObject> MakeSnapshotJson(const FGraphicsCVarSnapshot& Snapshot)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("capture_id"), Snapshot.CaptureId);
		Json->SetStringField(TEXT("label"), Snapshot.Label);
		Json->SetStringField(TEXT("captured_at"), Snapshot.CapturedAt.ToIso8601());
		Json->SetStringField(TEXT("capture_mode"), Snapshot.CaptureMode);
		Json->SetNumberField(TEXT("sample_frames"), Snapshot.SampleFrames);
		Json->SetNumberField(TEXT("target_frames"), Snapshot.TargetFrames);
		TSharedRef<FJsonObject> PassSampleQuality = MakeShared<FJsonObject>();
		PassSampleQuality->SetNumberField(
			TEXT("attempts"),
			Snapshot.PassSampleAttempts);
		PassSampleQuality->SetNumberField(
			TEXT("valid_samples"),
			Snapshot.ValidPassSamples);
		PassSampleQuality->SetNumberField(
			TEXT("stat_history_frames"),
			Snapshot.PassStatHistoryFrames);
		PassSampleQuality->SetNumberField(
			TEXT("valid_percent"),
			Snapshot.PassSampleAttempts > 0
				? static_cast<double>(Snapshot.ValidPassSamples) /
					static_cast<double>(Snapshot.PassSampleAttempts) * 100.0
				: 0.0);
		Json->SetObjectField(TEXT("pass_sample_quality"), PassSampleQuality);
		Json->SetObjectField(
			TEXT("total_gpu_frame"),
			MakeTimingJson(
				Snapshot.AverageGPUFrameMs,
				Snapshot.MinGPUFrameMs,
				Snapshot.MaxGPUFrameMs));

		TSharedRef<FJsonObject> SpikeSettings = MakeShared<FJsonObject>();
		SpikeSettings->SetBoolField(TEXT("enabled"), Snapshot.SpikeSettings.bEnabled);
		SpikeSettings->SetNumberField(
			TEXT("frame_budget_ms"),
			Snapshot.SpikeSettings.FrameBudgetMs);
		SpikeSettings->SetNumberField(
			TEXT("delta_threshold_ms"),
			Snapshot.SpikeSettings.DeltaThresholdMs);
		SpikeSettings->SetNumberField(
			TEXT("rolling_window_frames"),
			Snapshot.SpikeSettings.RollingWindowFrames);
		SpikeSettings->SetNumberField(
			TEXT("pre_frames"),
			Snapshot.SpikeSettings.PreFrames);
		SpikeSettings->SetNumberField(
			TEXT("post_frames"),
			Snapshot.SpikeSettings.PostFrames);
		Json->SetObjectField(TEXT("spike_tracking_settings"), SpikeSettings);

		TArray<TSharedPtr<FJsonValue>> SpikeEvents;
		SpikeEvents.Reserve(Snapshot.SpikeEvents.Num());
		for (const FGraphicsCVarSpikeEvent& Event : Snapshot.SpikeEvents)
		{
			SpikeEvents.Add(MakeShared<FJsonValueObject>(
				MakeSpikeEventJson(Event)));
		}
		Json->SetArrayField(TEXT("spike_events"), MoveTemp(SpikeEvents));

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

	TArray<FGraphicsCVarPassSnapshot> SortBaselinePassesByAverage(
		const FGraphicsCVarSnapshot& Baseline)
	{
		TArray<FGraphicsCVarPassSnapshot> Sorted;
		Baseline.Passes.GenerateValueArray(Sorted);
		Sorted.Sort([](
			const FGraphicsCVarPassSnapshot& A,
			const FGraphicsCVarPassSnapshot& B)
		{
			return A.AverageMs > B.AverageMs;
		});
		return Sorted;
	}

	double GetPercentOfTotal(
		const double PassAverageMs,
		const FGraphicsCVarSnapshot& Baseline)
	{
		return Baseline.AverageGPUFrameMs > UE_DOUBLE_SMALL_NUMBER
			? PassAverageMs / Baseline.AverageGPUFrameMs * 100.0
			: 0.0;
	}

	struct FSpikePassAggregate
	{
		FString Id;
		FString DisplayName;
		int32 EventCount = 0;
		double SumDeltaMs = 0.0;
		double MaxDeltaMs = 0.0;
	};

	TArray<FSpikePassAggregate> BuildSpikePassAggregates(
		const FGraphicsCVarSnapshot& Snapshot)
	{
		TMap<FString, FSpikePassAggregate> ByPass;
		for (const FGraphicsCVarSpikeEvent& Event : Snapshot.SpikeEvents)
		{
			if (!Event.bHasAlignedPassSample)
			{
				continue;
			}
			for (const FGraphicsCVarSpikePassDelta& Delta : Event.PassDeltas)
			{
				if (Delta.DeltaMs <= 0.001)
				{
					continue;
				}
				FSpikePassAggregate& Aggregate = ByPass.FindOrAdd(Delta.Id);
				Aggregate.Id = Delta.Id;
				Aggregate.DisplayName = Delta.DisplayName;
				++Aggregate.EventCount;
				Aggregate.SumDeltaMs += Delta.DeltaMs;
				Aggregate.MaxDeltaMs = FMath::Max(
					Aggregate.MaxDeltaMs,
					Delta.DeltaMs);
			}
		}

		TArray<FSpikePassAggregate> Result;
		ByPass.GenerateValueArray(Result);
		Result.Sort([](
			const FSpikePassAggregate& A,
			const FSpikePassAggregate& B)
		{
			return A.EventCount != B.EventCount
				? A.EventCount > B.EventCount
				: A.MaxDeltaMs > B.MaxDeltaMs;
		});
		return Result;
	}

	void AppendSpikeMarkdown(
		FString& Report,
		const FGraphicsCVarSnapshot& Snapshot,
		const FString& Heading)
	{
		Report += FString::Printf(TEXT("\n## %s Spike Events\n\n"), *Heading);
		Report += FString::Printf(
			TEXT("- Capture ID: `%s`\n- Tracking: `%s`\n"
				"- Pass sample validity: `%d / %d (%.1f%%)`\n"
				"- Pass stat history: `%d frames` per sample\n"
				"- Frame budget: `%.3f ms`\n"
				"- Delta threshold: `%.3f ms`\n- Rolling window: `%d frames`\n"
				"- Preserved window: `%d pre / %d post frames`\n\n"),
			*Snapshot.CaptureId,
			Snapshot.SpikeSettings.bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
			Snapshot.ValidPassSamples,
			Snapshot.PassSampleAttempts,
			Snapshot.PassSampleAttempts > 0
				? static_cast<double>(Snapshot.ValidPassSamples) /
					static_cast<double>(Snapshot.PassSampleAttempts) * 100.0
				: 0.0,
			Snapshot.PassStatHistoryFrames,
			Snapshot.SpikeSettings.FrameBudgetMs,
			Snapshot.SpikeSettings.DeltaThresholdMs,
			Snapshot.SpikeSettings.RollingWindowFrames,
			Snapshot.SpikeSettings.PreFrames,
			Snapshot.SpikeSettings.PostFrames);

		if (Snapshot.SpikeEvents.IsEmpty())
		{
			Report += TEXT("No spike events were detected.\n");
			return;
		}

		for (const FGraphicsCVarSpikeEvent& Event : Snapshot.SpikeEvents)
		{
			Report += FString::Printf(
				TEXT("\n### Spike #%d\n\n"
					"- Peak frame: `%d`\n- Spike range: `%d - %d`\n"
					"- Stored window: `%d - %d`\n"
					"- Rolling baseline: `%.3f ms`\n- Peak Total GPU: `%.3f ms`\n"
					"- Total delta: `%+.3f ms`\n"
					"- Pass alignment: `%s`\n\n"),
				Event.EventIndex,
				Event.PeakFrame,
				Event.StartFrame,
				Event.LastSpikeFrame,
				Event.WindowStartFrame,
				Event.WindowEndFrame,
				Event.BaselineTotalMs,
				Event.PeakTotalMs,
				Event.DeltaTotalMs,
				Event.bHasAlignedPassSample
					? *FString::Printf(
						TEXT("Frame %d (%+d from peak)"),
						Event.PassSampleFrame,
						Event.PassFrameOffset)
					: TEXT("Unavailable - no valid Pass sample within +/-5 frames"));
			if (!Event.bHasAlignedPassSample)
			{
				Report += TEXT(
					"Pass attribution for this event is unavailable and must not be inferred.\n\n");
				continue;
			}
			Report += TEXT("| Rank | GPU Pass | Rolling Avg | Peak | Delta | Change |\n");
			Report += TEXT("|---:|---|---:|---:|---:|---:|\n");
			const int32 PassCount = FMath::Min(15, Event.PassDeltas.Num());
			for (int32 Index = 0; Index < PassCount; ++Index)
			{
				const FGraphicsCVarSpikePassDelta& Delta = Event.PassDeltas[Index];
				Report += FString::Printf(
					TEXT("| %d | %s | %.3f | %.3f | %+.3f | %s |\n"),
					Index + 1,
					*EscapeMarkdownCell(Delta.DisplayName),
					Delta.BaselineMs,
					Delta.PeakMs,
					Delta.DeltaMs,
					*FormatPercentage(Delta.ChangePercent));
			}
		}

		const TArray<FSpikePassAggregate> Aggregates =
			BuildSpikePassAggregates(Snapshot);
		Report += TEXT("\n### Aggregate Positive Spike Contributors\n\n");
		Report += TEXT("| Rank | GPU Pass | Events | Average Increase | Maximum Increase |\n");
		Report += TEXT("|---:|---|---:|---:|---:|\n");
		const int32 AggregateCount = FMath::Min(20, Aggregates.Num());
		for (int32 Index = 0; Index < AggregateCount; ++Index)
		{
			const FSpikePassAggregate& Aggregate = Aggregates[Index];
			Report += FString::Printf(
				TEXT("| %d | %s | %d | %+.3f ms | %+.3f ms |\n"),
				Index + 1,
				*EscapeMarkdownCell(Aggregate.DisplayName),
				Aggregate.EventCount,
				Aggregate.SumDeltaMs /
					static_cast<double>(Aggregate.EventCount),
				Aggregate.MaxDeltaMs);
		}
		if (AggregateCount == 0)
		{
			Report += TEXT("| - | No aligned positive Pass deltas | 0 | 0 | 0 |\n");
		}
	}

	FString BuildSpikeLogMarkdown(
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate)
	{
		FString Report;
		Report += TEXT("# Unreal Engine GPU Spike Log\n\n");
		Report += FString::Printf(
			TEXT("- Generated: `%s`\n- Project: `%s`\n- Engine: `%s`\n"
				"- Platform: `%hs`\n\n"),
			*FDateTime::Now().ToIso8601(),
			FApp::GetProjectName(),
			*FEngineVersion::Current().ToString(),
			FPlatformProperties::PlatformName());
		Report += TEXT("## Instructions for AI Analysis\n\n");
		Report += TEXT("- Identify the passes that increased most at each spike peak.\n");
		Report += TEXT("- Use the preserved pre/post frame samples to distinguish one-frame spikes from sustained load.\n");
		Report += TEXT("- GPU passes can overlap or be nested; do not add pass timings together.\n");
		Report += TEXT("- Pass timings are stat-history averages aligned near the Total GPU peak, not exact single-frame timestamps.\n");
		Report += TEXT("- A pass missing from a rolling frame is treated as `0 ms` when its rolling average is calculated.\n");
		Report += TEXT("- Treat pass correlation as a clue, not proof of the responsible Actor or effect.\n\n");

		if (Baseline.bIsValid)
		{
			AppendSpikeMarkdown(Report, Baseline, TEXT("Baseline"));
		}
		if (Candidate.bIsValid)
		{
			AppendSpikeMarkdown(Report, Candidate, TEXT("Candidate"));
		}
		return Report;
	}

	TSharedRef<FJsonObject> MakeSpikeCaptureJson(
		const FGraphicsCVarSnapshot& Snapshot)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("capture_id"), Snapshot.CaptureId);
		Json->SetStringField(TEXT("label"), Snapshot.Label);
		Json->SetStringField(TEXT("captured_at"), Snapshot.CapturedAt.ToIso8601());
		Json->SetStringField(TEXT("capture_mode"), Snapshot.CaptureMode);
		Json->SetNumberField(TEXT("sample_frames"), Snapshot.SampleFrames);
		TSharedRef<FJsonObject> PassSampleQuality = MakeShared<FJsonObject>();
		PassSampleQuality->SetNumberField(
			TEXT("attempts"),
			Snapshot.PassSampleAttempts);
		PassSampleQuality->SetNumberField(
			TEXT("valid_samples"),
			Snapshot.ValidPassSamples);
		PassSampleQuality->SetNumberField(
			TEXT("stat_history_frames"),
			Snapshot.PassStatHistoryFrames);
		PassSampleQuality->SetNumberField(
			TEXT("valid_percent"),
			Snapshot.PassSampleAttempts > 0
				? static_cast<double>(Snapshot.ValidPassSamples) /
					static_cast<double>(Snapshot.PassSampleAttempts) * 100.0
				: 0.0);
		Json->SetObjectField(TEXT("pass_sample_quality"), PassSampleQuality);

		TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetBoolField(TEXT("enabled"), Snapshot.SpikeSettings.bEnabled);
		Settings->SetNumberField(
			TEXT("frame_budget_ms"),
			Snapshot.SpikeSettings.FrameBudgetMs);
		Settings->SetNumberField(
			TEXT("delta_threshold_ms"),
			Snapshot.SpikeSettings.DeltaThresholdMs);
		Settings->SetNumberField(
			TEXT("rolling_window_frames"),
			Snapshot.SpikeSettings.RollingWindowFrames);
		Settings->SetNumberField(TEXT("pre_frames"), Snapshot.SpikeSettings.PreFrames);
		Settings->SetNumberField(TEXT("post_frames"), Snapshot.SpikeSettings.PostFrames);
		Json->SetObjectField(TEXT("settings"), Settings);

		TArray<TSharedPtr<FJsonValue>> Events;
		Events.Reserve(Snapshot.SpikeEvents.Num());
		for (const FGraphicsCVarSpikeEvent& Event : Snapshot.SpikeEvents)
		{
			Events.Add(MakeShared<FJsonValueObject>(MakeSpikeEventJson(Event)));
		}
		Json->SetArrayField(TEXT("events"), MoveTemp(Events));

		const TArray<FSpikePassAggregate> Aggregates =
			BuildSpikePassAggregates(Snapshot);
		TArray<TSharedPtr<FJsonValue>> AggregateJsonValues;
		AggregateJsonValues.Reserve(Aggregates.Num());
		for (const FSpikePassAggregate& Aggregate : Aggregates)
		{
			TSharedRef<FJsonObject> AggregateJson = MakeShared<FJsonObject>();
			AggregateJson->SetStringField(TEXT("id"), Aggregate.Id);
			AggregateJson->SetStringField(
				TEXT("display_name"),
				Aggregate.DisplayName);
			AggregateJson->SetNumberField(
				TEXT("event_count"),
				Aggregate.EventCount);
			AggregateJson->SetNumberField(
				TEXT("average_delta_ms"),
				Aggregate.SumDeltaMs /
					static_cast<double>(Aggregate.EventCount));
			AggregateJson->SetNumberField(
				TEXT("max_delta_ms"),
				Aggregate.MaxDeltaMs);
			AggregateJsonValues.Add(
				MakeShared<FJsonValueObject>(AggregateJson));
		}
		Json->SetArrayField(
			TEXT("aggregate_positive_contributors"),
			MoveTemp(AggregateJsonValues));
		return Json;
	}

	FString BuildSpikeLogJson(
		const FGraphicsCVarSnapshot& Baseline,
		const FGraphicsCVarSnapshot& Candidate)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), ReportSchemaVersion);
		Root->SetStringField(TEXT("report_type"), TEXT("unreal_engine_gpu_spike_log"));
		Root->SetStringField(TEXT("generated_at"), FDateTime::Now().ToIso8601());

		TSharedRef<FJsonObject> Environment = MakeShared<FJsonObject>();
		Environment->SetStringField(TEXT("project"), FApp::GetProjectName());
		Environment->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Environment->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
		Root->SetObjectField(TEXT("environment"), Environment);

		TArray<TSharedPtr<FJsonValue>> Captures;
		if (Baseline.bIsValid)
		{
			Captures.Add(MakeShared<FJsonValueObject>(
				MakeSpikeCaptureJson(Baseline)));
		}
		if (Candidate.bIsValid)
		{
			Captures.Add(MakeShared<FJsonValueObject>(
				MakeSpikeCaptureJson(Candidate)));
		}
		Root->SetArrayField(TEXT("captures"), MoveTemp(Captures));

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Root, Writer);
		return JsonText;
	}

	FString BuildBaselineMarkdownReport(const FGraphicsCVarSnapshot& Baseline)
	{
		FString Report;
		Report += TEXT("# Unreal Engine GPU Baseline Analysis Report\n\n");
		Report += FString::Printf(
			TEXT("- Generated: `%s`\n- Project: `%s`\n- Engine: `%s`\n"
				"- Platform: `%hs`\n- Capture ID: `%s`\n"
				"- Pass sample validity: `%d / %d (%.1f%%)`\n"
				"- Pass stat history: `%d frames` per sample\n\n"),
			*FDateTime::Now().ToIso8601(),
			FApp::GetProjectName(),
			*FEngineVersion::Current().ToString(),
			FPlatformProperties::PlatformName(),
			*Baseline.CaptureId,
			Baseline.ValidPassSamples,
			Baseline.PassSampleAttempts,
			Baseline.PassSampleAttempts > 0
				? static_cast<double>(Baseline.ValidPassSamples) /
					static_cast<double>(Baseline.PassSampleAttempts) * 100.0
				: 0.0,
			Baseline.PassStatHistoryFrames);

		Report += TEXT("## Instructions for AI Analysis\n\n");
		Report += TEXT("- This report contains one Baseline capture and has no Candidate comparison.\n");
		Report += TEXT("- Identify the most expensive GPU passes by average time, then inspect passes with a large min/max range for spikes or instability.\n");
		Report += TEXT("- Use `Percent of Total GPU` only as a relative prioritization hint. GPU passes can overlap or be nested, so percentages must not be added together.\n");
		Report += TEXT("- Correlate active CVars with expensive passes, but distinguish correlation from confirmed causation.\n");
		Report += TEXT("- Consider capture duration, intermittent passes, editor measurement overhead, scene content, resolution, and platform before drawing conclusions.\n");
		Report += TEXT("- Respond with: executive summary, top optimization targets, likely causes, confidence level, recommended CVar or content experiments, and verification steps.\n\n");

		Report += TEXT("## Capture Summary\n\n");
		Report += TEXT("| Capture | Time | Mode | Frames | Target | Avg ms | Min ms | Max ms | GPU Passes |\n");
		Report += TEXT("|---|---|---|---:|---:|---:|---:|---:|---:|\n");
		Report += FString::Printf(
			TEXT("| %s | %s | %s | %d | %d | %.3f | %.3f | %.3f | %d |\n"),
			*EscapeMarkdownCell(Baseline.Label),
			*EscapeMarkdownCell(Baseline.CapturedAt.ToIso8601()),
			*EscapeMarkdownCell(Baseline.CaptureMode),
			Baseline.SampleFrames,
			Baseline.TargetFrames,
			Baseline.AverageGPUFrameMs,
			Baseline.MinGPUFrameMs,
			Baseline.MaxGPUFrameMs,
			Baseline.Passes.Num());

		const TArray<FGraphicsCVarPassSnapshot> SortedPasses =
			SortBaselinePassesByAverage(Baseline);
		Report += TEXT("\n## Top Optimization Starting Points\n\n");
		Report += TEXT("| Rank | GPU Pass | Avg ms | Min ms | Max ms | Range ms | Percent of Total GPU |\n");
		Report += TEXT("|---:|---|---:|---:|---:|---:|---:|\n");
		const int32 PriorityCount = FMath::Min(15, SortedPasses.Num());
		for (int32 Index = 0; Index < PriorityCount; ++Index)
		{
			const FGraphicsCVarPassSnapshot& Pass = SortedPasses[Index];
			Report += FString::Printf(
				TEXT("| %d | %s | %.3f | %.3f | %.3f | %.3f | %.1f%% |\n"),
				Index + 1,
				*EscapeMarkdownCell(Pass.DisplayName),
				Pass.AverageMs,
				Pass.MinMs,
				Pass.MaxMs,
				Pass.MaxMs - Pass.MinMs,
				GetPercentOfTotal(Pass.AverageMs, Baseline));
		}
		if (PriorityCount == 0)
		{
			Report += TEXT("| - | No GPU passes captured | 0 | 0 | 0 | 0 | 0% |\n");
		}

		Report += TEXT("\n## Full GPU Pass Timings\n\n");
		Report += TEXT("| GPU Pass | Avg ms | Min ms | Max ms | Range ms | Percent of Total GPU |\n");
		Report += TEXT("|---|---:|---:|---:|---:|---:|\n");
		for (const FGraphicsCVarPassSnapshot& Pass : SortedPasses)
		{
			Report += FString::Printf(
				TEXT("| %s | %.3f | %.3f | %.3f | %.3f | %.1f%% |\n"),
				*EscapeMarkdownCell(Pass.DisplayName),
				Pass.AverageMs,
				Pass.MinMs,
				Pass.MaxMs,
				Pass.MaxMs - Pass.MinMs,
				GetPercentOfTotal(Pass.AverageMs, Baseline));
		}

		AppendSpikeMarkdown(Report, Baseline, TEXT("Baseline"));

		Report += TEXT("\n## Managed CVar Snapshot\n\n");
		Report += TEXT("| CVar | Value |\n|---|---|\n");
		TArray<FString> CVarNames;
		Baseline.CVarValues.GetKeys(CVarNames);
		CVarNames.Sort();
		for (const FString& Name : CVarNames)
		{
			Report += FString::Printf(
				TEXT("| `%s` | `%s` |\n"),
				*EscapeMarkdownCell(Name),
				*EscapeMarkdownCell(Baseline.CVarValues.FindChecked(Name)));
		}
		return Report;
	}

	FString BuildBaselineJsonReport(const FGraphicsCVarSnapshot& Baseline)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), ReportSchemaVersion);
		Root->SetStringField(
			TEXT("report_type"),
			TEXT("unreal_engine_gpu_baseline_analysis"));
		Root->SetStringField(TEXT("generated_at"), FDateTime::Now().ToIso8601());

		TSharedRef<FJsonObject> Environment = MakeShared<FJsonObject>();
		Environment->SetStringField(TEXT("project"), FApp::GetProjectName());
		Environment->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Environment->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
		Root->SetObjectField(TEXT("environment"), Environment);

		TArray<TSharedPtr<FJsonValue>> Instructions;
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Analyze this Baseline without assuming a Candidate comparison exists.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Prioritize passes with high average_ms and inspect large timing_range_ms values for instability.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("GPU passes can overlap or be nested; do not add percent_of_total_gpu values together.")));
		Instructions.Add(MakeShared<FJsonValueString>(
			TEXT("Correlate active CVars with expensive passes, but do not claim causation without verification.")));
		Root->SetArrayField(TEXT("analysis_instructions"), MoveTemp(Instructions));
		Root->SetObjectField(TEXT("baseline"), MakeSnapshotJson(Baseline));

		const TArray<FGraphicsCVarPassSnapshot> SortedPasses =
			SortBaselinePassesByAverage(Baseline);
		TArray<TSharedPtr<FJsonValue>> OptimizationCandidates;
		const int32 PriorityCount = FMath::Min(15, SortedPasses.Num());
		OptimizationCandidates.Reserve(PriorityCount);
		for (int32 Index = 0; Index < PriorityCount; ++Index)
		{
			const FGraphicsCVarPassSnapshot& Pass = SortedPasses[Index];
			TSharedRef<FJsonObject> CandidateJson = MakeShared<FJsonObject>();
			CandidateJson->SetNumberField(TEXT("priority_rank"), Index + 1);
			CandidateJson->SetStringField(TEXT("id"), Pass.Id);
			CandidateJson->SetStringField(TEXT("display_name"), Pass.DisplayName);
			CandidateJson->SetObjectField(
				TEXT("timing"),
				MakeTimingJson(Pass.AverageMs, Pass.MinMs, Pass.MaxMs));
			CandidateJson->SetNumberField(
				TEXT("timing_range_ms"),
				Pass.MaxMs - Pass.MinMs);
			CandidateJson->SetNumberField(
				TEXT("percent_of_total_gpu"),
				GetPercentOfTotal(Pass.AverageMs, Baseline));
			OptimizationCandidates.Add(
				MakeShared<FJsonValueObject>(CandidateJson));
		}
		Root->SetArrayField(
			TEXT("optimization_candidates"),
			MoveTemp(OptimizationCandidates));

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Root, Writer);
		return JsonText;
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
			TEXT("- Generated: `%s`\n- Project: `%s`\n- Engine: `%s`\n"
				"- Platform: `%hs`\n- Baseline Capture ID: `%s`\n"
				"- Candidate Capture ID: `%s`\n"
				"- Baseline Pass validity: `%d / %d (%.1f%%)`\n"
				"- Candidate Pass validity: `%d / %d (%.1f%%)`\n"
				"- Pass stat history: `%d frames` per sample\n"
				"- Highlight threshold: `%.3f ms`\n\n"),
			*FDateTime::Now().ToIso8601(),
			FApp::GetProjectName(),
			*FEngineVersion::Current().ToString(),
			FPlatformProperties::PlatformName(),
			*Baseline.CaptureId,
			*Candidate.CaptureId,
			Baseline.ValidPassSamples,
			Baseline.PassSampleAttempts,
			Baseline.PassSampleAttempts > 0
				? static_cast<double>(Baseline.ValidPassSamples) /
					static_cast<double>(Baseline.PassSampleAttempts) * 100.0
				: 0.0,
			Candidate.ValidPassSamples,
			Candidate.PassSampleAttempts,
			Candidate.PassSampleAttempts > 0
				? static_cast<double>(Candidate.ValidPassSamples) /
					static_cast<double>(Candidate.PassSampleAttempts) * 100.0
				: 0.0,
			Baseline.PassStatHistoryFrames,
			HighlightThresholdMs);

		Report += TEXT("## Instructions for AI Analysis\n\n");
		Report += TEXT("- Treat a positive `Delta` as a GPU regression and a negative `Delta` as an improvement.\n");
		Report += TEXT("- Prioritize `Total GPU Frame`, then the passes with the largest absolute average-time changes.\n");
		Report += TEXT("- Correlate changed CVars with affected GPU passes, but distinguish correlation from confirmed causation.\n");
		Report += TEXT("- A pass present in only one capture is represented as `0 ms` on the missing side; use the `Presence` column to identify it.\n");
		Report += TEXT("- Pass timings are stat-history averages; use pass validity and spike alignment metadata when assigning confidence.\n");
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

		AppendSpikeMarkdown(Report, Baseline, TEXT("Baseline"));
		AppendSpikeMarkdown(Report, Candidate, TEXT("Candidate"));

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

FGraphicsCVarReportExportResult FGraphicsCVarReportExporter::ExportReport(
	const FGraphicsCVarProfiler& Profiler,
	const double HighlightThresholdMs)
{
	FGraphicsCVarReportExportResult Result;
	const FGraphicsCVarSnapshot& Baseline = Profiler.GetBaseline();
	const FGraphicsCVarSnapshot& Candidate = Profiler.GetCandidate();
	if (!Baseline.bIsValid)
	{
		Result.ErrorMessage = TEXT("Capture a Baseline before exporting.");
		return Result;
	}
	const bool bHasCandidate = Candidate.bIsValid;

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
		TEXT("StatGPU"));
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
		TEXT("%s_%s"),
		bHasCandidate ? TEXT("GPUProfile") : TEXT("GPUBaseline"),
		*Timestamp);
	Result.MarkdownPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".md"));
	Result.JsonPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".json"));

	FString Markdown;
	FString Json;
	if (bHasCandidate)
	{
		const TArray<FGraphicsCVarPassComparison> Rows = Profiler.BuildComparison();
		const TArray<FString> ChangedCVarNames =
			BuildChangedCVarNames(Baseline, Candidate);
		Markdown = BuildMarkdownReport(
			Baseline,
			Candidate,
			Rows,
			ChangedCVarNames,
			HighlightThresholdMs);
		Json = BuildJsonReport(
			Baseline,
			Candidate,
			Rows,
			ChangedCVarNames,
			HighlightThresholdMs);
	}
	else
	{
		Markdown = BuildBaselineMarkdownReport(Baseline);
		Json = BuildBaselineJsonReport(Baseline);
	}

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

FGraphicsCVarReportExportResult FGraphicsCVarReportExporter::ExportSpikeLog(
	const FGraphicsCVarProfiler& Profiler)
{
	FGraphicsCVarReportExportResult Result;
	const FGraphicsCVarSnapshot& Baseline = Profiler.GetBaseline();
	const FGraphicsCVarSnapshot& Candidate = Profiler.GetCandidate();
	const bool bHasBaselineSpikes =
		Baseline.bIsValid && !Baseline.SpikeEvents.IsEmpty();
	const bool bHasCandidateSpikes =
		Candidate.bIsValid && !Candidate.SpikeEvents.IsEmpty();
	if (!bHasBaselineSpikes && !bHasCandidateSpikes)
	{
		Result.ErrorMessage = TEXT("No spike events are available to export.");
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
		TEXT("SpikeLogs"));
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
		TEXT("GPUSpikeLog_%s"),
		*Timestamp);
	Result.MarkdownPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".md"));
	Result.JsonPath = FPaths::Combine(
		ReportDirectory,
		BaseFileName + TEXT(".json"));

	const FString Markdown = BuildSpikeLogMarkdown(Baseline, Candidate);
	const FString Json = BuildSpikeLogJson(Baseline, Candidate);
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
