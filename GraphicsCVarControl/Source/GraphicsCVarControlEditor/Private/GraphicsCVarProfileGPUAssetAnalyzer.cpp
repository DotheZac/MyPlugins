#include "GraphicsCVarProfileGPUAssetAnalyzer.h"

#include "AssetRegistry/AssetData.h"
#include "Components/ActorComponent.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"

namespace
{
	FString BoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString ObjectPathOrNone(const UObject* Object)
	{
		return Object ? Object->GetPathName() : TEXT("None");
	}

	void AppendMaterialDetails(
		const UPrimitiveComponent& Primitive,
		TArray<FString>& Details)
	{
		int32 TranslucentCount = 0;
		TArray<FString> MaterialPaths;
		for (int32 Index = 0; Index < Primitive.GetNumMaterials(); ++Index)
		{
			if (UMaterialInterface* Material = Primitive.GetMaterial(Index))
			{
				MaterialPaths.Add(Material->GetPathName());
				if (IsTranslucentBlendMode(Material->GetBlendMode()))
				{
					++TranslucentCount;
				}
			}
		}
		Details.Add(FString::Printf(
			TEXT("Materials: %d assigned, %d translucent"),
			MaterialPaths.Num(),
			TranslucentCount));
		for (const FString& Path : MaterialPaths)
		{
			Details.Add(FString::Printf(TEXT("Material reference: %s"), *Path));
		}
	}

	void AnalyzeComponent(const UActorComponent& Component, TArray<FString>& Details)
	{
		Details.Add(FString::Printf(
			TEXT("Component: %s (%s), active=%s"),
			*Component.GetName(),
			*Component.GetClass()->GetPathName(),
			*BoolText(Component.IsActive())));

		if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(&Component))
		{
			Details.Add(FString::Printf(
				TEXT("Primitive flags: visible=%s, hidden_in_game=%s, cast_shadow=%s, render_custom_depth=%s"),
				*BoolText(Primitive->IsVisible()),
				*BoolText(Primitive->bHiddenInGame),
				*BoolText(Primitive->CastShadow),
				*BoolText(Primitive->bRenderCustomDepth)));
			AppendMaterialDetails(*Primitive, Details);
		}

		if (const UStaticMeshComponent* StaticMeshComponent =
			Cast<UStaticMeshComponent>(&Component))
		{
			Details.Add(FString::Printf(
				TEXT("Static Mesh reference: %s"),
				*ObjectPathOrNone(StaticMeshComponent->GetStaticMesh())));
		}
		else if (const USkeletalMeshComponent* SkeletalMeshComponent =
			Cast<USkeletalMeshComponent>(&Component))
		{
			Details.Add(FString::Printf(
				TEXT("Skeletal Mesh reference: %s"),
				*ObjectPathOrNone(SkeletalMeshComponent->GetSkeletalMeshAsset())));
		}

		if (const ULightComponent* Light = Cast<ULightComponent>(&Component))
		{
			Details.Add(FString::Printf(
				TEXT("Light settings: intensity=%.3f, cast_shadows=%s, affect_world=%s"),
				Light->Intensity,
				*BoolText(Light->CastShadows),
				*BoolText(Light->bAffectsWorld)));
		}

		if (const USceneCaptureComponent* SceneCapture =
			Cast<USceneCaptureComponent>(&Component))
		{
			Details.Add(FString::Printf(
				TEXT("Scene Capture settings: capture_every_frame=%s, capture_on_movement=%s"),
				*BoolText(SceneCapture->bCaptureEveryFrame),
				*BoolText(SceneCapture->bCaptureOnMovement)));
		}

