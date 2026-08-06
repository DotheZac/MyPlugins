#include "GraphicsCVarProfileGPUHelper.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "GraphicsCVarProfileGPUAssetAnalyzer.h"
#include "GraphicsCVarProfileGPUCapture.h"
#include "GraphicsCVarProfileGPUReportExporter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Selection.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool OpenProfileGPUReportFolder()
	{
		const TSharedPtr<IPlugin> Plugin =
			IPluginManager::Get().FindPlugin(TEXT("GraphicsCVarControl"));
		if (!Plugin.IsValid())
		{
			return false;
		}

		const FString ReportDirectory = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Reports")));
		IFileManager::Get().MakeDirectory(*ReportDirectory, true);
		if (!IFileManager::Get().DirectoryExists(*ReportDirectory))
		{
			return false;
		}

		FPlatformProcess::ExploreFolder(*ReportDirectory);
		return true;
	}

	FGraphicsCVarProfileGPUContextObject MakeAssetContextObject(
		const FAssetData& AssetData)
	{
		FGraphicsCVarProfileGPUContextObject Result;
		Result.Type = EGraphicsCVarProfileGPUContextObjectType::Asset;
		Result.DisplayName = AssetData.AssetName.ToString();
		Result.ObjectPath = AssetData.GetObjectPathString();
		Result.ClassPath = AssetData.AssetClassPath.ToString();
		Result.AnalysisDetails =
			FGraphicsCVarProfileGPUAssetAnalyzer::AnalyzeAsset(AssetData);
		return Result;
	}

	FGraphicsCVarProfileGPUContextObject MakeActorContextObject(const AActor& Actor)
	{
		FGraphicsCVarProfileGPUContextObject Result;
		Result.Type = EGraphicsCVarProfileGPUContextObjectType::Actor;
		Result.DisplayName = Actor.GetActorLabel();
		Result.ObjectPath = Actor.GetPathName();
		Result.ClassPath = Actor.GetClass()->GetPathName();
		Result.AnalysisDetails =
			FGraphicsCVarProfileGPUAssetAnalyzer::AnalyzeActor(Actor);
		return Result;
	}

	FString GetContextObjectTypeLabel(
		const EGraphicsCVarProfileGPUContextObjectType Type)
	{
		return Type == EGraphicsCVarProfileGPUContextObjectType::Actor
			? TEXT("Actor")
			: TEXT("Asset");
	}

	FString MakeContextSummary(const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		TArray<FString> Names;
		Names.Reserve(CaptureSet.ContextObjects.Num());
		for (const FGraphicsCVarProfileGPUContextObject& Item : CaptureSet.ContextObjects)
		{
			Names.Add(FString::Printf(
				TEXT("[%s] %s x%d"),
				*GetContextObjectTypeLabel(Item.Type),
				*Item.DisplayName,
				Item.Quantity));
		}
		return Names.IsEmpty() ? TEXT("없음") : FString::Join(Names, TEXT(", "));
	}

	FString GetQueueLabel(const EGraphicsCVarProfileGPUQueueType Type)
	{
		switch (Type)
		{
		case EGraphicsCVarProfileGPUQueueType::Copy:
			return TEXT("Copy");
		case EGraphicsCVarProfileGPUQueueType::Compute:
			return TEXT("Compute");
		case EGraphicsCVarProfileGPUQueueType::Graphics:
			return TEXT("Graphics");
		default:
			return TEXT("Unknown");
		}
	}

	TSharedRef<STextBlock> MakeMutedText(const FString& Text)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f)));
	}

	TSharedRef<SWidget> MakeStatisticsCell(
		const FString& Text,
		const FString& Tooltip,
		const bool bHeader,
		const FLinearColor& TextColor = FLinearColor::White)
	{
		return SNew(SBorder)
			.Padding(FMargin(6.0f, 4.0f))
			.BorderBackgroundColor(FSlateColor(
				bHeader
					? FLinearColor(0.12f, 0.14f, 0.18f, 1.0f)
					: FLinearColor(0.045f, 0.045f, 0.045f, 1.0f)))
			.ToolTipText(Tooltip.IsEmpty()
				? FText::GetEmpty()
				: FText::FromString(Tooltip))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(TextColor))
				.Font(FCoreStyle::GetDefaultFontStyle(bHeader ? "Bold" : "Regular", 9))
			];
	}

	TSharedRef<SWidget> MakeStatisticsRow(
		const FString& Label,
		const FString& Median,
		const FString& Average,
		const FString& Range,
		const FString& Seen,
		const bool bHeader,
		const FLinearColor& MedianColor = FLinearColor::White,
		const FLinearColor& AverageColor = FLinearColor::White)
	{
		const FString MedianTooltip = bHeader
			? TEXT("측정값을 크기순으로 정렬했을 때 가운데에 위치하는 값입니다. 큰 이상치의 영향을 평균보다 적게 받습니다.")
			: FString();
		const FString AverageTooltip = bHeader
			? TEXT("모든 측정값을 더한 뒤 촬영 횟수로 나눈 평균값입니다. 큰 이상치의 영향을 받을 수 있습니다.")
			: FString();
		const FString RangeTooltip = bHeader
			? TEXT("Baseline과 Candidate에서 기록된 최솟값부터 최댓값까지의 범위입니다.")
			: FString();
		const FString SeenTooltip = bHeader
			? TEXT("해당 Pass가 전체 촬영 중 실제로 감지된 횟수입니다. 감지되지 않은 샘플은 0ms로 계산됩니다.")
			: FString();

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.30f)
			.Padding(0.5f)
			[
				MakeStatisticsCell(Label, FString(), bHeader)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.20f)
			.Padding(0.5f)
			[
				MakeStatisticsCell(Median, MedianTooltip, bHeader, MedianColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.16f)
			.Padding(0.5f)
			[
				MakeStatisticsCell(Average, AverageTooltip, bHeader, AverageColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.22f)
			.Padding(0.5f)
			[
				MakeStatisticsCell(Range, RangeTooltip, bHeader)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.12f)
			.Padding(0.5f)
			[
				MakeStatisticsCell(Seen, SeenTooltip, bHeader)
			];
	}

	struct FProfileGPUValueStats
	{
		double Average = 0.0;
		double Median = 0.0;
		double Min = 0.0;
		double Max = 0.0;
	};

	FProfileGPUValueStats CalculateStats(const TArray<double>& Values)
	{
		FProfileGPUValueStats Result;
		if (Values.IsEmpty())
		{
			return Result;
		}

		TArray<double> SortedValues = Values;
		SortedValues.Sort();
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		Result.Average = Sum / Values.Num();
		Result.Min = SortedValues[0];
		Result.Max = SortedValues.Last();
		const int32 Middle = SortedValues.Num() / 2;
		Result.Median = SortedValues.Num() % 2 == 0
			? (SortedValues[Middle - 1] + SortedValues[Middle]) * 0.5
			: SortedValues[Middle];
		return Result;
	}

	FString MakeQueueKey(
		const EGraphicsCVarProfileGPUQueueType Type,
		const int32 QueueIndex,
		const int32 GPUIndex)
	{
		return FString::Printf(
			TEXT("%d_%d_%d"),
			static_cast<int32>(Type),
			QueueIndex,
			GPUIndex);
	}

	struct FAggregatedProfileGPUPass
	{
		FString Name;
		TArray<double> ExclusiveSamples;
		int32 OccurrenceCount = 0;
	};

	struct FAggregatedProfileGPUQueue
	{
		EGraphicsCVarProfileGPUQueueType Type =
			EGraphicsCVarProfileGPUQueueType::Unknown;
		int32 QueueIndex = 0;
		int32 GPUIndex = 0;
		TArray<double> FrameTimeSamples;
		TMap<FString, FAggregatedProfileGPUPass> Passes;
	};

	TMap<FString, FAggregatedProfileGPUQueue> BuildAggregatedQueues(
		const FGraphicsCVarProfileGPUCaptureSet& CaptureSet)
	{
		TMap<FString, FAggregatedProfileGPUQueue> Result;
		const int32 TotalSamples = CaptureSet.Samples.Num();
		for (int32 SampleIndex = 0; SampleIndex < TotalSamples; ++SampleIndex)
		{
			for (const FGraphicsCVarProfileGPUQueueCapture& Queue :
				CaptureSet.Samples[SampleIndex].Queues)
			{
				const FString QueueKey = MakeQueueKey(
					Queue.Type,
					Queue.QueueIndex,
					Queue.GPUIndex);
				FAggregatedProfileGPUQueue& Aggregate = Result.FindOrAdd(QueueKey);
				Aggregate.Type = Queue.Type;
				Aggregate.QueueIndex = Queue.QueueIndex;
				Aggregate.GPUIndex = Queue.GPUIndex;
				if (Aggregate.FrameTimeSamples.IsEmpty())
				{
					Aggregate.FrameTimeSamples.SetNumZeroed(TotalSamples);
				}
				Aggregate.FrameTimeSamples[SampleIndex] = Queue.FrameTimeMs;

				for (const FGraphicsCVarProfileGPUPass& Pass : Queue.Passes)
				{
					FAggregatedProfileGPUPass& PassAggregate =
						Aggregate.Passes.FindOrAdd(Pass.Path);
					PassAggregate.Name = Pass.Name;
					if (PassAggregate.ExclusiveSamples.IsEmpty())
					{
						PassAggregate.ExclusiveSamples.SetNumZeroed(TotalSamples);
					}
					PassAggregate.ExclusiveSamples[SampleIndex] = Pass.ExclusiveMs;
					++PassAggregate.OccurrenceCount;
				}
			}
		}
		return Result;
	}
}

