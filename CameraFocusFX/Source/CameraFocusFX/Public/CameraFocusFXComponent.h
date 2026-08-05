#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraFocusFXComponent.generated.h"

class UCameraComponent;
class UCameraFocusFXPreset;
class USpringArmComponent;

/**
 * Applies a local focus effect as a SpringArm camera zooms in.
 * Add this component to a camera-owning Pawn or Actor; no manager Actor is required.
 */
UCLASS(ClassGroup = (Camera), meta = (BlueprintSpawnableComponent, DisplayName = "Camera Focus FX"))
class CAMERAFOCUSFX_API UCameraFocusFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCameraFocusFXComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Enables or fades out the effect. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX")
	void SetEffectEnabled(bool bEnabled);

	/** Assigns and immediately copies settings from a reusable Data Asset preset. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX|Preset")
	void ApplyPreset(UCameraFocusFXPreset* InPreset);

	/** Overrides automatic zoom with a normalized effect strength. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX")
	void SetManualFocusAlpha(float InFocusAlpha);

	/** Returns control to automatic SpringArm zoom evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX")
	void ClearManualFocusAlpha();

	/** Explicitly assigns components. Passing null allows automatic resolution on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX")
	void SetTargetComponents(UCameraComponent* InCamera, USpringArmComponent* InSpringArm);

	/** Re-runs automatic component discovery on the owner. */
	UFUNCTION(BlueprintCallable, Category = "Camera Focus FX")
	void RefreshTargetComponents();

	UFUNCTION(BlueprintPure, Category = "Camera Focus FX")
	float GetCurrentFocusAlpha() const { return CurrentFocusAlpha; }

protected:
	/** Optional reusable Data Asset applied when play begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Focus FX|Preset", meta = (ToolTip = "이 컴포넌트에 적용할 CameraFocusFXPreset Data Asset입니다."))
	TObjectPtr<UCameraFocusFXPreset> FocusPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Preset", meta = (ToolTip = "활성화하면 BeginPlay에서 지정된 Focus Preset의 값을 자동 적용합니다."))
	bool bApplyPresetOnBeginPlay = true;

	/** Automatically derives effect strength from the resolved SpringArm length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Zoom", meta = (ToolTip = "SpringArm 길이를 기준으로 효과 강도를 자동 계산합니다."))
	bool bUseAutomaticZoom = true;

	/** At or above this arm length the effect is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Zoom", meta = (ClampMin = "0.0", ToolTip = "효과가 시작되는 SpringArm 길이입니다. 이 길이 이상에서는 효과 강도가 0입니다."))
	float EffectStartArmLength = 1200.0f;

	/** At or below this arm length the effect reaches full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Zoom", meta = (ClampMin = "0.0", ToolTip = "효과가 최대가 되는 SpringArm 길이입니다. 이 길이 이하에서는 효과 강도가 1입니다."))
	float FullEffectArmLength = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Blend", meta = (ClampMin = "0.0", ToolTip = "효과 강도가 목표값으로 전환되는 보간 속도입니다. 0이면 즉시 전환됩니다."))
	float EffectInterpSpeed = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Vignette", meta = (ToolTip = "화면 가장자리를 어둡게 만드는 비네트 효과를 사용합니다."))
	bool bEnableVignette = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "효과가 최대일 때 적용할 비네트 강도입니다."))
	float MaxVignetteIntensity = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Depth Of Field", meta = (ToolTip = "캐릭터에 초점을 맞추고 배경을 흐리게 만드는 Depth of Field 효과를 사용합니다."))
	bool bEnableDepthOfField = true;

	/** F-stop used at full effect. Lower values produce stronger background blur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Depth Of Field", meta = (ClampMin = "0.01", ClampMax = "32.0", UIMin = "0.01", UIMax = "32.0", ToolTip = "효과가 최대일 때 사용할 조리개 값입니다. 값이 낮을수록 배경 블러가 강해집니다."))
	float FocusFStop = 1.4f;

	/** Local-space offset from the owner used as the automatic focal point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX|Depth Of Field", meta = (ToolTip = "소유 Actor의 로컬 좌표를 기준으로 초점을 맞출 위치 오프셋입니다."))
	FVector FocusTargetOffset = FVector(0.0f, 0.0f, 100.0f);

	/** Prevents remote Pawns from modifying a local player's camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Focus FX", meta = (ToolTip = "활성화하면 로컬 플레이어가 조작하는 Pawn의 카메라에만 효과를 적용합니다."))
	bool bLocalPlayerOnly = true;

private:
	void ResolveTargetComponents();
	bool CanApplyToOwner() const;
	float CalculateAutomaticFocusAlpha() const;
	void CacheOriginalSettings();
	void ApplyEffects(float FocusAlpha);
	void RestoreOriginalSettings();

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> TargetCamera;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> TargetSpringArm;

	bool bEffectEnabled = true;
	bool bManualFocusAlpha = false;
	bool bOriginalSettingsCached = false;
	bool bOverridesApplied = false;
	bool bVignetteOverrideApplied = false;
	bool bDepthOfFieldOverrideApplied = false;
	float ManualFocusAlpha = 0.0f;
	float CurrentFocusAlpha = 0.0f;

	bool bOriginalOverrideVignette = false;
	float OriginalVignetteIntensity = 0.0f;
	bool bOriginalOverrideFocalDistance = false;
	float OriginalFocalDistance = 0.0f;
	bool bOriginalOverrideFStop = false;
	float OriginalFStop = 0.0f;
};
