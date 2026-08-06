#include "Modules/ModuleManager.h"

#include "GraphicsCVarDebugViews.h"
#include "GraphicsCVarProfileGPUCapture.h"
#include "GraphicsCVarProfileGPUHelper.h"
#include "GraphicsCVarProfiler.h"
#include "GraphicsCVarReportExporter.h"

#include "Containers/Map.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
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
static const FName GraphicsCVarDebugViewsTabName(TEXT("GraphicsCVarDebugViews"));
static const FName GraphicsCVarProfileGPUHelperTabName(TEXT("GraphicsCVarProfileGPUHelper"));

namespace
{
	bool OpenStatGPUReportFolder()
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
}

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
		const TArray<FGraphicsCVarSpikeEvent>* BaselineSpikes =
			&Profiler.GetBaseline().SpikeEvents;
		const TArray<FGraphicsCVarSpikeEvent>* CandidateSpikes =
			&Profiler.GetCandidate().SpikeEvents;
		if (Profiler.IsCapturing())
		{
			if (Profiler.GetActiveTarget() == EGraphicsCVarCaptureTarget::Baseline)
			{
				BaselineSamples = &Profiler.GetActiveGPUFrameSamples();
				BaselineSpikes = &Profiler.GetActiveSpikeEvents();
			}
			else
			{
				CandidateSamples = &Profiler.GetActiveGPUFrameSamples();
				CandidateSpikes = &Profiler.GetActiveSpikeEvents();
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

		auto DrawSpikeMarkers = [&](
			const TArray<double>& Samples,
			const TArray<FGraphicsCVarSpikeEvent>& Events)
		{
			if (Samples.Num() < 2)
			{
				return;
			}

			for (const FGraphicsCVarSpikeEvent& Event : Events)
			{
				if (!Samples.IsValidIndex(Event.PeakFrame))
				{
					continue;
				}

				const float X = LocalSize.X * static_cast<float>(Event.PeakFrame) /
					static_cast<float>(Samples.Num() - 1);
				TArray<FVector2D> MarkerLine;
				MarkerLine.Add(FVector2D(X, 0.0f));
				MarkerLine.Add(FVector2D(X, LocalSize.Y));
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId + 4,
					AllottedGeometry.ToPaintGeometry(),
					MarkerLine,
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 0.08f, 0.04f, 0.9f),
					true,
					2.0f);
			}
		};
		DrawSpikeMarkers(*BaselineSamples, *BaselineSpikes);
		DrawSpikeMarkers(*CandidateSamples, *CandidateSpikes);
		return LayerId + 4;
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
	const TCHAR* EnabledWhenCVar = nullptr;
	const TCHAR* EnabledWhenValue = nullptr;
};