		if (Component.GetClass()->GetName().Contains(TEXT("NiagaraComponent")))
		{
			if (const FObjectPropertyBase* AssetProperty =
				FindFProperty<FObjectPropertyBase>(Component.GetClass(), TEXT("Asset")))
			{
				Details.Add(FString::Printf(
					TEXT("Niagara System reference: %s"),
					*ObjectPathOrNone(AssetProperty->GetObjectPropertyValue_InContainer(&Component))));
			}
		}
	}

	void AppendComponentSummary(
		const TArray<const UActorComponent*>& Components,
		TArray<FString>& Details)
	{
		TMap<FString, int32> Counts;
		for (const UActorComponent* Component : Components)
		{
			if (Component)
			{
				++Counts.FindOrAdd(Component->GetClass()->GetName());
			}
		}
		TArray<FString> CountStrings;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			CountStrings.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
		}
		CountStrings.Sort();
		Details.Add(FString::Printf(
			TEXT("Component count: %d%s%s"),
			Components.Num(),
			CountStrings.IsEmpty() ? TEXT("") : TEXT(" | "),
			CountStrings.IsEmpty() ? TEXT("") : *FString::Join(CountStrings, TEXT(", "))));

		for (const UActorComponent* Component : Components)
		{
			if (Component)
			{
				AnalyzeComponent(*Component, Details);
			}
		}
	}

	FString NiagaraSimTargetText(const ENiagaraSimTarget SimTarget)
	{
		return SimTarget == ENiagaraSimTarget::GPUComputeSim
			? TEXT("GPU Compute")
			: TEXT("CPU");
	}

	FString NiagaraBoundsModeText(const ENiagaraEmitterCalculateBoundMode Mode)
	{
		if (const UEnum* Enum = StaticEnum<ENiagaraEmitterCalculateBoundMode>())
		{
			return Enum->GetDisplayNameTextByValue(static_cast<int64>(Mode)).ToString();
		}
		return FString::FromInt(static_cast<int32>(Mode));
	}

	void AnalyzeNiagaraSystem(
		const UNiagaraSystem& System,
		TArray<FString>& Details)
	{
		const TArray<FNiagaraEmitterHandle>& EmitterHandles = System.GetEmitterHandles();
		int32 EnabledEmitters = 0;
		int32 GPUEmitters = 0;
		int32 CPUEmitters = 0;
		int32 EnabledRenderers = 0;
		Details.Add(FString::Printf(
			TEXT("Niagara System: emitters=%d, fixed_bounds=%s, has_gpu_emitters=%s"),
			EmitterHandles.Num(),
			*BoolText(System.bFixedBounds),
			*BoolText(System.HasAnyGPUEmitters())));

		for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
		{
			const bool bEmitterEnabled = Handle.GetIsEnabled();
			if (bEmitterEnabled)
			{
				++EnabledEmitters;
			}
			const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			if (!EmitterData)
			{
				Details.Add(FString::Printf(
					TEXT("Niagara Emitter: %s, enabled=%s, data=unavailable"),
					*Handle.GetName().ToString(),
					*BoolText(bEmitterEnabled)));
				continue;
			}

			if (EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				++GPUEmitters;
			}
			else
			{
				++CPUEmitters;
			}
			Details.Add(FString::Printf(
				TEXT("Niagara Emitter: %s, enabled=%s, sim=%s, bounds_mode=%s, persistent_ids=%s, renderers=%d, max_gpu_spawn_per_frame=%d, preallocation=%d"),
				*Handle.GetName().ToString(),
				*BoolText(bEmitterEnabled),
				*NiagaraSimTargetText(EmitterData->SimTarget),
				*NiagaraBoundsModeText(EmitterData->CalculateBoundsMode),
				*BoolText(EmitterData->bRequiresPersistentIDs),
				EmitterData->GetRenderers().Num(),
				EmitterData->MaxGPUParticlesSpawnPerFrame,
				EmitterData->PreAllocationCount));

			for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
			{
				if (!Renderer)
				{
					continue;
				}
				if (Renderer->GetIsEnabled())
				{
					++EnabledRenderers;
				}
				TArray<UMaterialInterface*> Materials;
				Renderer->GetUsedMaterials(nullptr, Materials);
				int32 TranslucentMaterials = 0;
				TArray<FString> MaterialPaths;
				for (UMaterialInterface* Material : Materials)
				{
					if (Material)
					{
						MaterialPaths.AddUnique(Material->GetPathName());
						if (IsTranslucentBlendMode(Material->GetBlendMode()))
						{
							++TranslucentMaterials;
						}
					}
				}
				Details.Add(FString::Printf(
					TEXT("Niagara Renderer: emitter=%s, type=%s, enabled=%s, materials=%d, translucent_materials=%d"),
					*Handle.GetName().ToString(),
					*Renderer->GetClass()->GetName(),
					*BoolText(Renderer->GetIsEnabled()),
					MaterialPaths.Num(),
					TranslucentMaterials));
				for (const FString& MaterialPath : MaterialPaths)
				{
					Details.Add(FString::Printf(
						TEXT("Niagara Material reference: %s"),
						*MaterialPath));
				}
			}
		}

		Details.Add(FString::Printf(
			TEXT("Niagara Summary: enabled_emitters=%d/%d, cpu_emitters=%d, gpu_emitters=%d, enabled_renderers=%d"),
			EnabledEmitters,
			EmitterHandles.Num(),
			CPUEmitters,
			GPUEmitters,
			EnabledRenderers));
	}
}

