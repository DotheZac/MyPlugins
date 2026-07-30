#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Widgets/SCompoundWidget.h"

class SGraphicsCVarDebugViewsPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphicsCVarDebugViewsPanel)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildViewModeButton(
		const FString& Label,
		const FString& Tooltip,
		EViewModeIndex ViewMode);
	TSharedRef<SWidget> BuildLumenModeButton(
		const FString& Label,
		const FString& Tooltip,
		FName LumenMode);

	void ApplyViewMode(EViewModeIndex ViewMode, const FString& Label);
	void ApplyLumenMode(FName LumenMode, const FString& Label);
	bool IsViewModeActive(EViewModeIndex ViewMode) const;
	bool IsLumenModeActive(FName LumenMode) const;
	FText GetActiveModeText() const;

	FString LastStatus;
	bool bLastActionSucceeded = true;
};