static const TArray<FCVarControl>& GetGraphicsCVarControls()
{
	static const TArray<FCVarControl> Controls = []()
	{
		TArray<FCVarControl> Result;

		Result.Add({ TEXT("Anti-Aliasing"), TEXT("AA Method"), TEXT("r.AntiAliasingMethod"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("FXAA"), TEXT("1") }, { TEXT("TAA"), TEXT("2") }, { TEXT("TSR"), TEXT("4") } } });
		Result.Add({ TEXT("Anti-Aliasing"), TEXT("TAA Quality"), TEXT("r.TemporalAA.Quality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Basic"), TEXT("1") }, { TEXT("High"), TEXT("2") } },
			TEXT("r.AntiAliasingMethod"), TEXT("2") });
		Result.Add({ TEXT("Anti-Aliasing"), TEXT("FXAA Quality"), TEXT("r.FXAA.Quality"),
			{ { TEXT("0"), TEXT("0") }, { TEXT("2"), TEXT("2") }, { TEXT("4"), TEXT("4") }, { TEXT("5"), TEXT("5") } },
			TEXT("r.AntiAliasingMethod"), TEXT("1") });

		Result.Add({ TEXT("Resolution"), TEXT("Screen Percentage"), TEXT("r.ScreenPercentage"),
			{ { TEXT("50"), TEXT("50") }, { TEXT("70"), TEXT("70") }, { TEXT("100"), TEXT("100") }, { TEXT("120"), TEXT("120") } } });
		Result.Add({ TEXT("Resolution"), TEXT("View Distance Scale"), TEXT("r.ViewDistanceScale"),
			{ { TEXT("0.5"), TEXT("0.5") }, { TEXT("1.0"), TEXT("1") }, { TEXT("2.0"), TEXT("2") } } });

		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI"), TEXT("r.Lumen.DiffuseIndirect.Allow"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflections"), TEXT("r.Lumen.Reflections.Allow"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflection Resolution"), TEXT("r.Lumen.Reflections.DownsampleFactor"),
			{ { TEXT("High"), TEXT("1") }, { TEXT("Balanced"), TEXT("2") }, { TEXT("Performance"), TEXT("4") } },
			TEXT("r.Lumen.Reflections.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Probe Spacing"), TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
			{ { TEXT("High"), TEXT("8") }, { TEXT("Balanced"), TEXT("16") }, { TEXT("Performance"), TEXT("32") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Screen Traces"), TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Mesh SDF Tracing"), TEXT("r.Lumen.ScreenProbeGather.TraceMeshSDFs"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Trace Resolution"), TEXT("r.Lumen.ScreenProbeGather.TracingOctahedronResolution"),
			{ { TEXT("Performance"), TEXT("4") }, { TEXT("Balanced"), TEXT("8") }, { TEXT("High"), TEXT("16") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Gather Resolution"), TEXT("r.Lumen.ScreenProbeGather.GatherOctahedronResolutionScale"),
			{ { TEXT("Performance"), TEXT("0.5") }, { TEXT("Balanced"), TEXT("1") }, { TEXT("High"), TEXT("2") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen GI Adaptive Probes"), TEXT("r.Lumen.ScreenProbeGather.NumAdaptiveProbes"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Low"), TEXT("4") }, { TEXT("Medium"), TEXT("8") }, { TEXT("High"), TEXT("16") } },
			TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflection Screen Traces"), TEXT("r.Lumen.Reflections.ScreenTraces"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } },
			TEXT("r.Lumen.Reflections.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflection Mesh SDF Tracing"), TEXT("r.Lumen.Reflections.TraceMeshSDFs"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } },
			TEXT("r.Lumen.Reflections.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Lumen Reflection Max Roughness"), TEXT("r.Lumen.Reflections.MaxRoughnessToTrace"),
			{ { TEXT("Use PPV"), TEXT("-1") }, { TEXT("0.4"), TEXT("0.4") }, { TEXT("0.6"), TEXT("0.6") }, { TEXT("0.8"), TEXT("0.8") } },
			TEXT("r.Lumen.Reflections.Allow"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Shadow Quality"), TEXT("r.ShadowQuality"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("2") }, { TEXT("Max"), TEXT("5") } } });
		Result.Add({ TEXT("Lighting"), TEXT("Virtual Shadows"), TEXT("r.Shadow.Virtual.Enable"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("On"), TEXT("1") } } });
		Result.Add({ TEXT("Lighting"), TEXT("VSM Directional Rays"), TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("4") }, { TEXT("High"), TEXT("7") } },
			TEXT("r.Shadow.Virtual.Enable"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("VSM Local Rays"), TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"),
			{ { TEXT("Off"), TEXT("0") }, { TEXT("Medium"), TEXT("4") }, { TEXT("High"), TEXT("7") } },
			TEXT("r.Shadow.Virtual.Enable"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Single Layer Water VSM Shader Support"), TEXT("r.Water.SingleLayer.ShadersSupportVSMFiltering"),
			{ { TEXT("0"), TEXT("0") }, { TEXT("1"), TEXT("1") } },
			TEXT("r.Shadow.Virtual.Enable"), TEXT("1") });
		Result.Add({ TEXT("Lighting"), TEXT("Single Layer Water VSM Filtering"), TEXT("r.Water.SingleLayer.VSMFiltering"),
			{ { TEXT("0"), TEXT("0") }, { TEXT("1"), TEXT("1") } },
			TEXT("r.Water.SingleLayer.ShadersSupportVSMFiltering"), TEXT("1") });
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
			{ { TEXT("High"), TEXT("8") }, { TEXT("Balanced"), TEXT("16") }, { TEXT("Performance"), TEXT("32") } },
			TEXT("r.VolumetricFog"), TEXT("1") });
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
		{ TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"), TEXT("Lumen GI가 다른 추적 방식으로 넘어가기 전에 화면에 보이는 정보를 먼저 추적할지 변경합니다.") },
		{ TEXT("r.Lumen.ScreenProbeGather.TraceMeshSDFs"), TEXT("Lumen GI의 Software Ray Tracing에서 Mesh Distance Field를 추적할지 변경합니다. Hardware Ray Tracing 사용 시 영향이 없을 수 있습니다.") },
		{ TEXT("r.Lumen.ScreenProbeGather.TracingOctahedronResolution"), TEXT("Screen Probe 하나에서 수행하는 추적의 방향 해상도를 변경합니다. 값이 높을수록 Ray 수와 GPU 비용이 증가합니다.") },
		{ TEXT("r.Lumen.ScreenProbeGather.GatherOctahedronResolutionScale"), TEXT("Screen Probe 필터링과 적분 해상도의 배율을 변경합니다. 값이 높을수록 품질과 GPU 비용이 증가합니다.") },
		{ TEXT("r.Lumen.ScreenProbeGather.NumAdaptiveProbes"), TEXT("기본 Screen Probe마다 추가로 배치할 수 있는 Adaptive Probe 수를 변경합니다. 장면에 따라 세부 간접광과 GPU 비용이 증가합니다.") },
		{ TEXT("r.Lumen.Reflections.ScreenTraces"), TEXT("Lumen Reflection이 다른 추적 방식으로 넘어가기 전에 화면에 보이는 정보를 먼저 추적할지 변경합니다.") },
		{ TEXT("r.Lumen.Reflections.TraceMeshSDFs"), TEXT("Lumen Reflection의 Software Ray Tracing에서 Mesh Distance Field를 추적할지 변경합니다. Hardware Ray Tracing 사용 시 영향이 없을 수 있습니다.") },
		{ TEXT("r.Lumen.Reflections.MaxRoughnessToTrace"), TEXT("Lumen이 전용 Reflection Ray를 추적할 최대 Roughness를 변경합니다. Use PPV는 -1을 적용하여 Post Process Volume 설정을 사용합니다.") },
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

class FGraphicsCVarStabilizationTimer final
{
public:
	static FGraphicsCVarStabilizationTimer& Get()
	{
		static FGraphicsCVarStabilizationTimer Instance;
		return Instance;
	}

	void Restart(const FString& Trigger)
	{
		StartedAtSeconds = FPlatformTime::Seconds();
		LastTrigger = Trigger;
		bHasStarted = true;
	}

	FText GetDisplayText() const
	{
		if (!bHasStarted)
		{
			return FText::FromString(TEXT(
				"Stabilization Timer: 아직 시작되지 않았습니다."));
		}

		const double ElapsedSeconds =
			FMath::Clamp(
				FPlatformTime::Seconds() - StartedAtSeconds,
				0.0,
				RecommendedWaitSeconds);
		if (ElapsedSeconds < RecommendedWaitSeconds)
		{
			return FText::FromString(FString::Printf(
				TEXT("Stabilization Timer: %.3f초 / 권장 30초  |  마지막 시작: %s"),
				ElapsedSeconds,
				*LastTrigger));
		}

		return FText::FromString(FString::Printf(
			TEXT("Stabilization Timer: %.3f초 / 권장 시간 도달  |  마지막 시작: %s"),
			ElapsedSeconds,
			*LastTrigger));
	}

	FSlateColor GetDisplayColor() const
	{
		if (!bHasStarted)
		{
			return FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f));
		}

		const double ElapsedSeconds =
			FMath::Clamp(
				FPlatformTime::Seconds() - StartedAtSeconds,
				0.0,
				RecommendedWaitSeconds);
		return ElapsedSeconds < RecommendedWaitSeconds
			? FSlateColor(FLinearColor(1.0f, 0.18f, 0.12f))
			: FSlateColor(FLinearColor(0.25f, 0.90f, 0.35f));
	}

private:
	static constexpr double RecommendedWaitSeconds = 30.0;

	double StartedAtSeconds = 0.0;
	FString LastTrigger;
	bool bHasStarted = false;
};

