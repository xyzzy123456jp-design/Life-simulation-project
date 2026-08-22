#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "JenniferNodSkeletalMeshComponent.generated.h"

UCLASS(ClassGroup=(LifeSimulation), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API UJenniferNodSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	void SetNodPitchDegrees(float InPitchDegrees);
	virtual void FinalizeBoneTransform() override;

private:
	float NodPitchDegrees = 0.0f;
	bool bLoggedMissingHeadBone = false;
};
