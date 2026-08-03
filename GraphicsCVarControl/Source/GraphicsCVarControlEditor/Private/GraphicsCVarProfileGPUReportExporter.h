#pragma once

#include "CoreMinimal.h"

struct FGraphicsCVarProfileGPUCaptureSet;

struct FGraphicsCVarProfileGPUReportExportResult
{
	bool bSuccess = false;
	FString MarkdownPath;
	FString JsonPath;
	FString ErrorMessage;
};

class FGraphicsCVarProfileGPUReportExporter
{
public:
	static FGraphicsCVarProfileGPUReportExportResult ExportReport(
		const FGraphicsCVarProfileGPUCaptureSet& Baseline,
		const FGraphicsCVarProfileGPUCaptureSet& Candidate);
};