static IConsoleVariable* GetCachedCVar(const FString& CVarName)
{
	static TMap<FString, IConsoleVariable*> CachedCVars;
	if (IConsoleVariable** CachedVariable = CachedCVars.Find(CVarName))
	{
		return *CachedVariable;
	}

	IConsoleVariable* Variable =
		IConsoleManager::Get().FindConsoleVariable(*CVarName);
	if (Variable)
	{
		CachedCVars.Add(CVarName, Variable);
	}
	return Variable;
}

static FString GetCVarValue(const FString& CVarName)
{
	if (IConsoleVariable* Variable = GetCachedCVar(CVarName))
	{
		return Variable->GetString();
	}
	return TEXT("missing");
}

static bool DoesCVarValueMatchOption(
	const FString& CVarName,
	const FString& OptionValue)
{
	const FString CurrentValue = GetCVarValue(CVarName).TrimStartAndEnd();
	const FString ExpectedValue = OptionValue.TrimStartAndEnd();
	if (CurrentValue == ExpectedValue)
	{
		return true;
	}
	if (CurrentValue.IsNumeric() && ExpectedValue.IsNumeric())
	{
		return FMath::IsNearlyEqual(
			FCString::Atod(*CurrentValue),
			FCString::Atod(*ExpectedValue),
			UE_DOUBLE_SMALL_NUMBER);
	}
	return false;
}

static bool IsCVarControlEffective(const FCVarControl& Control)
{
	return !Control.EnabledWhenCVar || !Control.EnabledWhenValue ||
		DoesCVarValueMatchOption(
			FString(Control.EnabledWhenCVar),
			FString(Control.EnabledWhenValue));
}

static FString GetCVarControlStateText(const FCVarControl& Control)
{
	const FString CVarName(Control.CVarName);
	const FString CurrentValue = GetCVarValue(CVarName).TrimStartAndEnd();
	if (CurrentValue.IsEmpty() || CurrentValue.Equals(TEXT("missing"), ESearchCase::IgnoreCase))
	{
		return TEXT("현재: 조회 불가");
	}
	if (!IsCVarControlEffective(Control))
	{
		return FString::Printf(TEXT("현재: %s · 적용 안 됨"), *CurrentValue);
	}

	for (const FCVarOption& Option : Control.Options)
	{
		if (DoesCVarValueMatchOption(CVarName, FString(Option.Value)))
		{
			return FString::Printf(TEXT("현재: %s (%s)"), Option.Label, *CurrentValue);
		}
	}
	return FString::Printf(TEXT("현재: %s · 프리셋 외"), *CurrentValue);
}

