#include "Modules/ModuleManager.h"

#include "GraphicsCVarProfiler.h"
#include "GraphicsCVarReportExporter.h"

#include "Containers/Map.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

static const FName GraphicsCVarControlTabName(TEXT("GraphicsCVarControl"));
static const FName GraphicsCVarProfilerTabName(TEXT("GraphicsCVarProfiler"));

class SGPUTotalHistoryGraph final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGPUTotalHistoryGraph)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		(void)InArgs;
		SetCanTick(true);
	}

	virtual FVector2D ComputeDesiredSize(const float LayoutScaleMultiplier) const override
	{
		(void)LayoutScaleMultiplier;
		return FVector2D(700.0f, 180.0f);
	}

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override
	{
		SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (FGraphicsCVarProfiler::Get().IsCapturing())
		{
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		const bool bParentEnabled) const override
	{
		(void)Args;
		(void)MyCullingRect;
		(void)InWidgetStyle;
		(void)bParentEnabled;

		const FGraphicsCVarProfiler& Profiler = FGraphicsCVarProfiler::Get();
		const TArray<double>* BaselineSamples = &Profiler.GetBaseline().GPUFrameSamples;
		const TArray<double>* CandidateSamples = &Profiler.GetCandidate().GPUFrameSamples;
		if (Profiler.IsCapturing())
		{
			if (Profiler.GetActiveTarget() == EGraphicsCVarCaptureTarget::Baseline)
			{
				BaselineSamples = &Profiler.GetActiveGPUFrameSamples();
			}
			else
			{
				CandidateSamples = &Profiler.GetActiveGPUFrameSamples();
			}
		}

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor(0.015f, 0.02f, 0.03f, 1.0f));

		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		for (int32 GridIndex = 1; GridIndex < 4; ++GridIndex)
		{
			const float Y = LocalSize.Y * static_cast<float>(GridIndex) / 4.0f;
			TArray<FVector2D> GridLine;
			GridLine.Add(FVector2D(0.0f, Y));
			GridLine.Add(FVector2D(LocalSize.X, Y));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				GridLine,
				ESlateDrawEffect::None,
				FLinearColor(0.12f, 0.14f, 0.18f, 1.0f),
				true,
				1.0f);
		}

		double MinValue = TNumericLimits<double>::Max();
		double MaxValue = 0.0;
		auto IncludeRange = [&MinValue, &MaxValue](const TArray<double>& Samples)
		{
			for (const double Sample : Samples)
			{
				MinValue = FMath::Min(MinValue, Sample);
				MaxValue = FMath::Max(MaxValue, Sample);
			}
		};
		IncludeRange(*BaselineSamples);
		IncludeRange(*CandidateSamples);

		if (MinValue == TNumericLimits<double>::Max())
		{
			return LayerId + 1;
		}

		const double RangePadding = FMath::Max((MaxValue - MinValue) * 0.1, 0.1);
		MinValue = FMath::Max(0.0, MinValue - RangePadding);
		MaxValue += RangePadding;
		const double ValueRange = FMath::Max(MaxValue - MinValue, 0.001);

		auto DrawSeries = [&](
			const TArray<double>& Samples,
			const FLinearColor& Color,
			const int32 SeriesLayer)
		{
			if (Samples.Num() < 2)
			{
				return;
			}

			const int32 MaxGraphPoints = FMath::Max(2, FMath::FloorToInt(LocalSize.X));
			const int32 SampleStep = FMath::Max(
				1,
				FMath::CeilToInt(
					static_cast<double>(Samples.Num()) /
					static_cast<double>(MaxGraphPoints)));
			TArray<FVector2D> Points;
			Points.Reserve(FMath::Min(Samples.Num(), MaxGraphPoints) + 1);
			for (int32 Index = 0; Index < Samples.Num(); Index += SampleStep)
			{
				const float X = LocalSize.X * static_cast<float>(Index) /
					static_cast<float>(Samples.Num() - 1);
				const float NormalizedY = static_cast<float>(
					(Samples[Index] - MinValue) / ValueRange);
				const float Y = LocalSize.Y * (1.0f - FMath::Clamp(NormalizedY, 0.0f, 1.0f));
				Points.Add(FVector2D(X, Y));
			}
			if ((Samples.Num() - 1) % SampleStep != 0)
			{
				const float NormalizedY = static_cast<float>(
					(Samples.Last() - MinValue) / ValueRange);
				Points.Add(FVector2D(
					LocalSize.X,
					LocalSize.Y * (1.0f - FMath::Clamp(NormalizedY, 0.0f, 1.0f))));
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				SeriesLayer,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				Color,
				true,
				1.5f);
		};

		DrawSeries(*BaselineSamples, FLinearColor(0.15f, 0.65f, 1.0f), LayerId + 2);
		DrawSeries(*CandidateSamples, FLinearColor(1.0f, 0.55f, 0.15f), LayerId + 3);
		return LayerId + 3;
	}
};

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
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflection Resolution"), TEXT("r.Lumen.Reflections.DownsampleFactor"),
			{ { TEXT("High"), TEXT("1") }, { TEXT("Balanced"), TEXT("2") }, { TEXT("Performance"), TEXT("4") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Probe Spacing"), TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
			{ { TEXT("High"), TEXT("8") }, { TEXT("Balanced"), TEXT("16") }, { TEXT("Performance"), TEXT("32") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Shadow Quality"), TEXT("r.ShadowQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("2") }, { TEXT("Max"), TEXT("5") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Virtual Shadows"), TEXT("r.Shadow.Virtual.Enable"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("VSM Directional Rays"), TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("4") }, { TEXT("High"), TEXT("7") } } });
		Result.Add({ TEXT("Lighting"), TEXT("VSM Local Rays"), TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("4") }, { TEXT("High"), TEXT("7") } } });
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
		Result.Add({ TEXT("Post Process"), TEXT("Volumetric Fog Grid"), TEXT("r.VolumetricFog.GridPixelSize"),
			{ { TEXT("High"), TEXT("8") }, { TEXT("Balanced"), TEXT("16") }, { TEXT("Performance"), TEXT("32") } } });
		Result.Add({ TEXT("Post Process"), TEXT("Separate Translucency Resolution"), TEXT("r.SeparateTranslucencyScreenPercentage"),
			{ { TEXT("50%"), TEXT("50") }, { TEXT("75%"), TEXT("75") }, { TEXT("100%"), TEXT("100") } } });

		Result.Add({ TEXT("Geometry"), TEXT("Nanite"), TEXT("r.Nanite"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Geometry"), TEXT("Static Mesh LOD Distance"), TEXT("r.StaticMeshLODDistanceScale"),
			{ { TEXT("High"), TEXT("0.5") }, { TEXT("Default"), TEXT("1") }, { TEXT("Performance"), TEXT("2") } } });

		Result.Add({ TEXT("Foliage"), TEXT("Foliage Density"), TEXT("foliage.DensityScale"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Half"), TEXT("0.5") }, { TEXT("Full"), TEXT("1") } } });

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
		{ TEXT("r.Lumen.Reflections.DownsampleFactor"), TEXT("Lumen Reflection 광선 추적의 다운샘플 배율을 변경합니다. 값이 클수록 내부 해상도와 GPU 비용이 낮아집니다.") },
		{ TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"), TEXT("Lumen Screen Probe가 배치되는 화면 타일 크기를 변경합니다. 값이 클수록 Probe 수와 GPU 비용이 줄어듭니다.") },
		{ TEXT("r.ShadowQuality"), TEXT("동적 그림자의 전체 품질 단계를 변경합니다. 0은 그림자를 끕니다.") },
		{ TEXT("r.Shadow.Virtual.Enable"), TEXT("Virtual Shadow Maps 사용 여부를 변경합니다.") },
		{ TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"), TEXT("Directional Light의 Virtual Shadow Map 소프트 섀도 Ray 수를 변경합니다. 값이 높을수록 부드러운 그림자의 품질과 비용이 증가합니다.") },
		{ TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"), TEXT("Point 및 Spot Light의 Virtual Shadow Map 소프트 섀도 Ray 수를 변경합니다. 값이 높을수록 품질과 비용이 증가합니다.") },
		{ TEXT("r.AmbientOcclusionLevels"), TEXT("Screen Space Ambient Occlusion의 계산 단계를 변경합니다. 0은 AO를 끕니다.") },
		{ TEXT("r.MotionBlurQuality"), TEXT("Motion Blur의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.BloomQuality"), TEXT("Bloom 후처리 효과의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.SSR.Quality"), TEXT("Screen Space Reflections의 품질 단계를 변경합니다. 0은 SSR을 끕니다.") },
		{ TEXT("r.DepthOfFieldQuality"), TEXT("Depth of Field 후처리 효과의 품질 단계를 변경합니다. 0은 효과를 끕니다.") },
		{ TEXT("r.VolumetricFog"), TEXT("Volumetric Fog 렌더링 사용 여부를 변경합니다.") },
		{ TEXT("r.VolumetricFog.GridPixelSize"), TEXT("Volumetric Fog 복셀 그리드의 XY 셀 크기를 픽셀 단위로 변경합니다. 값이 클수록 해상도와 GPU 비용이 낮아집니다.") },
		{ TEXT("r.SeparateTranslucencyScreenPercentage"), TEXT("Separate Translucency의 내부 렌더링 해상도 비율을 변경합니다. 값이 낮을수록 GPU 비용이 줄어듭니다.") },
		{ TEXT("r.Nanite"), TEXT("현재 플랫폼에서 Nanite 렌더링 사용 여부를 변경합니다.") },
		{ TEXT("r.StaticMeshLODDistanceScale"), TEXT("Static Mesh의 LOD 전환 거리 배율을 변경합니다. 값이 높을수록 더 낮은 LOD로 빠르게 전환됩니다.") },
		{ TEXT("foliage.DensityScale"), TEXT("Scalability가 활성화된 Foliage의 렌더링 밀도를 변경합니다. 0은 제거, 1은 원래 밀도입니다.") },
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

static FString GetCategoryDescription(const FString& Category)
{
	static const TMap<FString, FString> Descriptions =
	{
		{ TEXT("Anti-Aliasing"), TEXT("계단 현상을 줄이는 방식과 품질을 조절합니다.") },
		{ TEXT("Resolution"), TEXT("내부 렌더링 해상도와 오브젝트 표시 거리를 조절합니다.") },
		{ TEXT("Lighting"), TEXT("Lumen, 그림자, Ambient Occlusion 등 조명 계산의 품질과 비용을 조절합니다.") },
		{ TEXT("Post Process"), TEXT("렌더링 후 적용되는 화면 효과와 Fog, Translucency 품질을 조절합니다.") },
		{ TEXT("Geometry"), TEXT("Nanite와 Mesh LOD 등 지오메트리 처리 방식을 조절합니다.") },
		{ TEXT("Foliage"), TEXT("Foliage 인스턴스의 렌더링 밀도를 조절합니다.") },
		{ TEXT("Scalability"), TEXT("여러 렌더링 설정을 묶은 Unreal 품질 단계를 한 번에 변경합니다.") }
	};

	if (const FString* Description = Descriptions.Find(Category))
	{
		return *Description;
	}

	return TEXT("이 카테고리에 포함된 그래픽 설정을 조절합니다.");
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
		if (InArgs._ShowProfiler)
		{
			ChildSlot
			[
				SNew(SBorder)
				.Padding(12.0f)
				[
					SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							BuildProfilerSection()
						]
				]
			];
			return;
		}

		TSharedRef<SVerticalBox> PresetContent = SNew(SVerticalBox);
		PresetContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("Presets")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			];

		for (int32 PresetIndex = 1; PresetIndex <= 5; ++PresetIndex)
		{
			PresetContent->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					BuildPresetRow(PresetIndex)
				];
		}

		TSharedRef<SVerticalBox> ControlContent = SNew(SVerticalBox);
		FString LastCategory;
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			const FString Category(Control.Category);
			if (Category != LastCategory)
			{
				const bool bFirstCategory = LastCategory.IsEmpty();
				LastCategory = Category;
				const FString CategoryDescription = GetCategoryDescription(Category);
				ControlContent->AddSlot()
					.AutoHeight()
					.Padding(0.0f, bFirstCategory ? 0.0f : 14.0f, 0.0f, 6.0f)
					[
						SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
									.Text(FText::FromString(Category))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 2.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(CategoryDescription))
									.AutoWrapText(true)
									.ColorAndOpacity(FSlateColor(
										FLinearColor(0.55f, 0.55f, 0.55f)))
							]
					];
			}

			ControlContent->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					BuildControlRow(Control)
				];
		}

		ChildSlot
		[
			SNew(SBorder)
			.Padding(12.0f)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
							.WidthOverride(380.0f)
							[
								SNew(SBorder)
									.Padding(10.0f)
									[
										PresetContent
									]
							]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								ControlContent
							]
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
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Export AI Report")))
								.ToolTipText(FText::FromString(TEXT(
									"Baseline/Candidate 비교 결과를 AI 분석용 Markdown과 JSON 파일로 저장합니다.\n"
									"플러그인의 Reports 폴더에 자동으로 생성됩니다.\n"
									"두 Snapshot을 모두 캡처한 뒤 사용할 수 있습니다.")))
								.IsEnabled_Lambda([]()
								{
									const FGraphicsCVarProfiler& Profiler =
										FGraphicsCVarProfiler::Get();
									return !Profiler.IsCapturing() &&
										Profiler.GetBaseline().bIsValid &&
										Profiler.GetCandidate().bIsValid;
								})
								.OnClicked_Lambda([this]()
								{
									const FGraphicsCVarReportExportResult Result =
										FGraphicsCVarReportExporter::ExportComparisonReport(
											FGraphicsCVarProfiler::Get(),
											HighlightThresholdMs);
									if (Result.bSuccess)
									{
										ExportStatusMessage = FString::Printf(
											TEXT("AI report exported: %s and %s"),
											*Result.MarkdownPath,
											*Result.JsonPath);
										bLastExportSucceeded = true;
									}
									else
									{
										ExportStatusMessage = Result.ErrorMessage;
										bLastExportSucceeded = false;
									}
									return FReply::Handled();
								})
						]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Continuous Recording")))
								.ToolTipText(FText::FromString(TEXT(
									"수동 모드에서는 Stop을 누를 때까지 GPU 시간을 계속 기록합니다.\n"
									"Auto Stop 모드에서는 Target Frames에 도달하면 자동 종료합니다.\n"
									"결과는 선택한 Baseline 또는 Candidate Snapshot에 저장됩니다.")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(10.0f, 0.0f, 8.0f, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
								.IsChecked_Lambda([this]()
								{
									return bAutoStopContinuousCapture
										? ECheckBoxState::Checked
										: ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
								{
									bAutoStopContinuousCapture =
										NewState == ECheckBoxState::Checked;
								})
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.ToolTipText(FText::FromString(TEXT(
									"켜면 지정한 측정 프레임 수를 기록한 뒤 자동으로 종료합니다.\n"
									"끄면 Stop 버튼을 누를 때까지 기록합니다.")))
								[
									SNew(STextBlock)
										.Text(FText::FromString(TEXT("Auto Stop")))
								]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Target Frames")))
								.ToolTipText(FText::FromString(TEXT(
									"워밍업 이후 실제로 기록할 목표 프레임 수입니다.")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SSpinBox<int32>)
								.MinValue(10)
								.MaxValue(36000)
								.Value_Lambda([this]() { return ContinuousTargetFrames; })
								.OnValueChanged_Lambda([this](const int32 NewValue)
								{
									ContinuousTargetFrames = NewValue;
								})
								.IsEnabled_Lambda([this]()
								{
									return bAutoStopContinuousCapture &&
										!FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.ToolTipText(FText::FromString(TEXT(
									"Auto Stop에서 기록할 프레임 수를 10~36,000 사이로 설정합니다.\n"
									"기본값 300은 60 FPS 기준 약 5초입니다.")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Start Baseline")))
								.ToolTipText(FText::FromString(TEXT(
									"현재 CVar 상태로 Baseline 연속 기록을 시작합니다.\n"
									"플레이 구간 측정이 끝나면 Stop을 누르세요.")))
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.OnClicked_Lambda([this]()
								{
									StartContinuousCapture(EGraphicsCVarCaptureTarget::Baseline);
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Start Candidate")))
								.ToolTipText(FText::FromString(TEXT(
									"현재 CVar 상태로 Candidate 연속 기록을 시작합니다.\n"
									"플레이 구간 측정이 끝나면 Stop을 누르세요.")))
								.IsEnabled_Lambda([]()
								{
									return !FGraphicsCVarProfiler::Get().IsCapturing();
								})
								.OnClicked_Lambda([this]()
								{
									StartContinuousCapture(EGraphicsCVarCaptureTarget::Candidate);
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Stop")))
								.ToolTipText(FText::FromString(TEXT(
									"현재 연속 기록을 종료하고 평균, 최솟값, 최댓값을 Snapshot으로 확정합니다.")))
								.IsEnabled_Lambda([]()
								{
									return FGraphicsCVarProfiler::Get().IsContinuousCapture();
								})
								.OnClicked_Lambda([]()
								{
									FGraphicsCVarProfiler::Get().StopCapture();
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
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(ExportStatusMessage);
						})
						.ColorAndOpacity_Lambda([this]()
						{
							return FSlateColor(
								bLastExportSucceeded
									? FLinearColor(0.35f, 0.85f, 0.40f)
									: FLinearColor(1.0f, 0.45f, 0.40f));
						})
						.AutoWrapText(true)
						.Visibility_Lambda([this]()
						{
							return ExportStatusMessage.IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SProgressBar)
						.Percent_Lambda([]() -> TOptional<float>
						{
							return FGraphicsCVarProfiler::Get().IsCapturing() &&
								(
									!FGraphicsCVarProfiler::Get().IsContinuousCapture() ||
									FGraphicsCVarProfiler::Get().HasTargetFrameLimit()
								)
								? TOptional<float>(FGraphicsCVarProfiler::Get().GetProgress())
								: TOptional<float>();
						})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SBorder)
						.Padding(6.0f)
						[
							SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, 4.0f)
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Total GPU Frame History")))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(14.0f, 0.0f, 0.0f, 0.0f)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Baseline")))
												.ColorAndOpacity(FSlateColor(
													FLinearColor(0.15f, 0.65f, 1.0f)))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(10.0f, 0.0f, 0.0f, 0.0f)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Candidate")))
												.ColorAndOpacity(FSlateColor(
													FLinearColor(1.0f, 0.55f, 0.15f)))
										]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SBox)
										.HeightOverride(180.0f)
										[
											SNew(SGPUTotalHistoryGraph)
										]
								]
						]
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

	void StartContinuousCapture(const EGraphicsCVarCaptureTarget Target)
	{
		TMap<FString, FString> CVarValues;
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			const FString CVarName(Control.CVarName);
			CVarValues.Add(CVarName, GetCVarValue(CVarName));
		}

		FGraphicsCVarProfiler::Get().StartContinuousCapture(
			Target,
			CVarValues,
			bAutoStopContinuousCapture ? ContinuousTargetFrames : 0);
	}

	static FString FormatPassStats(
		const bool bHasValue,
		const double Average,
		const double Min,
		const double Max)
	{
		return bHasValue
			? FString::Printf(
				TEXT("Avg %.3f ms\nMin %.3f  Max %.3f"),
				Average,
				Min,
				Max)
			: TEXT("--");
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
					.FillWidth(0.36f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("GPU Pass")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.23f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("Baseline (Avg / Min / Max)")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.23f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(TEXT("Candidate (Avg / Min / Max)")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.18f)
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
								.FillWidth(0.36f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(Row.DisplayName))
										.ToolTipText(FText::FromString(Row.Id))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.23f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(
											FormatPassStats(
												Row.bHasBaseline,
												Row.BaselineMs,
												Row.BaselineMinMs,
												Row.BaselineMaxMs)))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.23f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(
											FormatPassStats(
												Row.bHasCandidate,
												Row.CandidateMs,
												Row.CandidateMinMs,
												Row.CandidateMaxMs)))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(0.18f)
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
				.FillWidth(0.22f)
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
				.FillWidth(0.60f)
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
	int32 ContinuousTargetFrames = 300;
	bool bAutoStopContinuousCapture = false;
	FString ExportStatusMessage;
	bool bLastExportSucceeded = false;
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
