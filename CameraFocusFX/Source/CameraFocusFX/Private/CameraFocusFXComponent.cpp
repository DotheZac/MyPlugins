#include "CameraFocusFXComponent.h"

#include "Camera/CameraComponent.h"
#include "CameraFocusFXPreset.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"

UCameraFocusFXComponent::UCameraFocusFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCameraFocusFXComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bApplyPresetOnBeginPlay && FocusPreset)
	{
		ApplyPreset(FocusPreset);
	}
	ResolveTargetComponents();
}

void UCameraFocusFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreOriginalSettings();
	Super::EndPlay(EndPlayReason);
}

void UCameraFocusFXComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetCamera || (bUseAutomaticZoom && !TargetSpringArm))
	{
		ResolveTargetComponents();
	}

	if (!TargetCamera || !CanApplyToOwner())
	{
		RestoreOriginalSettings();
		CurrentFocusAlpha = 0.0f;
		return;
	}

	float TargetFocusAlpha = 0.0f;
	if (bEffectEnabled)
	{
		TargetFocusAlpha = bManualFocusAlpha
			? ManualFocusAlpha
			: CalculateAutomaticFocusAlpha();
	}

	CurrentFocusAlpha = EffectInterpSpeed > 0.0f
		? FMath::FInterpTo(CurrentFocusAlpha, TargetFocusAlpha, DeltaTime, EffectInterpSpeed)
		: TargetFocusAlpha;

	if (CurrentFocusAlpha <= KINDA_SMALL_NUMBER && TargetFocusAlpha <= KINDA_SMALL_NUMBER)
	{
		CurrentFocusAlpha = 0.0f;
		RestoreOriginalSettings();
		return;
	}

	ApplyEffects(CurrentFocusAlpha);
}

void UCameraFocusFXComponent::SetEffectEnabled(bool bEnabled)
{
	bEffectEnabled = bEnabled;
}

void UCameraFocusFXComponent::ApplyPreset(UCameraFocusFXPreset* InPreset)
{
	if (!InPreset)
	{
		return;
	}

	RestoreOriginalSettings();
	FocusPreset = InPreset;
	bEffectEnabled = InPreset->bEffectEnabled;
	bUseAutomaticZoom = InPreset->bUseAutomaticZoom;
	EffectStartArmLength = InPreset->EffectStartArmLength;
	FullEffectArmLength = InPreset->FullEffectArmLength;
	EffectInterpSpeed = InPreset->EffectInterpSpeed;
	bEnableVignette = InPreset->bEnableVignette;
	MaxVignetteIntensity = InPreset->MaxVignetteIntensity;
	bEnableDepthOfField = InPreset->bEnableDepthOfField;
	FocusFStop = InPreset->FocusFStop;
	FocusTargetOffset = InPreset->FocusTargetOffset;
	bLocalPlayerOnly = InPreset->bLocalPlayerOnly;
}

void UCameraFocusFXComponent::SetManualFocusAlpha(float InFocusAlpha)
{
	bManualFocusAlpha = true;
	ManualFocusAlpha = FMath::Clamp(InFocusAlpha, 0.0f, 1.0f);
}

void UCameraFocusFXComponent::ClearManualFocusAlpha()
{
	bManualFocusAlpha = false;
}

void UCameraFocusFXComponent::SetTargetComponents(
	UCameraComponent* InCamera,
	USpringArmComponent* InSpringArm)
{
	RestoreOriginalSettings();
	TargetCamera = InCamera;
	TargetSpringArm = InSpringArm;
	bOriginalSettingsCached = false;
}

void UCameraFocusFXComponent::RefreshTargetComponents()
{
	RestoreOriginalSettings();
	TargetCamera = nullptr;
	TargetSpringArm = nullptr;
	bOriginalSettingsCached = false;
	ResolveTargetComponents();
}

void UCameraFocusFXComponent::ResolveTargetComponents()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (!TargetCamera)
	{
		TargetCamera = OwnerActor->FindComponentByClass<UCameraComponent>();
	}

	if (!TargetSpringArm)
	{
		TargetSpringArm = OwnerActor->FindComponentByClass<USpringArmComponent>();
	}

	if (TargetCamera && !bOriginalSettingsCached)
	{
		CacheOriginalSettings();
	}
}

