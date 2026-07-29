#include "Modules/ModuleManager.h"

#include "GraphicsCVarProfiler.h"

#include "Containers/Map.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

static const FName GraphicsCVarControlTabName(TEXT("GraphicsCVarControl"));
static const FName GraphicsCVarProfilerTabName(TEXT("GraphicsCVarProfiler"));

struct FCVarOption
{
	const TCHAR* Label;
	const TCHAR* Value;
};

struct FCVarControl
{
	const TCHAR* Category;
	const TCHAR* DisplayName;
	const TCHAR* CVarName;
	TArray<FCVarOption> Options;
};

static const TArray<FCVarControl>& GetGraphicsCVarControls()
{
	static const TArray<FCVarControl> Controls = []()
	{
		TArray<FCVarControl> Result;

		Result.Add({ TEXT("Anti-Aliasing"), TEXT("AA Method"), TEXT("r.AntiAliasingMethod"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("FXAA"), TEXT("1") }, { TEXT("TAA"), TEXT("2") }, { TEXT("TSR"), TEXT("4") } } });
		Result.Add({ TEXT("Anti-Aliasing"), TEXT("TAA Quality"), TEXT("r.TemporalAA.Quality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Basic"), TEXT("1") }, { TEXT("High"), TEXT("2") } } });
		Result.Add({ TEXT("Anti-Aliasing"), TEXT("FXAA Quality"), TEXT("r.FXAA.Quality"),
			{ { TEXT("0"), TEXT("0") }, { TEXT("2"), TEXT("2") }, { TEXT("4"), TEXT("4") }, { TEXT("5"), TEXT("5") } } });

		Result.Add({ TEXT("Resolution"), TEXT("Screen Percentage"), TEXT("r.ScreenPercentage"),
			{ { TEXT("50"), TEXT("50") }, { TEXT("70"), TEXT("70") }, { TEXT("100"), TEXT("100") }, { TEXT("120"), TEXT("120") } } });
		Result.Add({ TEXT("Resolution"), TEXT("View Distance Scale"), TEXT("r.ViewDistanceScale"),
			{ { TEXT("0.5"), TEXT("0.5") }, { TEXT("1.0"), TEXT("1") }, { TEXT("2.0"), TEXT("2") } } });

		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI"), TEXT("r.Lumen.DiffuseIndirect.Allow"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflections"), TEXT("r.Lumen.Reflections.Allow"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Shadow Quality"), TEXT("r.ShadowQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("2") }, { TEXT("Max"), TEXT("5") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Virtual Shadows"), TEXT("r.Shadow.Virtual.Enable"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Ambient Occlusion"), TEXT("r.AmbientOcclusionLevels"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Low"), TEXT("1") }, { TEXT("High"), TEXT("3") } } });

		Result.Add({ TEXT("Post Process"), TEXT("Motion Blur"), TEXT("r.MotionBlurQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("High"), TEXT("4") } } });
		Result.Add({ TEXT("Post Process"), TEXT("Bloom"), TEXT("r.BloomQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Good"), TEXT("3") }, { TEXT("Best"), TEXT("5") } } });
		Result.Add({ TEXT("Post Process"), TEXT("Screen Space Reflections"), TEXT("r.SSR.Quality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("2") }, { TEXT("High"), TEXT("4") } } });
		Result.Add({ TEXT("Post Process"), TEXT("Depth of Field"), TEXT("r.DepthOfFieldQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("2") }, { TEXT("High"), TEXT("4") } } });
		Result.Add({ TEXT("Post Process"), TEXT("Volumetric Fog"), TEXT("r.VolumetricFog"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });

		Result.Add({ TEXT("Geometry"), TEXT("Nanite"), TEXT("r.Nanite"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });

		Result.Add({ TEXT("Scalability"), TEXT("Global Illumination"), TEXT("sg.GlobalIlluminationQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });
		Result.Add({ TEXT("Scalability"), TEXT("Reflections"), TEXT("sg.ReflectionQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });
		Result.Add({ TEXT("Scalability"), TEXT("Shadows"), TEXT("sg.ShadowQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });
		Result.Add({ TEXT("Scalability"), TEXT("Textures"), TEXT("sg.TextureQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });
		Result.Add({ TEXT("Scalability"), TEXT("Effects"), TEXT("sg.EffectsQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });
		Result.Add({ TEXT("Scalability"), TEXT("Post Process"), TEXT("sg.PostProcessQuality"),
			{ { TEXT("Low"), TEXT("0") }, { TEXT("Med"), TEXT("1") }, { TEXT("High"), TEXT("2") }, { TEXT("Epic"), TEXT("3") } } });

		return Result;
	}();

	return Controls;
}

static FString GetCVarDescription(const FString& CVarName)
{
	static const TMap<FString, FString> Descriptions =
	{
		{ TEXT("r.AntiAliasingMethod"), TEXT("안티앨리어싱 방식을 변경합니다. 0은 Off, 1은 FXAA, 2는 TAA, 4는 TSR입니다.") },
		{ TEXT("r.TemporalAA.Quality"), TEXT("Temporal AA의 품질 단계를 변경합니다.") },
		{ TEXT("r.FXAA.Quality"), TEXT("FXAA의 품질 단계를 변경합니다. 값이 높을수록 품질과 비용이 증가합니다.") },
		{ TEXT("r.ScreenPercentage"), TEXT("화면의 내부 렌더링 해상도 비율을 변경합니다. 100은 원본 해상도입니다.") },
		{ TEXT("r.ViewDistanceScale"), TEXT("오브젝트가 렌더링되는 거리의 전체 배율을 변경합니다.") },
		{ TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("Lumen의 Diffuse Indirect Lighting 사용 여부를 변경합니다.") },
		{ TEXT("r.Lumen.Reflections.Allow"), TEXT("Lumen Reflections 사용 여부를 변경합니다.") },
		{ TEXT("r.ShadowQuality"), TEXT("동적 그림자의 전체 품질 단계를 변경합니다. 0은 그림자를 끕니다.") },
		{ TEXT("r.Shadow.Virtual.Enable"), TEXT("Virtual Shadow Maps 사용 여부를 변경합니다.") },
		{ TEXT("r.AmbientOcclusionLevels"), TEXT("Screen Space Ambient Occlusion의 계산 단계를 변경합니다. 0은 AO를 끕니다.") },
		{ TEXT("r.MotionBlurQuality"), TEXT("Motion Blur의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.BloomQuality"), TEXT("Bloom 후처리 효과의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.SSR.Quality"), TEXT("Screen Space Reflections의 품질 단계를 변경합니다. 0은 SSR을 끕니다.") },
		{ TEXT("r.DepthOfFieldQuality"), TEXT("Depth of Field 후처리 효과의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.VolumetricFog"), TEXT("Volumetric Fog 렌더링 사용 여부를 변경합니다.") },
		{ TEXT("r.Nanite"), TEXT("현재 플랫폼에서 Nanite 렌더링 사용 여부를 변경합니다.") },
		{ TEXT("sg.GlobalIlluminationQuality"), TEXT("Global Illumination 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") },
		{ TEXT("sg.ReflectionQuality"), TEXT("Reflection 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") },
		{ TEXT("sg.ShadowQuality"), TEXT("Shadow 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") },
		{ TEXT("sg.TextureQuality"), TEXT("Texture 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") },
		{ TEXT("sg.EffectsQuality"), TEXT("Effect 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") },
		{ TEXT("sg.PostProcessQuality"), TEXT("Post Process 관련 Scalability 설정 묶음의 품질 단계를 변경합니다.") }
	};

	if (const FString* Description = Descriptions.Find(CVarName))
	{
		return *Description;
	}

	return FString::Printf(TEXT("%s 값을 변경합니다."), *CVarName);
}

static FString GetCVarValue(const FString& CVarName)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*CVarName))
	{
		return Variable->GetString();
	}

	return TEXT("missing");
}

static void SetCVarValue(const FString& CVarName, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*CVarName))
	{
		Variable->Set(*Value, ECVF_SetByConsole);
	}
}

static FString GetPresetSectionName(const int32 PresetIndex)
{
	return FString::Printf(TEXT("GraphicsCVarControl.Preset%d"), PresetIndex);
}

static bool HasPreset(const int32 PresetIndex)
{
	bool bHasPreset = false;
	GConfig->GetBool(*GetPresetSectionName(PresetIndex), TEXT("bHasPreset"), bHasPreset, GEditorPerProjectIni);
	return bHasPreset;
}

static void SavePreset(const int32 PresetIndex)
{
	const FString SectionName = GetPresetSectionName(PresetIndex);

	GConfig->EmptySection(*SectionName, GEditorPerProjectIni);
	GConfig->SetBool(*SectionName, TEXT("bHasPreset"), true, GEditorPerProjectIni);

	for (const FCVarControl& Control : GetGraphicsCVarControls())
	{
		const FString CVarName(Control.CVarName);
		GConfig->SetString(*SectionName, *CVarName, *GetCVarValue(CVarName), GEditorPerProjectIni);
	}

	GConfig->Flush(false, GEditorPerProjectIni);
}

static void LoadPreset(const int32 PresetIndex)
{
	if (!HasPreset(PresetIndex))
	{
		return;
	}

	const FString SectionName = GetPresetSectionName(PresetIndex);

	for (const FCVarControl& Control : GetGraphicsCVarControls())
	{
		const FString CVarName(Control.CVarName);
		FString Value;
		if (GConfig->GetString(*SectionName, *CVarName, Value, GEditorPerProjectIni))
		{
			SetCVarValue(CVarName, Value);
		}
	}
}

static void ClearPreset(const int32 PresetIndex)
{
	GConfig->EmptySection(*GetPresetSectionName(PresetIndex), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

class SGraphicsCVarControlPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphicsCVarControlPanel)
		: _ShowProfiler(false)
	{}
		SLATE_ARGUMENT(bool, ShowProfiler)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
		FString LastCategory;

		if (InArgs._ShowProfiler)
		{
			Content->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					BuildProfilerSection()
				];
		}
		else
		{
			Content->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(TEXT("Presets")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				];

			for (int32 PresetIndex = 1; PresetIndex <= 5; ++PresetIndex)
			{
				Content->AddSlot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						BuildPresetRow(PresetIndex)
					];
			}

			for (const FCVarControl& Control : GetGraphicsCVarControls())
			{
				const FString Category(Control.Category);
				if (Category != LastCategory)
				{
					LastCategory = Category;
					Content->AddSlot()
						.AutoHeight()
						.Padding(0.0f, 14.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(Category))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						];
				}

				Content->AddSlot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						BuildControlRow(Control)
					];
			}
		}

		ChildSlot
		[
			SNew(SBorder)
			.Padding(12.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Content
				]
			]
		];
	}

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		const uint64 CurrentRevision = FGraphicsCVarProfiler::Get().GetResultRevision();
		if (CurrentRevision != DisplayedResultRevision)
		{
			DisplayedResultRevision = CurrentRevision;
			RebuildComparisonRows();
		}
	}

private:
	TSharedRef<SWidget> BuildProfilerSection()
	{
		return SNew(SBorder)
			.Padding(10.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(TEXT("GPU Snapshot Comparison")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Sample Frames")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 12.0f, 0.0f)
						[
							SNew(SSpinBox<int32>)
								.MinValue(10)
								.MaxValue(600)
								.Value_Lambda([this]() { return SampleFrames; })
								.OnValueChanged_Lambda([this](const int32 NewValue)
								{
									SampleFrames = NewValue;
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Highlight >= (ms)")))
								.ToolTipText(FText::FromString(TEXT(
									"Baseline과 Candidate의 절대 시간 차이가 이 값 이상이면 행을 강조합니다.")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 12.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
								.MinValue(0.01f)
								.MaxValue(10.0f)
								.Delta(0.05f)
								.Value_Lambda([this]() { return HighlightThresholdMs; })
								.OnValueChanged_Lambda([this](const float NewValue)
								{
									HighlightThresholdMs = NewValue;
									RebuildComparisonRows();
								})
								.ToolTipText(FText::FromString(TEXT(
									"행 하이라이트에 사용할 최소 GPU 시간 변화량입니다.")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Capture Baseline")))
								.ToolTipText(FText::FromString(TEXT(
									"현재 CVar 상태와 GPU 성능을 기준값으로 측정합니다.\n"
									"비교할 설정으로 변경하기 전에 먼저 실행하세요.")))
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.OnClicked_Lambda([this]()
								{
									StartCapture(EGraphicsCVarCaptureTarget::Baseline);
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Capture Candidate")))
								.ToolTipText(FText::FromString(TEXT(
									"변경된 CVar 상태와 GPU 성능을 비교값으로 측정합니다.\n"
									"Baseline 측정 후 설정을 변경한 다음 실행하세요.")))
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.OnClicked_Lambda([this]()
								{
									StartCapture(EGraphicsCVarCaptureTarget::Candidate);
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Clear")))
								.ToolTipText(FText::FromString(TEXT(
									"저장된 Baseline과 Candidate Snapshot을 모두 초기화합니다.")))
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.OnClicked_Lambda([]()
								{
									FGraphicsCVarProfiler::Get().ClearSnapshots();
									return FReply::Handled();
								})
						]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text_Lambda([]()
						{
							return FGraphicsCVarProfiler::Get().GetStatusText();
						})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SProgressBar)
						.Percent_Lambda([]() -> TOptional<float>
						{
							return FGraphicsCVarProfiler::Get().IsCapturing()
								? TOptional<float>(FGraphicsCVarProfiler::Get().GetProgress())
								: TOptional<float>();
						})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ComparisonRowsBox, SVerticalBox)
				]
			];
	}

	void StartCapture(const EGraphicsCVarCaptureTarget Target)
	{
		TMap<FString, FString> CVarValues;
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			const FString CVarName(Control.CVarName);
			CVarValues.Add(CVarName, GetCVarValue(CVarName));
		}

		FGraphicsCVarProfiler::Get().StartCapture(Target, CVarValues, SampleFrames);
	}

	static FString FormatPassValue(const bool bHasValue, const double Value)
	{
		return bHasValue ? FString::Printf(TEXT("%.3f ms"), Value) : TEXT("--");
	}

	void RebuildComparisonRows()
	{
		if (!ComparisonRowsBox.IsValid())
		{
			return;
		}

		ComparisonRowsBox->ClearChildren();
		ComparisonRowsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.40f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("GPU Pass")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.18f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("Baseline")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.18f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("Candidate")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.24f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("Difference")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
			];

		const TArray<FGraphicsCVarPassComparison> Rows =
			FGraphicsCVarProfiler::Get().BuildComparison();
		if (Rows.IsEmpty())
		{
			ComparisonRowsBox->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 6.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(
							TEXT("Capture a Baseline and Candidate to compare GPU passes.")))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				];
			return;
		}

		for (const FGraphicsCVarPassComparison& Row : Rows)
		{
			const FString DifferenceText =
				Row.bHasBaseline && Row.bHasCandidate
				? Row.ChangePercent.IsSet()
					? FString::Printf(
						TEXT("%+.3f ms  (%+.1f%%)"),
						Row.DeltaMs,
						Row.ChangePercent.GetValue())
					: FString::Printf(TEXT("%+.3f ms"), Row.DeltaMs)
				: TEXT("--");

			const bool bMeaningfulChange =
				Row.bHasBaseline &&
				Row.bHasCandidate &&
				FMath::Abs(Row.DeltaMs) >= HighlightThresholdMs;
			const FLinearColor DifferenceColor =
				!Row.bHasBaseline ||
				!Row.bHasCandidate ||
				FMath::IsNearlyZero(Row.DeltaMs, 0.001)
					? FLinearColor(0.70f, 0.70f, 0.70f)
					: Row.DeltaMs < 0.0
						? FLinearColor(0.45f, 1.00f, 0.50f)
						: FLinearColor(1.00f, 0.45f, 0.40f);
			const FLinearColor RowBackgroundColor =
				!bMeaningfulChange
					? FLinearColor::White
					: Row.DeltaMs < 0.0
						? FLinearColor(0.10f, 0.55f, 0.14f, 1.00f)
						: FLinearColor(0.58f, 0.08f, 0.05f, 1.00f);

			ComparisonRowsBox->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 1.0f)
				[
					SNew(SBorder)
						.Padding(5.0f)
						.BorderBackgroundColor(FSlateColor(RowBackgroundColor))
						[
							SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(0.40f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(Row.DisplayName))
										.ToolTipText(FText::FromString(Row.Id))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.18f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(
											FormatPassValue(Row.bHasBaseline, Row.BaselineMs)))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.18f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(
											FormatPassValue(Row.bHasCandidate, Row.CandidateMs)))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.24f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(DifferenceText))
										.ColorAndOpacity(FSlateColor(DifferenceColor))
										.Font(FCoreStyle::GetDefaultFontStyle(
											bMeaningfulChange ? "Bold" : "Regular",
											9))
								]
						]
				];
		}
	}

	TSharedRef<SWidget> BuildPresetRow(const int32 PresetIndex) const
	{
		return SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.34f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Preset %d"), PresetIndex)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.18f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([PresetIndex]()
					{
						return FText::FromString(HasPreset(PresetIndex) ? TEXT("Saved") : TEXT("Empty"));
					})
					.ColorAndOpacity_Lambda([PresetIndex]()
					{
						return FSlateColor(HasPreset(PresetIndex) ? FLinearColor(0.35f, 0.75f, 0.40f) : FLinearColor(0.55f, 0.55f, 0.55f));
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.48f)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Save")))
						.OnClicked_Lambda([PresetIndex]()
						{
							SavePreset(PresetIndex);
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Load")))
						.IsEnabled_Lambda([PresetIndex]()
						{
							return HasPreset(PresetIndex);
						})
						.OnClicked_Lambda([PresetIndex]()
						{
							LoadPreset(PresetIndex);
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Clear")))
						.IsEnabled_Lambda([PresetIndex]()
						{
							return HasPreset(PresetIndex);
						})
						.OnClicked_Lambda([PresetIndex]()
						{
							ClearPreset(PresetIndex);
							return FReply::Handled();
						})
					]
				]
			];
	}

	TSharedRef<SWidget> BuildControlRow(const FCVarControl& Control) const
	{
		TSharedRef<SHorizontalBox> OptionsBox = SNew(SHorizontalBox);
		const FString CVarName(Control.CVarName);
		const FString Description = GetCVarDescription(CVarName);

		for (const FCVarOption& Option : Control.Options)
		{
			const FString OptionLabel(Option.Label);
			const FString Value(Option.Value);
			OptionsBox->AddSlot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(OptionLabel))
					.ToolTipText(FText::FromString(FString::Printf(
						TEXT("%s\n\n적용 값: %s (%s)"),
						*Description,
						*Value,
						*OptionLabel)))
					.OnClicked_Lambda([CVarName, Value]()
					{
						SetCVarValue(CVarName, Value);
						return FReply::Handled();
					})
				];
		}

		return SNew(SBorder)
			.Padding(8.0f)
			.ToolTipText(FText::FromString(Description))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.34f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Control.DisplayName))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(CVarName))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.18f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([CVarName]()
					{
						return FText::FromString(FString::Printf(TEXT("Value: %s"), *GetCVarValue(CVarName)));
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.48f)
				.VAlign(VAlign_Center)
				[
					OptionsBox
				]
			];
	}

	int32 SampleFrames = 60;
	float HighlightThresholdMs = 0.2f;
	uint64 DisplayedResultRevision = MAX_uint64;
	TSharedPtr<SVerticalBox> ComparisonRowsBox;
};

class FGraphicsCVarControlEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicsCVarControlTabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicsCVarControlEditorModule::SpawnControlTab))
			.SetDisplayName(FText::FromString(TEXT("Graphics CVar Control")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicsCVarProfilerTabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicsCVarControlEditorModule::SpawnProfilerTab))
			.SetDisplayName(FText::FromString(TEXT("GPU Snapshot Comparison")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGraphicsCVarControlEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		FGraphicsCVarProfiler::Get().Shutdown();
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarControlTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarProfilerTabName);
	}

private:
	TSharedRef<SDockTab> SpawnControlTab(const FSpawnTabArgs& Args)
	{
		(void)Args;

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBox)
				.WidthOverride(760.0f)
				[
					SNew(SGraphicsCVarControlPanel)
						.ShowProfiler(false)
				]
			];
	}

	TSharedRef<SDockTab> SpawnProfilerTab(const FSpawnTabArgs& Args)
	{
		(void)Args;

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBox)
				.WidthOverride(980.0f)
				[
					SNew(SGraphicsCVarControlPanel)
						.ShowProfiler(true)
				]
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("GraphicsCVarControl"));

		Section.AddMenuEntry(
			TEXT("OpenGraphicsCVarControl"),
			FText::FromString(TEXT("Graphics CVar Control")),
			FText::FromString(TEXT("Open graphics console variable controls.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FGraphicsCVarControlEditorModule::OpenControlTab)));

		Section.AddMenuEntry(
			TEXT("OpenGraphicsCVarProfiler"),
			FText::FromString(TEXT("GPU Snapshot Comparison")),
			FText::FromString(TEXT("Open GPU baseline and candidate comparison.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FGraphicsCVarControlEditorModule::OpenProfilerTab)));
	}

	static void OpenControlTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarControlTabName);
	}

	static void OpenProfilerTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarProfilerTabName);
	}
};

IMPLEMENT_MODULE(FGraphicsCVarControlEditorModule, GraphicsCVarControlEditor)
