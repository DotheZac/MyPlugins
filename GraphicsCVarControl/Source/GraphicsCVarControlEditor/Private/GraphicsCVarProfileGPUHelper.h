#pragma once

#include "CoreMinimal.h"
#include "GraphicsCVarProfileGPUCapture.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

class SGraphicsCVarProfileGPUHelperPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphicsCVarProfileGPUHelperPanel)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;
	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override;

private:
	void AddSelectedContextObjects();
	void AddContextObject(const FGraphicsCVarProfileGPUContextObject& ContextObject);
	void RemoveContextObject(
		EGraphicsCVarProfileGPUContextObjectType Type,
		const FString& ObjectPath);
	void SetContextObjectQuantity(
		EGraphicsCVarProfileGPUContextObjectType Type,
		const FString& ObjectPath,
		int32 Quantity);
	void ClearContextObjects();
	void RebuildContextObjects();
	void RebuildResults();

	bool bShowVisualizer = false;
	bool bMultiCapture = false;
	int32 MultiSampleCount = 5;
	float MultiIntervalSeconds = 2.0f;
	FString CaptureMemo;
	FString ExportStatusMessage;
	bool bLastExportSucceeded = false;
	TArray<FGraphicsCVarProfileGPUContextObject> ContextObjects;
	uint64 DisplayedRevision = MAX_uint64;
	TSharedPtr<SVerticalBox> ContextObjectsBox;
	TSharedPtr<SVerticalBox> ResultsBox;
};
