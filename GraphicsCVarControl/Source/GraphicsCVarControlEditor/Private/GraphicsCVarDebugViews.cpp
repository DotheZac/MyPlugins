#include "GraphicsCVarDebugViews.h"

#include "EditorViewportClient.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FEditorViewportClient* GetActiveLevelViewportClient()
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
		{
			return nullptr;
		}

		FLevelEditorModule& LevelEditorModule =
			FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		const TSharedPtr<IAssetViewport> ActiveViewport =
			LevelEditorModule.GetFirstActiveViewport();
		return ActiveViewport.IsValid()
			? &ActiveViewport->GetAssetViewportClient()
			: nullptr;
	}

	FString GetViewModeLabel(const EViewModeIndex ViewMode)
	{
		switch (ViewMode)
		{
		case VMI_BrushWireframe:
			return TEXT("Wireframe");
		case VMI_Unlit:
			return TEXT("Unlit");
		case VMI_Lit:
			return TEXT("Lit");
		case VMI_Lit_DetailLighting:
			return TEXT("Detail Lighting");
		case VMI_LightingOnly:
			return TEXT("Lighting Only");
		case VMI_LightComplexity:
			return TEXT("Light Complexity");
		case VMI_ShaderComplexity:
			return TEXT("Shader Complexity");
		case VMI_LightmapDensity:
			return TEXT("Lightmap Density");
		case VMI_QuadOverdraw:
			return TEXT("Quad Overdraw");
		case VMI_ShaderComplexityWithQuadOverdraw:
			return TEXT("Shader Complexity & Quads");
		case VMI_VisualizeLumen:
			return TEXT("Lumen Visualization");
		default:
			return FString::Printf(TEXT("View Mode %d"), static_cast<int32>(ViewMode));
		}
	}

	TSharedRef<STextBlock> MakeSectionDescription(const FString& Text)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.60f, 0.60f, 0.60f)));
	}
}

void SGraphicsCVarDebugViewsPanel::Construct(const FArguments& InArgs)
{
	(void)InArgs;

	TSharedRef<SWrapBox> BasicModes = SNew(SWrapBox).UseAllottedSize(true);
	BasicModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Lit"),
				TEXT("기본 Lit View Mode로 돌아갑니다."),
				VMI_Lit)
		];
	BasicModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Unlit"),
				TEXT("조명 계산 없이 Material의 기본 색상과 Emissive를 확인합니다."),
				VMI_Unlit)
		];
	BasicModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Wireframe"),
				TEXT("Mesh의 삼각형과 지오메트리 밀도를 Wireframe으로 확인합니다."),
				VMI_BrushWireframe)
		];
	BasicModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Detail Lighting"),
				TEXT("Material의 Base Color 영향을 줄이고 Normal과 조명 결과를 확인합니다."),
				VMI_Lit_DetailLighting)
		];
	BasicModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Lighting Only"),
				TEXT("Material 표현을 단순화하여 조명 결과만 집중해서 확인합니다."),
				VMI_LightingOnly)
		];

	TSharedRef<SWrapBox> PerformanceModes = SNew(SWrapBox).UseAllottedSize(true);
	PerformanceModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Shader Complexity"),
				TEXT("Pixel Shader 비용과 Overdraw를 색상으로 확인합니다. 일반적으로 밝은 빨강과 흰색에 가까울수록 비용이 높습니다."),
				VMI_ShaderComplexity)
		];
	PerformanceModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Shader Complexity & Quads"),
				TEXT("Shader 비용과 2x2 Pixel Quad 단위 Overdraw를 함께 확인합니다."),
				VMI_ShaderComplexityWithQuadOverdraw)
		];
	PerformanceModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Quad Overdraw"),
				TEXT("작은 삼각형과 겹치는 픽셀 때문에 발생하는 Quad Overdraw를 확인합니다."),
				VMI_QuadOverdraw)
		];
	PerformanceModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Light Complexity"),
				TEXT("한 픽셀에 영향을 주는 동적 Light 수를 색상으로 확인합니다."),
				VMI_LightComplexity)
		];
	PerformanceModes->AddSlot()
		.Padding(3.0f)
		[
			BuildViewModeButton(
				TEXT("Lightmap Density"),
				TEXT("Static Lighting을 사용할 때 Lightmap Texel Density를 확인합니다."),
				VMI_LightmapDensity)
		];

	TSharedRef<SWrapBox> LumenModes = SNew(SWrapBox).UseAllottedSize(true);
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Overview"),
				TEXT("주요 Lumen Visualization을 타일 형태로 한 번에 확인합니다."),
				TEXT("Overview"))
		];
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Performance Overview"),
				TEXT("Lumen 성능 진단용 Visualization을 타일 형태로 확인합니다."),
				TEXT("PerformanceOverview"))
		];
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Lumen Scene"),
				TEXT("Lumen이 사용하는 Scene 표현을 높은 품질과 거리로 확인합니다."),
				TEXT("LumenScene"))
		];
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Surface Cache"),
				TEXT("Lumen Surface Cache Coverage를 확인합니다. 분홍색은 Coverage 누락, 노란색은 Cull된 Mesh를 의미합니다."),
				TEXT("SurfaceCache"))
		];
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Geometry Normals"),
				TEXT("현재 Lumen 설정에서 사용하는 Geometry Normal을 확인합니다."),
				TEXT("GeometryNormals"))
		];
	LumenModes->AddSlot()
		.Padding(3.0f)
		[
			BuildLumenModeButton(
				TEXT("Reflection View"),
				TEXT("현재 Reflection 설정으로 Lumen Scene 표현을 확인합니다."),
				TEXT("ReflectionView"))
		];

	ChildSlot
	[
		SNew(SBorder)
			.Padding(12.0f)
			[
				SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("Rendering Debug Views")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								MakeSectionDescription(TEXT(
									"버튼을 누르면 현재 활성 Level Editor Viewport의 View Mode가 변경됩니다. "
									"원래 화면으로 돌아가려면 Lit을 누르세요."))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("Basic Views")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								BasicModes
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("Performance Views")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								PerformanceModes
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("Lumen Views")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								LumenModes
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 4.0f)
							[
								SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return GetActiveModeText();
									})
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return FText::FromString(LastStatus);
									})
									.ColorAndOpacity_Lambda([this]()
									{
										return FSlateColor(
											bLastActionSucceeded
												? FLinearColor(0.35f, 0.85f, 0.40f)
												: FLinearColor(1.0f, 0.45f, 0.40f));
									})
									.AutoWrapText(true)
							]
					]
			]
	];
}