bool UCameraFocusFXComponent::CanApplyToOwner() const
{
	if (!bLocalPlayerOnly)
	{
		return true;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return !OwnerPawn || OwnerPawn->IsLocallyControlled();
}

float UCameraFocusFXComponent::CalculateAutomaticFocusAlpha() const
{
	if (!bUseAutomaticZoom || !TargetSpringArm)
	{
		return 0.0f;
	}

	const float StartLength = FMath::Max(EffectStartArmLength, FullEffectArmLength);
	const float FullLength = FMath::Min(EffectStartArmLength, FullEffectArmLength);
	if (FMath::IsNearlyEqual(StartLength, FullLength))
	{
		return TargetSpringArm->TargetArmLength <= FullLength ? 1.0f : 0.0f;
	}

	return FMath::GetMappedRangeValueClamped(
		FVector2D(StartLength, FullLength),
		FVector2D(0.0f, 1.0f),
		TargetSpringArm->TargetArmLength);
}

void UCameraFocusFXComponent::CacheOriginalSettings()
{
	if (!TargetCamera)
	{
		return;
	}

	const FPostProcessSettings& Settings = TargetCamera->PostProcessSettings;
	bOriginalOverrideVignette = Settings.bOverride_VignetteIntensity;
	OriginalVignetteIntensity = Settings.VignetteIntensity;
	bOriginalOverrideFocalDistance = Settings.bOverride_DepthOfFieldFocalDistance;
	OriginalFocalDistance = Settings.DepthOfFieldFocalDistance;
	bOriginalOverrideFStop = Settings.bOverride_DepthOfFieldFstop;
	OriginalFStop = Settings.DepthOfFieldFstop;
	bOriginalSettingsCached = true;
}

void UCameraFocusFXComponent::ApplyEffects(float FocusAlpha)
{
	if (!TargetCamera)
	{
		return;
	}

	if (!bOriginalSettingsCached)
	{
		CacheOriginalSettings();
	}

	const float SafeFocusAlpha = FMath::Clamp(FocusAlpha, 0.0f, 1.0f);
	FPostProcessSettings& Settings = TargetCamera->PostProcessSettings;

	if (bEnableVignette)
	{
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Lerp(
			OriginalVignetteIntensity,
			MaxVignetteIntensity,
			SafeFocusAlpha);
		bVignetteOverrideApplied = true;
	}

	if (bEnableDepthOfField)
	{
		const FVector FocalPoint = GetOwner()->GetActorTransform().TransformPosition(FocusTargetOffset);
		const float FocalDistance = FVector::Distance(TargetCamera->GetComponentLocation(), FocalPoint);

		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = FocalDistance;
		Settings.bOverride_DepthOfFieldFstop = true;
		Settings.DepthOfFieldFstop = FMath::Lerp(
			OriginalFStop,
			FMath::Clamp(FocusFStop, 0.01f, 32.0f),
			SafeFocusAlpha);
		bDepthOfFieldOverrideApplied = true;
	}

	bOverridesApplied = true;
}

void UCameraFocusFXComponent::RestoreOriginalSettings()
{
	if (!TargetCamera || !bOriginalSettingsCached || !bOverridesApplied)
	{
		return;
	}

	FPostProcessSettings& Settings = TargetCamera->PostProcessSettings;
	if (bVignetteOverrideApplied)
	{
		Settings.bOverride_VignetteIntensity = bOriginalOverrideVignette;
		Settings.VignetteIntensity = OriginalVignetteIntensity;
	}

	if (bDepthOfFieldOverrideApplied)
	{
		Settings.bOverride_DepthOfFieldFocalDistance = bOriginalOverrideFocalDistance;
		Settings.DepthOfFieldFocalDistance = OriginalFocalDistance;
		Settings.bOverride_DepthOfFieldFstop = bOriginalOverrideFStop;
		Settings.DepthOfFieldFstop = OriginalFStop;
	}

	bOverridesApplied = false;
	bVignetteOverrideApplied = false;
	bDepthOfFieldOverrideApplied = false;
}
