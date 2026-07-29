#include "Modules/ModuleManager.h"

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
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

static const FName GraphicsCVarControlTabName(TEXT("GraphicsCVarControl"));

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
	SLATE_BEGIN_ARGS(SGraphicsCVarControlPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		(void)InArgs;

		TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
		FString LastCategory;

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

private:
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

		for (const FCVarOption& Option : Control.Options)
		{
			const FString Value(Option.Value);
			OptionsBox->AddSlot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(Option.Label))
					.OnClicked_Lambda([CVarName, Value]()
					{
						SetCVarValue(CVarName, Value);
						return FReply::Handled();
					})
				];
		}

		return SNew(SBorder)
			.Padding(8.0f)
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

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGraphicsCVarControlEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphicsCVarControlTabName);
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
	}

	static void OpenControlTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(GraphicsCVarControlTabName);
	}
};

IMPLEMENT_MODULE(FGraphicsCVarControlEditorModule, GraphicsCVarControlEditor)