TSharedRef<SWidget> SGraphicsCVarDebugViewsPanel::BuildViewModeButton(
	const FString& Label,
	const FString& Tooltip,
	const EViewModeIndex ViewMode)
{
	return SNew(SButton)
		.Text(FText::FromString(Label))
		.ToolTipText(FText::FromString(Tooltip))
		.ButtonColorAndOpacity_Lambda([this, ViewMode]()
		{
			return IsViewModeActive(ViewMode)
				? FLinearColor(0.20f, 0.55f, 0.95f)
				: FLinearColor::White;
		})
		.OnClicked_Lambda([this, ViewMode, Label]()
		{
			ApplyViewMode(ViewMode, Label);
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SGraphicsCVarDebugViewsPanel::BuildLumenModeButton(
	const FString& Label,
	const FString& Tooltip,
	const FName LumenMode)
{
	return SNew(SButton)
		.Text(FText::FromString(Label))
		.ToolTipText(FText::FromString(Tooltip))
		.ButtonColorAndOpacity_Lambda([this, LumenMode]()
		{
			return IsLumenModeActive(LumenMode)
				? FLinearColor(0.20f, 0.55f, 0.95f)
				: FLinearColor::White;
		})
		.OnClicked_Lambda([this, LumenMode, Label]()
		{
			ApplyLumenMode(LumenMode, Label);
			return FReply::Handled();
		});
}

void SGraphicsCVarDebugViewsPanel::ApplyViewMode(
	const EViewModeIndex ViewMode,
	const FString& Label)
{
	FEditorViewportClient* ViewportClient = GetActiveLevelViewportClient();
	if (!ViewportClient)
	{
		LastStatus = TEXT("활성 Level Editor Viewport를 찾을 수 없습니다.");
		bLastActionSucceeded = false;
		return;
	}

	ViewportClient->SetViewMode(ViewMode);
	ViewportClient->Invalidate();
	LastStatus = FString::Printf(TEXT("%s View Mode를 적용했습니다."), *Label);
	bLastActionSucceeded = true;
}

void SGraphicsCVarDebugViewsPanel::ApplyLumenMode(
	const FName LumenMode,
	const FString& Label)
{
	FEditorViewportClient* ViewportClient = GetActiveLevelViewportClient();
	if (!ViewportClient)
	{
		LastStatus = TEXT("활성 Level Editor Viewport를 찾을 수 없습니다.");
		bLastActionSucceeded = false;
		return;
	}

	ViewportClient->ChangeLumenVisualizationMode(LumenMode);
	ViewportClient->Invalidate();
	LastStatus = FString::Printf(TEXT("Lumen %s를 적용했습니다."), *Label);
	bLastActionSucceeded = true;
}

bool SGraphicsCVarDebugViewsPanel::IsViewModeActive(
	const EViewModeIndex ViewMode) const
{
	const FEditorViewportClient* ViewportClient = GetActiveLevelViewportClient();
	return ViewportClient && ViewportClient->GetViewMode() == ViewMode;
}

bool SGraphicsCVarDebugViewsPanel::IsLumenModeActive(
	const FName LumenMode) const
{
	const FEditorViewportClient* ViewportClient = GetActiveLevelViewportClient();
	return ViewportClient &&
		ViewportClient->IsLumenVisualizationModeSelected(LumenMode);
}

FText SGraphicsCVarDebugViewsPanel::GetActiveModeText() const
{
	const FEditorViewportClient* ViewportClient = GetActiveLevelViewportClient();
	if (!ViewportClient)
	{
		return FText::FromString(TEXT("Current View: No active Level Editor Viewport"));
	}

	if (ViewportClient->GetViewMode() == VMI_VisualizeLumen)
	{
		return FText::Format(
			FText::FromString(TEXT("Current View: Lumen / {0}")),
			ViewportClient->GetCurrentLumenVisualizationModeDisplayName());
	}

	return FText::FromString(FString::Printf(
		TEXT("Current View: %s"),
		*GetViewModeLabel(ViewportClient->GetViewMode())));
}
