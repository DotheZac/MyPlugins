#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CameraFocusFXPreset.generated.h"

/** Reusable settings for UCameraFocusFXComponent. */
UCLASS(BlueprintType)
class CAMERAFOCUSFX_API UCameraFocusFXPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ToolTip = "이 프리셋의 카메라 집중 효과를 활성화합니다."))
	bool bEffectEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zoom", meta = (ToolTip = "SpringArm 길이를 기준으로 효과 강도를 자동 계산합니다."))
	bool bUseAutomaticZoom = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zoom", meta = (ClampMin = "0.0", ToolTip = "효과가 시작되는 SpringArm 길이입니다. 이 길이 이상에서는 효과 강도가 0입니다."))
	float EffectStartArmLength = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zoom", meta = (ClampMin = "0.0", ToolTip = "효과가 최대가 되는 SpringArm 길이입니다. 이 길이 이하에서는 효과 강도가 1입니다."))
	float FullEffectArmLength = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (ClampMin = "0.0", ToolTip = "효과 강도가 목표값으로 전환되는 보간 속도입니다. 0이면 즉시 전환됩니다."))
	float EffectInterpSpeed = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vignette", meta = (ToolTip = "화면 가장자리를 어둡게 만드는 비네트 효과를 사용합니다."))
	bool bEnableVignette = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "효과가 최대일 때 적용할 비네트 강도입니다."))
	float MaxVignetteIntensity = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depth Of Field", meta = (ToolTip = "캐릭터에 초점을 맞추고 배경을 흐리게 만드는 Depth of Field 효과를 사용합니다."))
	bool bEnableDepthOfField = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depth Of Field", meta = (ClampMin = "0.01", ClampMax = "32.0", UIMin = "0.01", UIMax = "32.0", ToolTip = "효과가 최대일 때 사용할 조리개 값입니다. 값이 낮을수록 배경 블러가 강해집니다."))
	float FocusFStop = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depth Of Field", meta = (ToolTip = "소유 Actor의 로컬 좌표를 기준으로 초점을 맞출 위치 오프셋입니다."))
	FVector FocusTargetOffset = FVector(0.0f, 0.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ToolTip = "활성화하면 로컬 플레이어가 조작하는 Pawn의 카메라에만 효과를 적용합니다."))
	bool bLocalPlayerOnly = true;
};