void SGraphicsCVarProfileGPUHelperPanel::Construct(const FArguments& InArgs)
{
	(void)InArgs;
	SetCanTick(true);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(10.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("GPU Profile Helper")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				MakeMutedText(
					TEXT("ProfileGPU 단일 프레임을 캡처하고 Queue와 Pass별 비용을 정리합니다."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(
					TEXT("⚠ 캡처 전 약 30초간 안정화하고, 비교 시에는 대상 에셋을 제외한 카메라·해상도·CVar·조명·애니메이션 시점을 동일하게 유지하세요.")))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.16f, 0.12f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.055f, 0.065f, 0.08f, 1.0f)))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 5.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Current Capture Context")))
						.ToolTipText(FText::FromString(
							TEXT("다음 Baseline 또는 Candidate 캡처와 함께 저장할 메모와 관련 대상을 설정합니다.")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SMultiLineEditableTextBox)
						.HintText(FText::FromString(
							TEXT("이번 캡처에서 추가하거나 변경한 내용을 메모하세요.")))
						.Text_Lambda([this]()
						{
							return FText::FromString(CaptureMemo);
						})
						.OnTextChanged_Lambda([this](const FText& NewText)
						{
							CaptureMemo = NewText.ToString();
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SBorder)
						.Padding(10.0f)
						.BorderBackgroundColor(FSlateColor(FLinearColor(0.075f, 0.09f, 0.12f, 1.0f)))
						.ToolTipText(FText::FromString(
							TEXT("Content Browser의 에셋 또는 World Outliner의 Actor를 끌어다 놓으세요.")))
						[
							SNew(STextBlock)
							.Text(FText::FromString(
								TEXT("에셋 또는 World Outliner Actor를 여기에 드롭하세요")))
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.72f, 0.88f)))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 5.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Add Selected")))
							.ToolTipText(FText::FromString(
								TEXT("Content Browser에서 선택한 에셋과 레벨에서 선택한 Actor를 관련 대상으로 추가합니다.")))
							.OnClicked_Lambda([this]()
							{
								AddSelectedContextObjects();
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Clear Related")))
							.ToolTipText(FText::FromString(
								TEXT("현재 등록한 관련 에셋과 Actor만 초기화합니다. 메모는 유지됩니다.")))
							.IsEnabled_Lambda([this]() { return !ContextObjects.IsEmpty(); })
							.OnClicked_Lambda([this]()
							{
								ClearContextObjects();
								return FReply::Handled();
							})
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(ContextObjectsBox, SVerticalBox)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Capture Baseline")))
					.ToolTipText(FText::FromString(
						TEXT("현재 렌더링 프레임을 Baseline ProfileGPU 데이터로 저장합니다.")))
					.IsEnabled_Lambda([]()
					{
						return !FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.OnClicked_Lambda([this]()
					{
						FGraphicsCVarProfileGPUCaptureService::Get().StartCapture(
							EGraphicsCVarProfileGPUCaptureTarget::Baseline,
							bMultiCapture ? MultiSampleCount : 1,
							bMultiCapture ? MultiIntervalSeconds : 0.0f,
							bShowVisualizer && !bMultiCapture,
							CaptureMemo,
							ContextObjects);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Capture Candidate")))
					.ToolTipText(FText::FromString(
						TEXT("현재 렌더링 프레임을 Candidate ProfileGPU 데이터로 저장합니다.")))
					.IsEnabled_Lambda([]()
					{
						return !FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.OnClicked_Lambda([this]()
					{
						FGraphicsCVarProfileGPUCaptureService::Get().StartCapture(
							EGraphicsCVarProfileGPUCaptureTarget::Candidate,
							bMultiCapture ? MultiSampleCount : 1,
							bMultiCapture ? MultiIntervalSeconds : 0.0f,
							bShowVisualizer && !bMultiCapture,
							CaptureMemo,
							ContextObjects);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.ToolTipText(FText::FromString(
						TEXT("진행 중인 ProfileGPU 캡처를 취소하고 임시 CVar를 복구합니다.")))
					.IsEnabled_Lambda([]()
					{
						return FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.OnClicked_Lambda([]()
					{
						FGraphicsCVarProfileGPUCaptureService::Get().CancelCapture();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Clear")))
					.ToolTipText(FText::FromString(
						TEXT("저장된 ProfileGPU Baseline과 Candidate를 초기화합니다.")))
					.IsEnabled_Lambda([]()
					{
						return !FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.OnClicked_Lambda([]()
					{
						FGraphicsCVarProfileGPUCaptureService::Get().ClearCaptures();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Export AI Report")))
					.ToolTipText(FText::FromString(
						TEXT("ProfileGPU 통계, 메모, 수량, 관련 에셋 및 Actor 내부 분석을 Reports/ProfileGPU 폴더에 Markdown과 JSON으로 저장합니다. Baseline만 있어도 출력할 수 있습니다.")))
					.IsEnabled_Lambda([]()
					{
						const FGraphicsCVarProfileGPUCaptureService& Service =
							FGraphicsCVarProfileGPUCaptureService::Get();
						return !Service.IsCapturing() && Service.GetBaseline().bIsValid;
					})
					.OnClicked_Lambda([this]()
					{
						const FGraphicsCVarProfileGPUCaptureService& Service =
							FGraphicsCVarProfileGPUCaptureService::Get();
						const FGraphicsCVarProfileGPUReportExportResult Result =
							FGraphicsCVarProfileGPUReportExporter::ExportReport(
								Service.GetBaseline(), Service.GetCandidate());
						bLastExportSucceeded = Result.bSuccess;
						ExportStatusMessage = Result.bSuccess
							? FString::Printf(
								TEXT("Report saved: %s | %s"),
								*Result.MarkdownPath,
								*Result.JsonPath)
							: Result.ErrorMessage;
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Open Report Folder")))
					.ToolTipText(FText::FromString(
						TEXT("Plugins/GraphicsCVarControl/Reports 폴더를 Windows Explorer에서 엽니다.")))
					.OnClicked_Lambda([this]()
					{
						bLastExportSucceeded = OpenProfileGPUReportFolder();
						ExportStatusMessage = bLastExportSucceeded
							? TEXT("Opened the Reports folder.")
							: TEXT("Failed to open the Reports folder.");
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bShowVisualizer
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						bShowVisualizer = NewState == ECheckBoxState::Checked;
					})
					.IsEnabled_Lambda([this]()
					{
						return !bMultiCapture &&
							!FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.ToolTipText(FText::FromString(
						TEXT("캡처 완료 후 Unreal Engine 기본 GPU Visualizer 창도 함께 표시합니다.")))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Open original GPU Visualizer")))
						.Margin(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bMultiCapture
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.IsEnabled_Lambda([]()
					{
						return !FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						bMultiCapture = NewState == ECheckBoxState::Checked;
					})
					.ToolTipText(FText::FromString(
						TEXT("여러 ProfileGPU 프레임을 지정 간격으로 수집하고 통계를 계산합니다.")))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Multi Capture")))
						.Margin(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Samples")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(2)
					.MaxValue(20)
					.Value_Lambda([this]() { return MultiSampleCount; })
					.OnValueChanged_Lambda([this](const int32 Value)
					{
						MultiSampleCount = Value;
					})
					.IsEnabled_Lambda([this]()
					{
						return bMultiCapture &&
							!FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Interval (sec)")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<float>)
					.MinValue(0.1f)
					.MaxValue(10.0f)
					.Delta(0.1f)
					.Value_Lambda([this]() { return MultiIntervalSeconds; })
					.OnValueChanged_Lambda([this](const float Value)
					{
						MultiIntervalSeconds = Value;
					})
					.IsEnabled_Lambda([this]()
					{
						return bMultiCapture &&
							!FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing();
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([]()
				{
					return FText::FromString(
						FGraphicsCVarProfileGPUCaptureService::Get().GetStatus());
				})
				.ColorAndOpacity_Lambda([]()
				{
					return FSlateColor(
						FGraphicsCVarProfileGPUCaptureService::Get().IsCapturing()
							? FLinearColor(1.0f, 0.78f, 0.18f)
							: FLinearColor(0.72f, 0.72f, 0.72f));
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]()
				{
					return ExportStatusMessage.IsEmpty()
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				.Text_Lambda([this]()
				{
					return FText::FromString(ExportStatusMessage);
				})
				.AutoWrapText(true)
				.ColorAndOpacity_Lambda([this]()
				{
					return FSlateColor(bLastExportSucceeded
						? FLinearColor(0.25f, 0.92f, 0.42f)
						: FLinearColor(1.0f, 0.28f, 0.22f));
				})
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(ResultsBox, SVerticalBox)
				]
			]
		]
	];

	RebuildContextObjects();
	RebuildResults();
}

FReply SGraphicsCVarProfileGPUHelperPanel::OnDragOver(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	(void)MyGeometry;
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid() ||
		DragDropEvent.GetOperationAs<FActorDragDropOp>().IsValid())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SGraphicsCVarProfileGPUHelperPanel::OnDrop(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	(void)MyGeometry;
	if (const TSharedPtr<FAssetDragDropOp> AssetOperation =
		DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		for (const FAssetData& AssetData : AssetOperation->GetAssets())
		{
			AddContextObject(MakeAssetContextObject(AssetData));
		}
		return FReply::Handled();
	}

	if (const TSharedPtr<FActorDragDropOp> ActorOperation =
		DragDropEvent.GetOperationAs<FActorDragDropOp>())
	{
		for (const TWeakObjectPtr<AActor>& Actor : ActorOperation->Actors)
		{
			if (Actor.IsValid())
			{
				AddContextObject(MakeActorContextObject(*Actor.Get()));
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SGraphicsCVarProfileGPUHelperPanel::AddSelectedContextObjects()
{
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
	for (const FAssetData& AssetData : SelectedAssets)
	{
		AddContextObject(MakeAssetContextObject(AssetData));
	}

	if (GEditor && GEditor->GetSelectedActors())
	{
		for (FSelectionIterator Iterator(*GEditor->GetSelectedActors()); Iterator; ++Iterator)
		{
			if (const AActor* Actor = Cast<AActor>(*Iterator))
			{
				AddContextObject(MakeActorContextObject(*Actor));
			}
		}
	}
}

void SGraphicsCVarProfileGPUHelperPanel::AddContextObject(
	const FGraphicsCVarProfileGPUContextObject& ContextObject)
{
	if (ContextObject.ObjectPath.IsEmpty())
	{
		return;
	}
	const bool bAlreadyExists = ContextObjects.ContainsByPredicate(
		[&ContextObject](const FGraphicsCVarProfileGPUContextObject& Existing)
		{
			return Existing.Type == ContextObject.Type &&
				Existing.ObjectPath == ContextObject.ObjectPath;
		});
	if (!bAlreadyExists)
	{
		ContextObjects.Add(ContextObject);
		RebuildContextObjects();
	}
}

void SGraphicsCVarProfileGPUHelperPanel::RemoveContextObject(
	const EGraphicsCVarProfileGPUContextObjectType Type,
	const FString& ObjectPath)
{
	ContextObjects.RemoveAll(
		[Type, &ObjectPath](const FGraphicsCVarProfileGPUContextObject& Existing)
		{
			return Existing.Type == Type && Existing.ObjectPath == ObjectPath;
		});
	RebuildContextObjects();
}

void SGraphicsCVarProfileGPUHelperPanel::SetContextObjectQuantity(
	const EGraphicsCVarProfileGPUContextObjectType Type,
	const FString& ObjectPath,
	const int32 Quantity)
{
	if (Type != EGraphicsCVarProfileGPUContextObjectType::Asset)
	{
		return;
	}
	if (FGraphicsCVarProfileGPUContextObject* Item = ContextObjects.FindByPredicate(
		[Type, &ObjectPath](const FGraphicsCVarProfileGPUContextObject& Existing)
		{
			return Existing.Type == Type && Existing.ObjectPath == ObjectPath;
		}))
	{
		Item->Quantity = FMath::Clamp(Quantity, 1, 100000);
	}
}

void SGraphicsCVarProfileGPUHelperPanel::ClearContextObjects()
{
	ContextObjects.Reset();
	RebuildContextObjects();
}

void SGraphicsCVarProfileGPUHelperPanel::RebuildContextObjects()
{
	if (!ContextObjectsBox.IsValid())
	{
		return;
	}
	ContextObjectsBox->ClearChildren();
	if (ContextObjects.IsEmpty())
	{
		ContextObjectsBox->AddSlot()
		.AutoHeight()
		[
			MakeMutedText(TEXT("등록된 관련 에셋 또는 Actor가 없습니다."))
		];
		return;
	}

	for (const FGraphicsCVarProfileGPUContextObject& Item : ContextObjects)
	{
		const EGraphicsCVarProfileGPUContextObjectType Type = Item.Type;
		const FString ObjectPath = Item.ObjectPath;
		const int32 Quantity = Item.Quantity;
		const FString AnalysisPreview = Item.AnalysisDetails.IsEmpty()
			? TEXT("분석 정보 없음")
			: FString::Join(Item.AnalysisDetails, TEXT(" | ")).Left(220);
		const FString DetailTooltip = Item.AnalysisDetails.IsEmpty()
			? Item.ObjectPath
			: FString::Printf(
				TEXT("%s\n\n%s"),
				*Item.ObjectPath,
				*FString::Join(Item.AnalysisDetails, TEXT("\n")));
		ContextObjectsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("[%s] %s   %s"),
						*GetContextObjectTypeLabel(Item.Type),
						*Item.DisplayName,
						*Item.ClassPath)))
					.ToolTipText(FText::FromString(DetailTooltip))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 1.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(AnalysisPreview))
					.ToolTipText(FText::FromString(DetailTooltip))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.64f, 0.72f)))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Quantity")))
				.ToolTipText(FText::FromString(
					Type == EGraphicsCVarProfileGPUContextObjectType::Asset
						? TEXT("캡처 장면에 추가된 해당 에셋의 대략적인 수량입니다. 비용이 수량에 선형 비례한다는 의미는 아닙니다.")
						: TEXT("World Outliner Actor는 실제 인스턴스 하나로 기록되므로 수량이 1로 고정됩니다.")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(100000)
				.MinSliderValue(1)
				.MaxSliderValue(1000)
				.Value(Quantity)
				.IsEnabled(Type == EGraphicsCVarProfileGPUContextObjectType::Asset)
				.ToolTipText(FText::FromString(
					Type == EGraphicsCVarProfileGPUContextObjectType::Asset
						? TEXT("실제 비교 장면에 추가한 에셋 수량을 입력하세요.")
						: TEXT("실제 Actor 한 개를 나타내므로 1로 고정됩니다.")))
				.OnValueChanged_Lambda([this, Type, ObjectPath](const int32 NewQuantity)
				{
					SetContextObjectQuantity(Type, ObjectPath, NewQuantity);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Remove")))
				.OnClicked_Lambda([this, Type, ObjectPath]()
				{
					RemoveContextObject(Type, ObjectPath);
					return FReply::Handled();
				})
			]
		];
	}
}

void SGraphicsCVarProfileGPUHelperPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const uint64 Revision =
		FGraphicsCVarProfileGPUCaptureService::Get().GetResultRevision();
	if (DisplayedRevision != Revision)
	{
		RebuildResults();
	}
}

void SGraphicsCVarProfileGPUHelperPanel::RebuildResults()
{
	if (!ResultsBox.IsValid())
	{
		return;
	}

	ResultsBox->ClearChildren();
	const FGraphicsCVarProfileGPUCaptureService& Service =
		FGraphicsCVarProfileGPUCaptureService::Get();
	DisplayedRevision = Service.GetResultRevision();
	const FGraphicsCVarProfileGPUCaptureSet& Baseline = Service.GetBaseline();
	const FGraphicsCVarProfileGPUCaptureSet& Candidate = Service.GetCandidate();
	const FGraphicsCVarProfileGPUCapture& Capture = Service.GetLastCapture();

	ResultsBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("Baseline: %s   |   Candidate: %s"),
			Baseline.bIsValid
				? *FString::Printf(TEXT("%d sample(s)"), Baseline.Samples.Num())
				: TEXT("Empty"),
			Candidate.bIsValid
				? *FString::Printf(TEXT("%d sample(s)"), Candidate.Samples.Num())
				: TEXT("Empty"))))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	if (Baseline.bIsValid || Candidate.bIsValid)
	{
		TSharedRef<SVerticalBox> StoredContextSection = SNew(SVerticalBox);
		StoredContextSection->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Stored Capture Context")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];

		if (Baseline.bIsValid)
		{
			StoredContextSection->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Baseline 메모: %s\n관련 대상: %s"),
					Baseline.Memo.IsEmpty() ? TEXT("없음") : *Baseline.Memo,
					*MakeContextSummary(Baseline))))
				.AutoWrapText(true)
			];
		}
		if (Candidate.bIsValid)
		{
			StoredContextSection->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Candidate 메모: %s\n관련 대상: %s"),
					Candidate.Memo.IsEmpty() ? TEXT("없음") : *Candidate.Memo,
					*MakeContextSummary(Candidate))))
				.AutoWrapText(true)
			];
		}

		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
			.Padding(7.0f)
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.055f, 0.065f, 0.08f, 1.0f)))
			[
				StoredContextSection
			]
		];
	}

	if (Baseline.bIsValid && Candidate.bIsValid)
	{
		const TMap<FString, FAggregatedProfileGPUQueue> BaselineQueues =
			BuildAggregatedQueues(Baseline);
		const TMap<FString, FAggregatedProfileGPUQueue> CandidateQueues =
			BuildAggregatedQueues(Candidate);

		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Baseline / Candidate Comparison")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		];

		for (const TPair<FString, FAggregatedProfileGPUQueue>& QueuePair : BaselineQueues)
		{
			const FAggregatedProfileGPUQueue& BaselineQueue = QueuePair.Value;
			const FAggregatedProfileGPUQueue* CandidateQueue =
				CandidateQueues.Find(QueuePair.Key);
			const FProfileGPUValueStats BaselineFrameStats =
				CalculateStats(BaselineQueue.FrameTimeSamples);
			const FProfileGPUValueStats CandidateFrameStats = CandidateQueue
				? CalculateStats(CandidateQueue->FrameTimeSamples)
				: FProfileGPUValueStats();
			const double FrameDeltaMs =
				CandidateFrameStats.Median - BaselineFrameStats.Median;
			const double FrameAverageDeltaMs =
				CandidateFrameStats.Average - BaselineFrameStats.Average;
			const FLinearColor FrameMedianColor = FrameDeltaMs > 0.0
				? FLinearColor(1.0f, 0.28f, 0.22f)
				: FrameDeltaMs < 0.0
					? FLinearColor(0.25f, 0.92f, 0.42f)
					: FLinearColor::White;
			const FLinearColor FrameAverageColor = FrameAverageDeltaMs > 0.0
				? FLinearColor(1.0f, 0.28f, 0.22f)
				: FrameAverageDeltaMs < 0.0
					? FLinearColor(0.25f, 0.92f, 0.42f)
					: FLinearColor::White;

			TSharedRef<SVerticalBox> QueueSection = SNew(SVerticalBox);
			QueueSection->AddSlot()
			.AutoHeight()
			.Padding(2.0f, 1.0f, 2.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s Queue %d / GPU %d"),
					*GetQueueLabel(BaselineQueue.Type),
					BaselineQueue.QueueIndex,
					BaselineQueue.GPUIndex)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			];
			QueueSection->AddSlot()
			.AutoHeight()
			[
				MakeStatisticsRow(
					TEXT("Pass / Queue"),
					TEXT("Median"),
					TEXT("Avg"),
					TEXT("Range"),
					TEXT("Seen"),
					true)
			];
			QueueSection->AddSlot()
			.AutoHeight()
			[
				MakeStatisticsRow(
					TEXT("Queue Frame Time"),
					FString::Printf(
						TEXT("B %.3f -> C %.3f ms\nDelta %+.3f"),
						BaselineFrameStats.Median,
						CandidateFrameStats.Median,
						FrameDeltaMs),
					FString::Printf(
						TEXT("B %.3f -> C %.3f ms\nDelta %+.3f"),
						BaselineFrameStats.Average,
						CandidateFrameStats.Average,
						FrameAverageDeltaMs),
					FString::Printf(
						TEXT("B %.3f–%.3f\nC %.3f–%.3f ms"),
						BaselineFrameStats.Min,
						BaselineFrameStats.Max,
						CandidateFrameStats.Min,
						CandidateFrameStats.Max),
					FString::Printf(
						TEXT("B %d/%d\nC %d/%d"),
						BaselineQueue.FrameTimeSamples.Num(),
						Baseline.Samples.Num(),
						CandidateQueue ? CandidateQueue->FrameTimeSamples.Num() : 0,
						Candidate.Samples.Num()),
					false,
					FrameMedianColor,
					FrameAverageColor)
			];

			struct FPassDelta
			{
				FString Name;
				FProfileGPUValueStats BaselineStats;
				FProfileGPUValueStats CandidateStats;
				int32 BaselineOccurrences = 0;
				int32 CandidateOccurrences = 0;
			};
			TMap<FString, FPassDelta> DeltasByPath;
			for (const TPair<FString, FAggregatedProfileGPUPass>& PassPair :
				BaselineQueue.Passes)
			{
				FPassDelta& Delta = DeltasByPath.FindOrAdd(PassPair.Key);
				Delta.Name = PassPair.Value.Name;
				Delta.BaselineStats = CalculateStats(PassPair.Value.ExclusiveSamples);
				Delta.BaselineOccurrences = PassPair.Value.OccurrenceCount;
			}
			if (CandidateQueue)
			{
				for (const TPair<FString, FAggregatedProfileGPUPass>& PassPair :
					CandidateQueue->Passes)
				{
					FPassDelta& Delta = DeltasByPath.FindOrAdd(PassPair.Key);
					Delta.Name = PassPair.Value.Name;
					Delta.CandidateStats = CalculateStats(PassPair.Value.ExclusiveSamples);
					Delta.CandidateOccurrences = PassPair.Value.OccurrenceCount;
				}
			}

			TArray<FPassDelta> RankedDeltas;
			DeltasByPath.GenerateValueArray(RankedDeltas);
			RankedDeltas.Sort([](const FPassDelta& A, const FPassDelta& B)
			{
				return FMath::Abs(A.CandidateStats.Median - A.BaselineStats.Median) >
					FMath::Abs(B.CandidateStats.Median - B.BaselineStats.Median);
			});

			const int32 DeltaDisplayCount = FMath::Min(20, RankedDeltas.Num());
			for (int32 Index = 0; Index < DeltaDisplayCount; ++Index)
			{
				const FPassDelta& Delta = RankedDeltas[Index];
				const double DeltaMs =
					Delta.CandidateStats.Median - Delta.BaselineStats.Median;
				const double AverageDeltaMs =
					Delta.CandidateStats.Average - Delta.BaselineStats.Average;
				const FLinearColor MedianColor = DeltaMs > 0.0
					? FLinearColor(1.0f, 0.28f, 0.22f)
					: DeltaMs < 0.0
						? FLinearColor(0.25f, 0.92f, 0.42f)
						: FLinearColor(0.72f, 0.72f, 0.72f);
				const FLinearColor AverageColor = AverageDeltaMs > 0.0
					? FLinearColor(1.0f, 0.28f, 0.22f)
					: AverageDeltaMs < 0.0
						? FLinearColor(0.25f, 0.92f, 0.42f)
						: FLinearColor(0.72f, 0.72f, 0.72f);
				QueueSection->AddSlot()
				.AutoHeight()
				[
					MakeStatisticsRow(
						FString::Printf(TEXT("%2d. %s"), Index + 1, *Delta.Name),
						FString::Printf(
							TEXT("B %.3f -> C %.3f ms\nDelta %+.3f"),
							Delta.BaselineStats.Median,
							Delta.CandidateStats.Median,
							DeltaMs),
						FString::Printf(
							TEXT("B %.3f -> C %.3f ms\nDelta %+.3f"),
							Delta.BaselineStats.Average,
							Delta.CandidateStats.Average,
							AverageDeltaMs),
						FString::Printf(
							TEXT("B %.3f–%.3f\nC %.3f–%.3f ms"),
							Delta.BaselineStats.Min,
							Delta.BaselineStats.Max,
							Delta.CandidateStats.Min,
							Delta.CandidateStats.Max),
						FString::Printf(
							TEXT("B %d/%d\nC %d/%d"),
							Delta.BaselineOccurrences,
							Baseline.Samples.Num(),
							Delta.CandidateOccurrences,
							Candidate.Samples.Num()),
						false,
						MedianColor,
						AverageColor)
				];
			}

			ResultsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 5.0f)
			[
				SNew(SBorder)
				.Padding(6.0f)
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.08f, 0.08f, 0.09f, 1.0f)))
				[
					QueueSection
				]
			];
		}

		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f)
		[
			SNew(SSeparator)
		];
	}

	if (!Capture.bIsValid)
	{
		ResultsBox->AddSlot()
		.AutoHeight()
		[
			MakeMutedText(TEXT("아직 수집된 ProfileGPU 결과가 없습니다."))
		];
		return;
	}

	ResultsBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("Latest: %s | Frame %d | Captured %s"),
			Service.GetLastTarget() == EGraphicsCVarProfileGPUCaptureTarget::Baseline
				? TEXT("Baseline")
				: TEXT("Candidate"),
			Capture.FrameNumber,
			*Capture.CapturedAt.ToString(TEXT("%Y-%m-%d %H:%M:%S")))))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	for (const FGraphicsCVarProfileGPUQueueCapture& Queue : Capture.Queues)
	{
		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("%s Queue %d / GPU %d | Frame %.3f ms | %d passes"),
				*GetQueueLabel(Queue.Type),
				Queue.QueueIndex,
				Queue.GPUIndex,
				Queue.FrameTimeMs,
				Queue.Passes.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];

		if (Queue.Passes.IsEmpty())
		{
			ResultsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 0.0f, 0.0f, 3.0f)
			[
				MakeMutedText(TEXT("No recorded work for this queue."))
			];
			continue;
		}

		TArray<const FGraphicsCVarProfileGPUPass*> RankedPasses;
		RankedPasses.Reserve(Queue.Passes.Num());
		for (const FGraphicsCVarProfileGPUPass& Pass : Queue.Passes)
		{
			if (Pass.Name != TEXT("<root>"))
			{
				RankedPasses.Add(&Pass);
			}
		}
		RankedPasses.Sort(
			[](const FGraphicsCVarProfileGPUPass& A, const FGraphicsCVarProfileGPUPass& B)
			{
				return A.ExclusiveMs > B.ExclusiveMs;
			});

		ResultsBox->AddSlot()
		.AutoHeight()
		.Padding(12.0f, 1.0f, 0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				TEXT("Top exclusive passes | Exclusive / Inclusive | Draws / Dispatches")))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.70f, 0.88f)))
		];

		const int32 DisplayCount = FMath::Min(20, RankedPasses.Num());
		for (int32 Index = 0; Index < DisplayCount; ++Index)
		{
			const FGraphicsCVarProfileGPUPass& Pass = *RankedPasses[Index];
			ResultsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%2d. %-54s  %8.3f / %8.3f ms   %lld / %lld"),
					Index + 1,
					*Pass.Name.Left(54),
					Pass.ExclusiveMs,
					Pass.InclusiveMs,
					Pass.ExclusiveDraws,
					Pass.ExclusiveDispatches)))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			];
		}
	}
}
