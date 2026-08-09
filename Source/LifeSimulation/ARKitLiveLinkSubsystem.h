#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ARKitLiveLinkSubsystem.generated.h"

class FARKitLiveLinkSource;

/**
 * FARKitLiveLinkSourceをゲーム開始時に1つだけ生成し、LiveLinkクライアントに
 * 登録しておくサブシステム。LipSyncComponentなど、顔を動かしたい側は
 * GetGameInstance()->GetSubsystem<UARKitLiveLinkSubsystem>()経由でこれを取得し、
 * PushJawOpen()を呼ぶだけでよい。
 */
UCLASS()
class LIFESIMULATION_API UARKitLiveLinkSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// JawOpenカーブの値(0.0〜1.0)を更新して配信する
	void PushJawOpen(float Value);

	// このソースが使うLiveLinkサブジェクト名。
	// BP_PaytonのARKit Face Subjにこの名前を設定すること
	static FName GetSubjectName() { return TEXT("LifeSimARKitFace"); }

private:
	TSharedPtr<FARKitLiveLinkSource> Source;
	FGuid SourceGuid;
};
