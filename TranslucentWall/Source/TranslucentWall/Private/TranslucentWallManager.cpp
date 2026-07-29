#include "TranslucentWallManager.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

ATranslucentWallManager::ATranslucentWallManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATranslucentWallManager::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, PlayerIndex);
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, PlayerIndex);
	if (!IsValid(PlayerCharacter) || !IsValid(CameraManager))
	{
		return;
	}

	TArray<FHitResult> HitResults;
	const TArray<AActor*> ActorsToIgnore;
	UKismetSystemLibrary::LineTraceMulti(
		this,
		CameraManager->GetCameraLocation(),
		PlayerCharacter->GetActorLocation(),
		TraceChannel,
		bTraceComplex,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		HitResults,
		true
	);

	TSet<TWeakObjectPtr<UMeshComponent>> CurrentWalls;
	for (const FHitResult& HitResult : HitResults)
	{
		UMeshComponent* MeshComponent = Cast<UMeshComponent>(HitResult.GetComponent());
		if (IsValid(MeshComponent) && MeshComponent->ComponentHasTag(WallComponentTag))
		{
			CurrentWalls.Add(MeshComponent);
		}
	}

	for (const TWeakObjectPtr<UMeshComponent>& PreviousWall : PreviousWalls)
	{
		if (UMeshComponent* MeshComponent = PreviousWall.Get();
			IsValid(MeshComponent) && !CurrentWalls.Contains(PreviousWall))
		{
			SetWallParameter(*MeshComponent, OffRayValue);
		}
	}

	for (const TWeakObjectPtr<UMeshComponent>& CurrentWall : CurrentWalls)
	{
		if (UMeshComponent* MeshComponent = CurrentWall.Get();
			IsValid(MeshComponent) && !PreviousWalls.Contains(CurrentWall))
		{
			SetWallParameter(*MeshComponent, OnRayValue);
		}
	}

	PreviousWalls = MoveTemp(CurrentWalls);
}

void ATranslucentWallManager::SetWallParameter(UMeshComponent& MeshComponent, const float Value) const
{
	MeshComponent.SetScalarParameterValueOnMaterials(OpacityParameterName, Value);
}
