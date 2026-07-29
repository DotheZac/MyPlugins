#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"

#include "TranslucentWallManager.generated.h"

class UMeshComponent;

UCLASS(Blueprintable)
class TRANSLUCENTWALL_API ATranslucentWallManager : public AActor
{
	GENERATED_BODY()

public:
	ATranslucentWallManager();

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Trace", meta = (ClampMin = "0"))
	int32 PlayerIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Trace")
	TEnumAsByte<ETraceTypeQuery> TraceChannel = TraceTypeQuery5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Trace")
	FName WallComponentTag = TEXT("Wall");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Material")
	FName OpacityParameterName = TEXT("IsOnRay");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Material")
	float OnRayValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Translucent Wall|Material")
	float OffRayValue = 0.0f;

private:
	void SetWallParameter(UMeshComponent& MeshComponent, float Value) const;

	TSet<TWeakObjectPtr<UMeshComponent>> PreviousWalls;
};
