#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaserReflectionActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS(Blueprintable)
class LASER_API ALaserReflectionActor : public AActor
{
	GENERATED_BODY()

public:
	ALaserReflectionActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Recalculates the laser path immediately. */
	UFUNCTION(BlueprintCallable, Category = "Laser")
	void UpdateLaserTrace();

	UFUNCTION(BlueprintPure, Category = "Laser")
	FVector GetLaserOrigin() const;

	UFUNCTION(BlueprintPure, Category = "Laser")
	FVector GetLaserDirection() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Laser|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Laser|Components")
	TObjectPtr<UStaticMeshComponent> ConeMesh;

	/** Located at the cone tip and aimed along the actor's local +X axis. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Laser|Components")
	TObjectPtr<USceneComponent> MuzzlePoint;

	/** Maximum number of reflections. The initial straight segment is not a bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxBounces = 3;

	/** Maximum length of the complete laser path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MaxDistance = 2000.0f;

	/** Moves the next trace slightly away from the hit surface to avoid hitting it again immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float SurfaceOffset = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Trace")
	bool bTraceEveryFrame = true;

	/** A hit reflects only when its component or owning actor has this tag. None allows every hit to reflect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Reflection")
	FName ReflectionTag = TEXT("Mirror");

	/** A hit on a component or actor with this tag completes the laser puzzle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Goal")
	FName ClearTag = TEXT("Clear");

	/** True only while the current laser path reaches a Clear-tagged target. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Laser|Goal")
	bool IsClear = false;

	/** Niagara System used once per straight laser segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Niagara")
	TObjectPtr<UNiagaraSystem> LaserNiagaraSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Niagara")
	bool bUseNiagaraLaser = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Niagara")
	FName NiagaraStartParameter = TEXT("User.Start");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Niagara")
	FName NiagaraEndParameter = TEXT("User.End");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Debug")
	bool bDrawDebugLaser = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Debug")
	FLinearColor DebugLaserColor = FLinearColor::Red;

	/** Overall cone height along the actor's local +X direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Emitter", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float ConeLength = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Emitter", meta = (ClampMin = "0.5", UIMin = "0.5", Units = "cm"))
	float ConeRadius = 50.0f;

	/** Additional distance forward from the geometric cone tip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Laser|Emitter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MuzzleForwardOffset = 1.0f;

	/** Ordered laser path: emitter tip, reflection points, and the final endpoint. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Laser|Result")
	TArray<FVector> LaserPoints;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Laser|Result")
	TArray<FHitResult> LaserHits;

	/** Runtime Niagara components, one for each straight laser segment. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Laser|Result")
	TArray<TObjectPtr<UNiagaraComponent>> NiagaraSegments;

private:
	void UpdateEmitterTransform();
	bool HitHasTag(const FHitResult& Hit, FName Tag) const;
	bool IsReflectiveHit(const FHitResult& Hit) const;
	void SetClearState(bool bNewIsClear);
	void UpdateNiagaraSegments();
	UNiagaraComponent* GetOrCreateNiagaraSegment(int32 SegmentIndex);
	void DeactivateNiagaraSegmentsFrom(int32 FirstInactiveIndex);
};