TArray<FString> FGraphicsCVarProfileGPUAssetAnalyzer::AnalyzeAsset(
	const FAssetData& AssetData)
{
	TArray<FString> Details;
	Details.Add(FString::Printf(
		TEXT("Asset class: %s"),
		*AssetData.AssetClassPath.ToString()));

	UObject* Asset = AssetData.GetAsset();
	if (!Asset)
	{
		Details.Add(TEXT("Asset could not be loaded for internal inspection."));
		return Details;
	}

	if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		TArray<const UActorComponent*> Components;
		if (Blueprint->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate)
				{
					Components.Add(Node->ComponentTemplate);
				}
			}
		}
		AppendComponentSummary(Components, Details);
	}
	else if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		Details.Add(FString::Printf(
			TEXT("Static Mesh: LODs=%d, material_slots=%d, Nanite=%s"),
			StaticMesh->GetNumLODs(),
			StaticMesh->GetStaticMaterials().Num(),
			*BoolText(StaticMesh->GetNaniteSettings().bEnabled)));
	}
	else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		Details.Add(FString::Printf(
			TEXT("Skeletal Mesh: LODs=%d, material_slots=%d"),
			SkeletalMesh->GetLODNum(),
			SkeletalMesh->GetMaterials().Num()));
	}
	else if (const UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
	{
		Details.Add(FString::Printf(
			TEXT("Material: blend_mode=%d, translucent=%s"),
			static_cast<int32>(Material->GetBlendMode()),
			*BoolText(IsTranslucentBlendMode(Material->GetBlendMode()))));
	}

	if (const UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(Asset))
	{
		AnalyzeNiagaraSystem(*NiagaraSystem, Details);
	}
	return Details;
}

TArray<FString> FGraphicsCVarProfileGPUAssetAnalyzer::AnalyzeActor(const AActor& Actor)
{
	TArray<FString> Details;
	Details.Add(FString::Printf(TEXT("Actor class: %s"), *Actor.GetClass()->GetPathName()));
	Details.Add(FString::Printf(
		TEXT("Actor flags: hidden_in_game=%s"),
		*BoolText(Actor.IsHidden())));

	TInlineComponentArray<UActorComponent*> ActorComponents;
	Actor.GetComponents(ActorComponents);
	TArray<const UActorComponent*> Components;
	Components.Reserve(ActorComponents.Num());
	for (const UActorComponent* Component : ActorComponents)
	{
		Components.Add(Component);
	}
	AppendComponentSummary(Components, Details);
	return Details;
}