static FSlateColor GetCVarControlStateColor(const FCVarControl& Control)
{
	const FString CVarName(Control.CVarName);
	const FString CurrentValue = GetCVarValue(CVarName).TrimStartAndEnd();
	if (CurrentValue.IsEmpty() || CurrentValue.Equals(TEXT("missing"), ESearchCase::IgnoreCase))
	{
		return FSlateColor(FLinearColor(0.95f, 0.18f, 0.14f, 1.0f));
	}
	if (!IsCVarControlEffective(Control))
	{
		return FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f));
	}

	for (const FCVarOption& Option : Control.Options)
	{
		if (DoesCVarValueMatchOption(CVarName, FString(Option.Value)))
		{
			return FSlateColor(FLinearColor(0.08f, 0.9f, 0.24f, 1.0f));
		}
	}
	return FSlateColor(FLinearColor(0.25f, 0.65f, 1.0f, 1.0f));
}

static bool SetCVarValue(
	const FString& CVarName,
	const FString& Value,
	const bool bRestartStabilizationTimer = true)
{
	if (IConsoleVariable* Variable = GetCachedCVar(CVarName))
	{
		Variable->Set(*Value, ECVF_SetByConsole);
		if (bRestartStabilizationTimer)
		{
			FGraphicsCVarStabilizationTimer::Get().Restart(
				FString::Printf(TEXT("CVar 변경: %s"), *CVarName));
		}
		return true;
	}
	return false;
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
	bool bLoadedAnyCVar = false;

	for (const FCVarControl& Control : GetGraphicsCVarControls())
	{
		const FString CVarName(Control.CVarName);
		FString Value;
		if (GConfig->GetString(*SectionName, *CVarName, Value, GEditorPerProjectIni))
		{
			bLoadedAnyCVar |= SetCVarValue(CVarName, Value, false);
		}
	}

	if (bLoadedAnyCVar)
	{
		FGraphicsCVarStabilizationTimer::Get().Restart(FString::Printf(
			TEXT("Preset %d Load"),
			PresetIndex));
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
		ControlContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT(
						"No CVar options match the search text.")))
					.ColorAndOpacity(FSlateColor(
						FLinearColor(0.75f, 0.55f, 0.25f)))
					.Visibility_Lambda([this]()
					{
						return HasAnyControlMatch()
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
			];
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
							.Visibility_Lambda([this, Category]()
							{
								return DoesCategoryHaveMatch(Category)
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
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
						SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 10.0f)
							[
								SNew(SSearchBox)
									.HintText(FText::FromString(TEXT(
										"Search CVar options...")))
									.OnTextChanged_Lambda([this](const FText& NewText)
									{
										ControlSearchText = NewText.ToString();
										ControlSearchText.TrimStartAndEndInline();
										Invalidate(EInvalidateWidgetReason::Layout);
									})
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										ControlContent
									]
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
			RebuildSpikeRows();
		}

		const FGraphicsCVarProfiler& Profiler = FGraphicsCVarProfiler::Get();
		const TArray<FGraphicsCVarSpikeEvent>& VisibleSpikes =
			Profiler.IsCapturing()
				? Profiler.GetActiveSpikeEvents()
				: Profiler.GetBaseline().SpikeEvents;
		const uint64 CurrentSpikeSignature =
			(static_cast<uint64>(VisibleSpikes.Num()) << 32) |
			static_cast<uint32>(
				VisibleSpikes.IsEmpty() ? 0 : VisibleSpikes.Last().PeakFrame + 1);
		if (CurrentSpikeSignature != DisplayedSpikeSignature)
		{
			DisplayedSpikeSignature = CurrentSpikeSignature;
			RebuildSpikeRows();
		}
	}

