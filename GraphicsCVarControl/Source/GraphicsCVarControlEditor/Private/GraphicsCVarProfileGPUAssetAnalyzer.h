#pragma once

#include "CoreMinimal.h"

struct FAssetData;
class AActor;

class FGraphicsCVarProfileGPUAssetAnalyzer
{
public:
	static TArray<FString> AnalyzeAsset(const FAssetData& AssetData);
	static TArray<FString> AnalyzeActor(const AActor& Actor);
};
