#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SunZoneManager.generated.h"

class ADirectionalLight;

UCLASS(Blueprintable, BlueprintType)
class SUNZONECONTROLLER_API ASunZoneManager : public AActor
{
	GENERATED_BODY()

public:
	ASunZoneManager();

	virtual void Tick(float DeltaSeconds) override;

	/** Smoothly moves the sun to the rotation represented by NewZone. */
	UFUNCTION(BlueprintCallable, Category = "Sun Zone")
	void SetCurrentZone(int32 NewZone);

	/** Immediately applies the rotation represented by NewZone. */
	UFUNCTION(BlueprintCallable, Category = "Sun Zone")
	void SnapToZone(int32 NewZone);

	/** Immediately previews Current Zone in the editor. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sun Zone")
	void PreviewCurrentZone();

	UFUNCTION(BlueprintPure, Category = "Sun Zone")
	int32 GetCurrentZone() const { return CurrentZone; }

	UFUNCTION(BlueprintPure, Category = "Sun Zone")
	float GetCurrentAlpha() const { return CurrentAlpha; }

	UFUNCTION(BlueprintPure, Category = "Sun Zone")
	float GetTargetAlpha() const { return TargetAlpha; }

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	/** Directional Light whose world rotation represents the sun. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Sun Zone|Target")
	TObjectPtr<ADirectionalLight> SunActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Zone|Rotation")
	FRotator StartRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Zone|Rotation")
	FRotator EndRotation = FRotator(90.0, 0.0, 0.0);

	/** Number used as the denominator of CurrentZone / TotalZoneCount. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun Zone|Zone", meta = (ClampMin = "1", UIMin = "1"))
	int32 TotalZoneCount = 10;

	/** Integer zone index. Zero represents StartRotation and TotalZoneCount represents EndRotation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun Zone|Zone", meta = (ClampMin = "0", UIMin = "0"))
	int32 CurrentZone = 0;

	/** Seconds used to smoothly move from the currently displayed alpha to the next zone alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Zone|Transition", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float TransitionDuration = 1.0f;

private:
	float CalculateZoneAlpha(int32 Zone) const;
	void ApplySunRotation(float Alpha) const;

	float CurrentAlpha = 0.0f;
	float TransitionStartAlpha = 0.0f;
	float TargetAlpha = 0.0f;
	float TransitionElapsed = 0.0f;
};
