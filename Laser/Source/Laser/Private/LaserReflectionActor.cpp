#include "LaserReflectionActor.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

ALaserReflectionActor::ALaserReflectionActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ConeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConeMesh"));
	ConeMesh->SetupAttachment(SceneRoot);
	ConeMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	ConeMesh->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMeshFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMeshFinder.Succeeded())
	{
		ConeMesh->SetStaticMesh(ConeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LaserSystemFinder(TEXT("/Laser/Laser.Laser"));
	if (LaserSystemFinder.Succeeded())
	{
		LaserNiagaraSystem = LaserSystemFinder.Object;
	}

	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(SceneRoot);

	UpdateEmitterTransform();
}

void ALaserReflectionActor::BeginPlay()
{
	Super::BeginPlay();
	IsClear = false;
}

void ALaserReflectionActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bTraceEveryFrame)
	{
		UpdateLaserTrace();
	}
}

void ALaserReflectionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateEmitterTransform();

	if (!bTraceEveryFrame)
	{
		UpdateLaserTrace();
	}
}

void ALaserReflectionActor::UpdateEmitterTransform()
{
	if (ConeMesh)
	{
		// Engine/BasicShapes/Cone is 100 cm high on local +Z with a 50 cm base radius.
		// Pitch -90 aligns its tip with the actor's local +X direction.
		ConeMesh->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		ConeMesh->SetRelativeScale3D(FVector(ConeRadius / 50.0f, ConeRadius / 50.0f, ConeLength / 100.0f));
	}

	if (MuzzlePoint)
	{
		MuzzlePoint->SetRelativeLocation(FVector((ConeLength * 0.5f) + MuzzleForwardOffset, 0.0f, 0.0f));
		MuzzlePoint->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

FVector ALaserReflectionActor::GetLaserOrigin() const
{
	return MuzzlePoint ? MuzzlePoint->GetComponentLocation() : GetActorLocation();
}

FVector ALaserReflectionActor::GetLaserDirection() const
{
	return MuzzlePoint ? MuzzlePoint->GetForwardVector() : GetActorForwardVector();
}

bool ALaserReflectionActor::IsReflectiveHit(const FHitResult& Hit) const
{
	if (ReflectionTag.IsNone())
	{
		return true;
	}

	return HitHasTag(Hit, ReflectionTag);
}

bool ALaserReflectionActor::HitHasTag(const FHitResult& Hit, const FName Tag) const
{
	if (Tag.IsNone())
	{
		return false;
	}

	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (IsValid(HitComponent) && HitComponent->ComponentHasTag(Tag))
	{
		return true;
	}

	const AActor* HitActor = Hit.GetActor();
	return IsValid(HitActor) && HitActor->ActorHasTag(Tag);
}

void ALaserReflectionActor::SetClearState(const bool bNewIsClear)
{
	if (IsClear == bNewIsClear)
	{
		return;
	}

	IsClear = bNewIsClear;
}

void ALaserReflectionActor::UpdateLaserTrace()
{
	LaserPoints.Reset();
	LaserHits.Reset();

	UWorld* World = GetWorld();
	if (!World || MaxDistance <= UE_KINDA_SMALL_NUMBER)
	{
		SetClearState(false);
		DeactivateNiagaraSegmentsFrom(0);
		return;
	}

	FVector CurrentStart = GetLaserOrigin();
	FVector CurrentDirection = GetLaserDirection().GetSafeNormal();
	float RemainingDistance = MaxDistance;
	bool bReachedClearTarget = false;

	LaserPoints.Add(CurrentStart);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LaserReflectionTrace), bTraceComplex, this);
	QueryParams.AddIgnoredActor(this);

	const int32 ClampedMaxBounces = FMath::Max(0, MaxBounces);

	// Trace index 0 is the initial segment. Each subsequent index is one reflection.
	for (int32 TraceIndex = 0; TraceIndex <= ClampedMaxBounces; ++TraceIndex)
	{
		const FVector TraceEnd = CurrentStart + (CurrentDirection * RemainingDistance);
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit,
			CurrentStart,
			TraceEnd,
			TraceChannel,
			QueryParams
		);

		const FVector SegmentEnd = bHit ? Hit.ImpactPoint : TraceEnd;
		LaserPoints.Add(SegmentEnd);

		if (bDrawDebugLaser)
		{
			DrawDebugLine(World, CurrentStart, SegmentEnd, DebugLaserColor.ToFColor(true), false, 0.0f, 0, 2.0f);
		}

		if (!bHit)
		{
			break;
		}

		LaserHits.Add(Hit);
		bReachedClearTarget |= HitHasTag(Hit, ClearTag);
		RemainingDistance = FMath::Max(0.0f, RemainingDistance - Hit.Distance);

		if (!IsReflectiveHit(Hit))
		{
			break;
		}

		if (TraceIndex >= ClampedMaxBounces || RemainingDistance <= UE_KINDA_SMALL_NUMBER)
		{
			break;
		}

		CurrentDirection = FMath::GetReflectionVector(CurrentDirection, Hit.ImpactNormal).GetSafeNormal();
		CurrentStart = Hit.ImpactPoint + (CurrentDirection * SurfaceOffset);
		RemainingDistance = FMath::Max(0.0f, RemainingDistance - SurfaceOffset);
	}

	SetClearState(bReachedClearTarget);
	UpdateNiagaraSegments();
}

