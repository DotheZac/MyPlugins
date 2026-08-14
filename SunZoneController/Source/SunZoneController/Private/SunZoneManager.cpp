#include "SunZoneManager.h"

#include "Engine/DirectionalLight.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

ASunZoneManager::ASunZoneManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ASunZoneManager::BeginPlay()
{
	Super::BeginPlay();

	TotalZoneCount = FMath::Max(1, TotalZoneCount);
	CurrentZone = FMath::Clamp(CurrentZone, 0, TotalZoneCount);
	CurrentAlpha = CalculateZoneAlpha(CurrentZone);
	TransitionStartAlpha = CurrentAlpha;
	TargetAlpha = CurrentAlpha;
	TransitionElapsed = 0.0f;

	ApplySunRotation(CurrentAlpha);
	SetActorTickEnabled(false);
}

#if WITH_EDITOR
void ASunZoneManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedPropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASunZoneManager, CurrentZone))
	{
		SetCurrentZone(CurrentZone);
	}
	else if (
		ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASunZoneManager, TotalZoneCount)
		|| ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASunZoneManager, SunActor)
		|| ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASunZoneManager, StartRotation)
		|| ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ASunZoneManager, EndRotation)
	)
	{
		SnapToZone(CurrentZone);
	}
}

bool ASunZoneManager::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

void ASunZoneManager::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TransitionDuration <= 0.0f || FMath::IsNearlyEqual(CurrentAlpha, TargetAlpha))
	{
		CurrentAlpha = TargetAlpha;
		ApplySunRotation(CurrentAlpha);
		SetActorTickEnabled(false);
		return;
	}

	TransitionElapsed += DeltaSeconds;
	const float LinearProgress = FMath::Clamp(TransitionElapsed / TransitionDuration, 0.0f, 1.0f);
	const float SmoothProgress = FMath::SmoothStep(0.0f, 1.0f, LinearProgress);
	CurrentAlpha = FMath::Lerp(TransitionStartAlpha, TargetAlpha, SmoothProgress);
	ApplySunRotation(CurrentAlpha);

	if (LinearProgress >= 1.0f)
	{
		CurrentAlpha = TargetAlpha;
		SetActorTickEnabled(false);
	}
}

void ASunZoneManager::SetCurrentZone(const int32 NewZone)
{
	TotalZoneCount = FMath::Max(1, TotalZoneCount);
	CurrentZone = FMath::Clamp(NewZone, 0, TotalZoneCount);
	TransitionStartAlpha = CurrentAlpha;
	TargetAlpha = CalculateZoneAlpha(CurrentZone);
	TransitionElapsed = 0.0f;

	if (TransitionDuration <= 0.0f || FMath::IsNearlyEqual(CurrentAlpha, TargetAlpha))
	{
		CurrentAlpha = TargetAlpha;
		ApplySunRotation(CurrentAlpha);
		SetActorTickEnabled(false);
		return;
	}

	SetActorTickEnabled(true);
}

void ASunZoneManager::SnapToZone(const int32 NewZone)
{
	TotalZoneCount = FMath::Max(1, TotalZoneCount);
	CurrentZone = FMath::Clamp(NewZone, 0, TotalZoneCount);
	CurrentAlpha = CalculateZoneAlpha(CurrentZone);
	TransitionStartAlpha = CurrentAlpha;
	TargetAlpha = CurrentAlpha;
	TransitionElapsed = 0.0f;

	ApplySunRotation(CurrentAlpha);
	SetActorTickEnabled(false);
}

void ASunZoneManager::PreviewCurrentZone()
{
	SnapToZone(CurrentZone);
}

float ASunZoneManager::CalculateZoneAlpha(const int32 Zone) const
{
	const int32 SafeZoneCount = FMath::Max(1, TotalZoneCount);
	const int32 SafeZone = FMath::Clamp(Zone, 0, SafeZoneCount);
	return static_cast<float>(SafeZone) / static_cast<float>(SafeZoneCount);
}

void ASunZoneManager::ApplySunRotation(const float Alpha) const
{
	if (!IsValid(SunActor))
	{
		return;
	}

	const FQuat StartQuaternion = StartRotation.Quaternion();
	const FQuat EndQuaternion = EndRotation.Quaternion();
	const FQuat SunQuaternion = FQuat::Slerp(StartQuaternion, EndQuaternion, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetNormalized();
	SunActor->SetActorRotation(SunQuaternion, ETeleportType::None);
}
