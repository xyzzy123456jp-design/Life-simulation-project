#include "JenniferNodSkeletalMeshComponent.h"

void UJenniferNodSkeletalMeshComponent::SetNodPitchDegrees(float InPitchDegrees)
{
	NodPitchDegrees = FMath::Clamp(InPitchDegrees, -15.0f, 15.0f);
	RefreshBoneTransforms();
}

void UJenniferNodSkeletalMeshComponent::FinalizeBoneTransform()
{
	// Super::FinalizeBoneTransform()はEditable SpaceBasesを描画用Read Bufferへ
	// Flipする。必ずその直前へ差分を入れ、GPUへ届く最終姿勢へ反映する。
	const int32 HeadBoneIndex = GetBoneIndex(TEXT("head"));
	TArray<FTransform>& ComponentTransforms = GetEditableComponentSpaceTransforms();
	if (HeadBoneIndex == INDEX_NONE || !ComponentTransforms.IsValidIndex(HeadBoneIndex))
	{
		if (!bLoggedMissingHeadBone)
		{
			bLoggedMissingHeadBone = true;
			UE_LOG(LogTemp, Error, TEXT("[NOD] head bone unavailable; nod disabled mesh=%s"),
				GetSkinnedAsset() ? *GetSkinnedAsset()->GetPathName() : TEXT("None"));
		}
		Super::FinalizeBoneTransform();
		return;
	}

	bLoggedMissingHeadBone = false;
	if (!FMath::IsNearlyZero(NodPitchDegrees, 0.001f))
	{
		FTransform& HeadTransform = ComponentTransforms[HeadBoneIndex];
		const FQuat AdditivePitch(FVector::XAxisVector, FMath::DegreesToRadians(NodPitchDegrees));
		HeadTransform.SetRotation((AdditivePitch * HeadTransform.GetRotation()).GetNormalized());
	}

	Super::FinalizeBoneTransform();
}