UNiagaraComponent* ALaserReflectionActor::GetOrCreateNiagaraSegment(const int32 SegmentIndex)
{
	if (NiagaraSegments.IsValidIndex(SegmentIndex) && IsValid(NiagaraSegments[SegmentIndex]))
	{
		return NiagaraSegments[SegmentIndex];
	}

	UNiagaraComponent* SegmentComponent = NewObject<UNiagaraComponent>(this, NAME_None, RF_Transient);
	if (!SegmentComponent)
	{
		return nullptr;
	}

	SegmentComponent->SetupAttachment(SceneRoot);
	SegmentComponent->SetAutoActivate(false);
	SegmentComponent->SetAsset(LaserNiagaraSystem);
	AddInstanceComponent(SegmentComponent);
	SegmentComponent->RegisterComponent();

	if (NiagaraSegments.IsValidIndex(SegmentIndex))
	{
		NiagaraSegments[SegmentIndex] = SegmentComponent;
	}
	else
	{
		NiagaraSegments.SetNum(SegmentIndex + 1);
		NiagaraSegments[SegmentIndex] = SegmentComponent;
	}

	return SegmentComponent;
}

void ALaserReflectionActor::UpdateNiagaraSegments()
{
	if (!bUseNiagaraLaser || !LaserNiagaraSystem)
	{
		DeactivateNiagaraSegmentsFrom(0);
		return;
	}

	const int32 RequiredSegmentCount = FMath::Max(0, LaserPoints.Num() - 1);
	for (int32 SegmentIndex = 0; SegmentIndex < RequiredSegmentCount; ++SegmentIndex)
	{
		UNiagaraComponent* SegmentComponent = GetOrCreateNiagaraSegment(SegmentIndex);
		if (!SegmentComponent)
		{
			continue;
		}

		if (SegmentComponent->GetAsset() != LaserNiagaraSystem)
		{
			SegmentComponent->SetAsset(LaserNiagaraSystem);
			SegmentComponent->ReinitializeSystem();
		}

		SegmentComponent->SetVariablePosition(NiagaraStartParameter, LaserPoints[SegmentIndex]);
		SegmentComponent->SetVariablePosition(NiagaraEndParameter, LaserPoints[SegmentIndex + 1]);

		if (!SegmentComponent->IsActive())
		{
			SegmentComponent->Activate(true);
		}
	}

	DeactivateNiagaraSegmentsFrom(RequiredSegmentCount);
}

void ALaserReflectionActor::DeactivateNiagaraSegmentsFrom(const int32 FirstInactiveIndex)
{
	for (int32 SegmentIndex = FMath::Max(0, FirstInactiveIndex); SegmentIndex < NiagaraSegments.Num(); ++SegmentIndex)
	{
		if (IsValid(NiagaraSegments[SegmentIndex]) && NiagaraSegments[SegmentIndex]->IsActive())
		{
			NiagaraSegments[SegmentIndex]->DeactivateImmediate();
		}
	}
}
