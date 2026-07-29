#pragma once

#include "CoreMinimal.h"

class FGraphicsCVarProfiler;

struct FGraphicsCVarReportExportResult
{
	bool bSuccess = false;
	FString MarkdownPath;
	FString JsonPath;
	FString ErrorMessage;
};

class FGraphicsCVarReportExporter
{
public:
	static FGraphicsCVarReportExportResult ExportComparisonReport(
		const FGraphicsCVarProfiler& Profiler,
		double HighlightThresholdMs);
};