private:
	bool DoesControlMatch(const FCVarControl& Control) const
	{
		return ControlSearchText.IsEmpty() ||
			FString(Control.Category).Contains(
				ControlSearchText,
				ESearchCase::IgnoreCase) ||
			FString(Control.DisplayName).Contains(
				ControlSearchText,
				ESearchCase::IgnoreCase) ||
			FString(Control.CVarName).Contains(
				ControlSearchText,
				ESearchCase::IgnoreCase);
	}

	bool DoesCategoryHaveMatch(const FString& Category) const
	{
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			if (Category == Control.Category && DoesControlMatch(Control))
			{
				return true;
			}
		}
		return false;
	}

	bool HasAnyControlMatch() const
	{
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			if (DoesControlMatch(Control))
			{
				return true;
			}
		}
		return false;
	}

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
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(TEXT(
							"중요: 레벨 시작 또는 CVar 변경 후 약 30초 동안 Streaming과 렌더링 캐시가 안정화된 뒤 Baseline/Candidate를 캡처하세요.")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FSlateColor(
							FLinearColor(1.0f, 0.12f, 0.08f)))
						.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
						.Text_Lambda([]()
						{
							return FGraphicsCVarStabilizationTimer::Get()
								.GetDisplayText();
						})
						.ColorAndOpacity_Lambda([]()
						{
							return FGraphicsCVarStabilizationTimer::Get()
								.GetDisplayColor();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.AutoWrapText(true)
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
									"Baseline 단독 분석 또는 Baseline/Candidate 비교 결과를 AI 분석용 Markdown과 JSON 파일로 저장합니다.\n"
									"플러그인의 Reports/StatGPU 폴더에 자동으로 생성됩니다.\n"
									"Baseline만 캡처한 상태에서도 사용할 수 있습니다.")))
								.IsEnabled_Lambda([]()
								{
									const FGraphicsCVarProfiler& Profiler =
										FGraphicsCVarProfiler::Get();
									return !Profiler.IsCapturing() &&
										Profiler.GetBaseline().bIsValid;
								})
								.OnClicked_Lambda([this]()
								{
									const FGraphicsCVarReportExportResult Result =
										FGraphicsCVarReportExporter::ExportReport(
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
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Open Report Folder")))
								.ToolTipText(FText::FromString(TEXT(
									"Plugins/GraphicsCVarControl/Reports 폴더를 Windows Explorer에서 엽니다.")))
								.OnClicked_Lambda([this]()
								{
									bLastExportSucceeded = OpenStatGPUReportFolder();
									ExportStatusMessage = bLastExportSucceeded
										? TEXT("Opened the Reports folder.")
										: TEXT("Failed to open the Reports folder.");
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
								.Text(FText::FromString(TEXT("Export Spike Log")))
								.ToolTipText(FText::FromString(TEXT(
									"감지된 Baseline/Candidate 스파이크 사건만 별도의 Markdown과 JSON 로그로 저장합니다.\n"
									"파일은 플러그인의 Reports/SpikeLogs 폴더에 GPUSpikeLog 이름으로 생성됩니다.")))
								.IsEnabled_Lambda([]()
								{
									const FGraphicsCVarProfiler& Profiler =
										FGraphicsCVarProfiler::Get();
									return !Profiler.IsCapturing() &&
										(
											!Profiler.GetBaseline().SpikeEvents.IsEmpty() ||
											!Profiler.GetCandidate().SpikeEvents.IsEmpty()
										);
								})
								.OnClicked_Lambda([this]()
								{
									const FGraphicsCVarReportExportResult Result =
										FGraphicsCVarReportExporter::ExportSpikeLog(
											FGraphicsCVarProfiler::Get());
									if (Result.bSuccess)
									{
										ExportStatusMessage = FString::Printf(
											TEXT("Spike log exported: %s and %s"),
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
				.Padding(0.0f, 4.0f, 0.0f, 8.0f)
				[
					SNew(SBorder)
						.Padding(6.0f)
						[
							SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
											SNew(SCheckBox)
												.IsChecked_Lambda([this]()
												{
													return bSpikeTrackingEnabled
														? ECheckBoxState::Checked
														: ECheckBoxState::Unchecked;
												})
												.OnCheckStateChanged_Lambda([this](
													const ECheckBoxState NewState)
												{
													bSpikeTrackingEnabled =
														NewState == ECheckBoxState::Checked;
												})
												.IsEnabled_Lambda([]()
												{
													return !FGraphicsCVarProfiler::Get().IsCapturing();
												})
												.ToolTipText(FText::FromString(TEXT(
													"연속 기록 중 Total GPU가 프레임 예산과 이동 기준값을 모두 넘으면 "
													"스파이크 사건과 증가한 GPU Pass를 기록합니다.")))
												[
													SNew(STextBlock)
														.Text(FText::FromString(TEXT("Spike Tracking")))
												]
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(14.0f, 0.0f, 4.0f, 0.0f)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Frame Budget")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SSpinBox<float>)
												.MinValue(0.1f)
												.MaxValue(1000.0f)
												.Delta(0.1f)
												.Value_Lambda([this]() { return SpikeFrameBudgetMs; })
												.OnValueChanged_Lambda([this](const float NewValue)
												{
													SpikeFrameBudgetMs = NewValue;
												})
												.IsEnabled_Lambda([this]()
												{
													return bSpikeTrackingEnabled &&
														!FGraphicsCVarProfiler::Get().IsCapturing();
												})
												.ToolTipText(FText::FromString(TEXT(
													"스파이크로 인정할 최소 Total GPU 시간입니다. "
													"60 FPS 예산은 16.67 ms입니다.")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(12.0f, 0.0f, 4.0f, 0.0f)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Delta")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SSpinBox<float>)
												.MinValue(0.01f)
												.MaxValue(1000.0f)
												.Delta(0.1f)
												.Value_Lambda([this]() { return SpikeDeltaThresholdMs; })
												.OnValueChanged_Lambda([this](const float NewValue)
												{
													SpikeDeltaThresholdMs = NewValue;
												})
												.IsEnabled_Lambda([this]()
												{
													return bSpikeTrackingEnabled &&
														!FGraphicsCVarProfiler::Get().IsCapturing();
												})
												.ToolTipText(FText::FromString(TEXT(
													"최근 프레임 중앙값보다 최소 몇 ms 높아야 하는지 설정합니다.")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(12.0f, 0.0f, 4.0f, 0.0f)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Rolling Frames")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(SSpinBox<int32>)
												.MinValue(30)
												.MaxValue(600)
												.Value_Lambda([this]() { return SpikeRollingWindowFrames; })
												.OnValueChanged_Lambda([this](const int32 NewValue)
												{
													SpikeRollingWindowFrames = NewValue;
												})
												.IsEnabled_Lambda([this]()
												{
													return bSpikeTrackingEnabled &&
														!FGraphicsCVarProfiler::Get().IsCapturing();
												})
												.ToolTipText(FText::FromString(TEXT(
													"평상시 Total GPU 중앙값을 계산할 이전 프레임 수입니다.")))
										]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 5.0f, 0.0f, 0.0f)
								[
									SNew(STextBlock)
										.Text(FText::FromString(TEXT(
											"기본 조건: Total GPU >= 16.67 ms 이면서 최근 120프레임 중앙값보다 2.0 ms 이상 증가. "
											"사건당 이전 30프레임과 이후 60프레임을 보존합니다.")))
										.ColorAndOpacity(FSlateColor(
											FLinearColor(0.70f, 0.70f, 0.70f)))
										.AutoWrapText(true)
								]
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
									SNew(STextBlock)
										.Text(FText::FromString(TEXT("Spike Events")))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
										.ToolTipText(FText::FromString(TEXT(
											"빨간 그래프 마커가 표시된 프레임과 당시 가장 크게 변한 GPU Pass를 보여줍니다.")))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SAssignNew(SpikeRowsBox, SVerticalBox)
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

	void StartContinuousCapture(const EGraphicsCVarCaptureTarget Target)
	{
		TMap<FString, FString> CVarValues;
		for (const FCVarControl& Control : GetGraphicsCVarControls())
		{
			const FString CVarName(Control.CVarName);
			CVarValues.Add(CVarName, GetCVarValue(CVarName));
		}

		FGraphicsCVarSpikeSettings SpikeSettings;
		SpikeSettings.bEnabled = bSpikeTrackingEnabled;
		SpikeSettings.FrameBudgetMs = SpikeFrameBudgetMs;
		SpikeSettings.DeltaThresholdMs = SpikeDeltaThresholdMs;
		SpikeSettings.RollingWindowFrames = SpikeRollingWindowFrames;
		SpikeSettings.PreFrames = 30;
		SpikeSettings.PostFrames = 60;
		SpikeSettings.MaxEvents = 20;

		FGraphicsCVarProfiler::Get().StartContinuousCapture(
			Target,
			CVarValues,
			bAutoStopContinuousCapture ? ContinuousTargetFrames : 0,
			SpikeSettings);
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

	void RebuildSpikeRows()
	{
		if (!SpikeRowsBox.IsValid())
		{
			return;
		}

		SpikeRowsBox->ClearChildren();
		const FGraphicsCVarProfiler& Profiler = FGraphicsCVarProfiler::Get();
		const bool bCapturingBaseline =
			Profiler.IsCapturing() &&
			Profiler.GetActiveTarget() == EGraphicsCVarCaptureTarget::Baseline;
		const bool bCapturingCandidate =
			Profiler.IsCapturing() &&
			Profiler.GetActiveTarget() == EGraphicsCVarCaptureTarget::Candidate;
		const TArray<FGraphicsCVarSpikeEvent>& BaselineEvents =
			bCapturingBaseline
				? Profiler.GetActiveSpikeEvents()
				: Profiler.GetBaseline().SpikeEvents;
		const TArray<FGraphicsCVarSpikeEvent>& CandidateEvents =
			bCapturingCandidate
				? Profiler.GetActiveSpikeEvents()
				: Profiler.GetCandidate().SpikeEvents;

		if (BaselineEvents.IsEmpty() && CandidateEvents.IsEmpty())
		{
			SpikeRowsBox->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(TEXT(
							"아직 감지된 스파이크가 없습니다. Spike Tracking을 켜고 연속 기록을 시작하세요.")))
						.ColorAndOpacity(FSlateColor(
							FLinearColor(0.55f, 0.55f, 0.55f)))
						.AutoWrapText(true)
				];
			return;
		}

		auto AppendEvents = [this](
			const FString& CaptureLabel,
			const TArray<FGraphicsCVarSpikeEvent>& Events)
		{
			for (const FGraphicsCVarSpikeEvent& Event : Events)
			{
				TSharedRef<SVerticalBox> EventBox = SNew(SVerticalBox);
				EventBox->AddSlot()
					.AutoHeight()
					[
						SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(
								TEXT("%s Spike #%d | Frame %d | Peak %.3f ms | "
									"Rolling %.3f ms | Delta +%.3f ms | %d frame span"),
								*CaptureLabel,
								Event.EventIndex,
								Event.PeakFrame,
								Event.PeakTotalMs,
								Event.BaselineTotalMs,
								Event.DeltaTotalMs,
								Event.LastSpikeFrame - Event.StartFrame + 1)))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(
								FLinearColor(1.0f, 0.35f, 0.28f)))
					];
				EventBox->AddSlot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 3.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(
								TEXT("Stored window: Frame %d - %d | Pass sample: %s"),
								Event.WindowStartFrame,
								Event.WindowEndFrame,
								Event.bHasAlignedPassSample
									? *FString::Printf(
										TEXT("Frame %d (%+d)"),
										Event.PassSampleFrame,
										Event.PassFrameOffset)
									: TEXT("Unavailable"))))
							.ColorAndOpacity(FSlateColor(
								Event.bHasAlignedPassSample
									? FLinearColor(0.60f, 0.60f, 0.60f)
									: FLinearColor(1.0f, 0.55f, 0.12f)))
					];

				if (!Event.bHasAlignedPassSample)
				{
					EventBox->AddSlot()
						.AutoHeight()
						.Padding(8.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT(
									"Peak 주변 ±5프레임에 유효한 GPU Pass 표본이 없어 원인 Pass를 표시하지 않습니다.")))
								.ColorAndOpacity(FSlateColor(
									FLinearColor(1.0f, 0.55f, 0.12f)))
								.AutoWrapText(true)
						];
				}

				const int32 DetailCount = FMath::Min(5, Event.PassDeltas.Num());
				for (int32 Index = 0; Index < DetailCount; ++Index)
				{
					const FGraphicsCVarSpikePassDelta& Delta =
						Event.PassDeltas[Index];
					const FString PercentText = Delta.ChangePercent.IsSet()
						? FString::Printf(
							TEXT(" (%+.1f%%)"),
							Delta.ChangePercent.GetValue())
						: TEXT("");
					const FLinearColor DeltaColor =
						Delta.DeltaMs < 0.0
							? FLinearColor(0.45f, 1.0f, 0.50f)
							: FLinearColor(1.0f, 0.45f, 0.40f);
					EventBox->AddSlot()
						.AutoHeight()
						.Padding(8.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("%d. %s: %.3f -> %.3f ms, %+.3f ms%s"),
									Index + 1,
									*Delta.DisplayName,
									Delta.BaselineMs,
									Delta.PeakMs,
									Delta.DeltaMs,
									*PercentText)))
								.ColorAndOpacity(FSlateColor(DeltaColor))
								.ToolTipText(FText::FromString(Delta.Id))
						];
				}

				SpikeRowsBox->AddSlot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						SNew(SBorder)
							.Padding(6.0f)
							.BorderBackgroundColor(FSlateColor(
								FLinearColor(0.20f, 0.025f, 0.02f, 1.0f)))
							[
								EventBox
							]
					];
			}
		};

		AppendEvents(TEXT("Baseline"), BaselineEvents);
		AppendEvents(TEXT("Candidate"), CandidateEvents);
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
		const FString ControlTooltip = Control.EnabledWhenCVar && Control.EnabledWhenValue
			? FString::Printf(
				TEXT("%s\n\n활성 조건: %s = %s"),
				*Description,
				Control.EnabledWhenCVar,
				Control.EnabledWhenValue)
			: Description;

		for (const FCVarOption& Option : Control.Options)
		{
			const FString OptionLabel(Option.Label);
			const FString Value(Option.Value);
			OptionsBox->AddSlot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[
						SNew(SBox)
						.WidthOverride(14.0f)
						.HeightOverride(4.0f)
						[
							SNew(SBorder)
							.Padding(0.0f)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor_Lambda([CVarName, Value]()
							{
								return FSlateColor(
									DoesCVarValueMatchOption(CVarName, Value)
										? FLinearColor(0.08f, 0.9f, 0.24f, 1.0f)
										: FLinearColor(0.075f, 0.085f, 0.095f, 1.0f));
							})
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SButton)
						.Text(FText::FromString(OptionLabel))
						.IsEnabled_Lambda([Control]()
						{
							return IsCVarControlEffective(Control);
						})
						.ToolTipText(FText::FromString(FString::Printf(
							TEXT("%s\n\n적용 값: %s (%s)"),
							*ControlTooltip,
							*Value,
							*OptionLabel)))
						.OnClicked_Lambda([CVarName, Value]()
						{
							SetCVarValue(CVarName, Value);
							return FReply::Handled();
						})
					]
				];
		}

		return SNew(SBorder)
			.Padding(8.0f)
			.ToolTipText(FText::FromString(ControlTooltip))
			.ColorAndOpacity_Lambda([Control]()
			{
				return FLinearColor(1.0f, 1.0f, 1.0f, IsCVarControlEffective(Control) ? 1.0f : 0.4f);
			})
			.Visibility_Lambda([this, Control]()
			{
				return DoesControlMatch(Control)
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
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
					.Text_Lambda([Control]()
					{
						return FText::FromString(GetCVarControlStateText(Control));
					})
					.ColorAndOpacity_Lambda([Control]()
					{
						return GetCVarControlStateColor(Control);
					})
					.ToolTipText(FText::FromString(TEXT("현재 CVar 값과 프리셋 일치 여부를 표시합니다. '프리셋 외'는 값이 켜지지 않았다는 뜻이 아니라, 준비된 버튼과 정확히 일치하지 않는다는 뜻입니다.")))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.48f)
				.VAlign(VAlign_Center)
				[
					OptionsBox
				]
			];
	}

	int32 ContinuousTargetFrames = 300;
	bool bAutoStopContinuousCapture = false;
	bool bSpikeTrackingEnabled = true;
	float SpikeFrameBudgetMs = 16.67f;
	float SpikeDeltaThresholdMs = 2.0f;
	int32 SpikeRollingWindowFrames = 120;
	FString ControlSearchText;
	FString ExportStatusMessage;
	bool bLastExportSucceeded = false;
	float HighlightThresholdMs = 0.2f;
	uint64 DisplayedResultRevision = MAX_uint64;
	uint64 DisplayedSpikeSignature = MAX_uint64;
	TSharedPtr<SVerticalBox> ComparisonRowsBox;
	TSharedPtr<SVerticalBox> SpikeRowsBox;
};

class FGraphicsCVarControlEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(
			this,
			&FGraphicsCVarControlEditorModule::HandleBeginPIE);

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

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicsCVarDebugViewsTabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicsCVarControlEditorModule::SpawnDebugViewsTab))
			.SetDisplayName(FText::FromString(TEXT("Rendering Debug Views")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GraphicsCVarProfileGPUHelperTabName,
			FOnSpawnTab::CreateRaw(this, &FGraphicsCVarControlEditorModule::SpawnProfileGPUHelperTab))
			.SetDisplayName(FText::FromString(TEXT("GPU Profile Helper")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGraphicsCVarControlEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (BeginPIEHandle.IsValid())
		{
			FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
			BeginPIEHandle.Reset();
		}

		FGraphicsCVarProfiler::Get().Shutdown();
		FGraphicsCVarProfileGPUCaptureService::Get().Shutdown();
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarControlTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarProfilerTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarDebugViewsTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarProfileGPUHelperTabName);
	}

private:
	void HandleBeginPIE(const bool bIsSimulating)
	{
		FGraphicsCVarStabilizationTimer::Get().Restart(
			bIsSimulating ? TEXT("Simulate 시작") : TEXT("PIE 시작"));
	}

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

	TSharedRef<SDockTab> SpawnDebugViewsTab(const FSpawnTabArgs& Args)
	{
		(void)Args;

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBox)
					.WidthOverride(720.0f)
					[
						SNew(SGraphicsCVarDebugViewsPanel)
					]
			];
	}

	TSharedRef<SDockTab> SpawnProfileGPUHelperTab(const FSpawnTabArgs& Args)
	{
		(void)Args;

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBox)
				.WidthOverride(1120.0f)
				[
					SNew(SGraphicsCVarProfileGPUHelperPanel)
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

		Section.AddMenuEntry(
			TEXT("OpenGraphicsCVarDebugViews"),
			FText::FromString(TEXT("Rendering Debug Views")),
			FText::FromString(TEXT("Open viewport rendering visualization controls.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FGraphicsCVarControlEditorModule::OpenDebugViewsTab)));

		Section.AddMenuEntry(
			TEXT("OpenGraphicsCVarProfileGPUHelper"),
			FText::FromString(TEXT("GPU Profile Helper")),
			FText::FromString(TEXT("Capture and inspect detailed ProfileGPU pass timings.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FGraphicsCVarControlEditorModule::OpenProfileGPUHelperTab)));
	}

	static void OpenControlTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarControlTabName);
	}

	static void OpenProfilerTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarProfilerTabName);
	}

	static void OpenDebugViewsTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarDebugViewsTabName);
	}

	static void OpenProfileGPUHelperTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarProfileGPUHelperTabName);
	}

	FDelegateHandle BeginPIEHandle;
};

IMPLEMENT_MODULE(FGraphicsCVarControlEditorModule, GraphicsCVarControlEditor)
