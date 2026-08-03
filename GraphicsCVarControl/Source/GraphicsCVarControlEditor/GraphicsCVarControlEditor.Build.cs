using UnrealBuildTool;

public class GraphicsCVarControlEditor : ModuleRules
{
	public GraphicsCVarControlEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"LevelEditor",
				"Niagara",
				"Projects",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
