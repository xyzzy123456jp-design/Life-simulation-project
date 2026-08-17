#include "PaytonMetaHumanBuildLibrary.h"

#if WITH_EDITOR
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#endif

bool UPaytonMetaHumanBuildLibrary::BuildPaytonCustom2Character()
{
#if WITH_EDITOR
	UMetaHumanCharacter* Character = LoadObject<UMetaHumanCharacter>(
		nullptr,
		TEXT("/Game/MetaHumans/Payton_Custom2.Payton_Custom2"));
	if (!Character)
	{
		UE_LOG(LogTemp, Error, TEXT("PaytonMetaHumanBuild: Payton_Custom2をロードできませんでした"));
		return false;
	}

	UMetaHumanCharacterEditorSubsystem* EditorSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (!EditorSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("PaytonMetaHumanBuild: MetaHumanCharacterEditorSubsystemを取得できませんでした"));
		return false;
	}

	if (!EditorSubsystem->IsObjectAddedForEditing(Character)
		&& !EditorSubsystem->TryAddObjectToEdit(Character))
	{
		UE_LOG(LogTemp, Error, TEXT("PaytonMetaHumanBuild: Payton_Custom2を編集登録できませんでした"));
		return false;
	}

	if (Character->NeedsToDownloadTextureSources())
	{
		UE_LOG(LogTemp, Warning, TEXT("PaytonMetaHumanBuild: 不足している顔テクスチャSourceを取得します"));
		FMetaHumanCharacterTextureRequestParams TextureParams;
		TextureParams.bReportProgress = true;
		TextureParams.bBlocking = true;
		EditorSubsystem->RequestTextureSources(Character, TextureParams);
	}

	if (EditorSubsystem->GetRiggingState(Character) != EMetaHumanCharacterRigState::Rigged)
	{
		UE_LOG(LogTemp, Warning, TEXT("PaytonMetaHumanBuild: 完全な顔リグを生成します"));
		FMetaHumanCharacterAutoRiggingRequestParams RigParams;
		RigParams.RigType = EMetaHumanRigType::JointsAndBlendShapes;
		RigParams.bReportProgress = true;
		RigParams.bBlocking = true;
		EditorSubsystem->RequestAutoRigging(Character, RigParams);
	}

	if (EditorSubsystem->GetRiggingState(Character) != EMetaHumanCharacterRigState::Rigged
		|| Character->NeedsToDownloadTextureSources())
	{
		UE_LOG(LogTemp, Error, TEXT("PaytonMetaHumanBuild: リグまたはテクスチャSourceの準備が完了しませんでした"));
		return false;
	}

	FMetaHumanCharacterEditorBuildParameters Params;
	Params.PipelineType = EMetaHumanDefaultPipelineType::Cinematic;
	Params.PipelineQuality = EMetaHumanQualityLevel::Cinematic;
	Params.AbsoluteBuildPath = TEXT("/Game/MetaHumans");
	Params.NameOverride = TEXT("Payton_Custom2_Character");
	Params.CommonFolderPath = TEXT("/Game/MetaHumans/Common");
	Params.bEnableWardrobeItemValidation = true;

	UE_LOG(LogTemp, Warning, TEXT("PaytonMetaHumanBuild: Payton_Custom2のAssemblyを開始します"));
	// UE5.8の正式なEditor Subsystem経路を使う。
	// Subsystem側でキャラクターに対応するPipelineOverrideを生成してからAssemblyされる。
	EditorSubsystem->BuildMetaHuman(Character, Params);
	return true;
#else
	return false;
#endif
}
