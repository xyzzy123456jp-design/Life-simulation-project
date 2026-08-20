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

	// AIまたは手動テストから指定された表情のTargetだけを更新する。
	// CurrentはTickExpressionで滑らかにTargetへ補間する。
	bool SetExpressionTarget(const FString& Emotion, float Intensity);
	void TickExpression(float DeltaTime);

	// Phase 1のカーブ単体確認用。既知の表情カーブだけをTargetとして設定する。
	bool SetManualCurveTarget(FName CurveName, float Value);
	bool IsKnownExpression(const FString& Emotion) const;

	// このソースが使うLiveLinkサブジェクト名。
	// BP_PaytonのARKit Face Subjにこの名前を設定すること
	static FName GetSubjectName() { return TEXT("LifeSimARKitFace"); }

private:
	TSharedPtr<FARKitLiveLinkSource> Source;
	FGuid SourceGuid;

	TMap<FName, float> ExpressionCurrentValues;
	TMap<FName, float> ExpressionTargetValues;
	FString CurrentExpression = TEXT("neutral");
	bool bExpressionConvergenceLogged = true;

	// Blinkはemotionとは独立した自律レイヤーとして管理する。
	float BlinkTimeUntilNext = 2.5f;
	float BlinkElapsed = 0.0f;
	bool bBlinkInProgress = false;

	void InitializeExpressionState(bool bResetCurrent);
	void TickBlink(float DeltaTime);
};
