#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LipSyncComponent.generated.h"

class URealtimeVoiceComponent;
class UTTSComponent;
class USkeletalMeshComponent;

/**
 * 音量駆動の簡易リップシンク。2種類の音声ソースのどちらかを設定して使う。
 * - VoiceSource: RealtimeVoiceComponent(新しいRealtime API構成)
 * - TTSSource: TTSComponent(旧Whisper/ChatManager/TTS構成)
 * 両方設定された場合はVoiceSourceを優先する。
 *
 * MetaHumanの顔はAnimBP(RigLogic)駆動のため、SkeletalMeshComponentへの
 * 直接のSetMorphTargetは効果がない。そのため、UARKitLiveLinkSubsystem経由で
 * JawOpenカーブをLiveLinkとして配信し、Face_AnimBPの"Use ARKit Face"入力に
 * 反映させる方式を取る。
 *
 * 前提条件(エディタ側の設定):
 * - MetaHumanのBlueprint(BP_Paytonなど)で Use ARKit Face を true にする
 * - ARKit Face Subj に "LifeSimARKitFace" を設定する
 *   (UARKitLiveLinkSubsystem::GetSubjectName()と同じ名前)
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API ULipSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULipSyncComponent();

	// 音量(振幅)の取得元(新Realtime構成)。RealtimeVoiceComponentを設定する
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	URealtimeVoiceComponent* VoiceSource;

	// 音量(振幅)の取得元(旧TTS構成)。TTSComponentを設定する。
	// VoiceSourceが設定されている場合はそちらが優先される
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	UTTSComponent* TTSSource;

	// 互換性のために残しているが、LiveLink方式では未使用
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	AActor* CharacterActor;

	// VoiceSourceの振幅(0.0〜1.0)にこの係数を掛けてJawOpenの値にする。
	// 口の開き方が小さい/大きい場合に調整する
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	float AmplitudeToMorphScale = 1.0f;

	// 値の変化を滑らかにする補間速度(大きいほど素早く追従する)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	float InterpSpeed = 15.0f;

	// デバッグ用に、現在の振幅とJawOpen値を画面に表示するか
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LipSync")
	bool bShowDebugMessage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.1"))
	float BlinkIntervalMin = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.1"))
	float BlinkIntervalMax = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.01"))
	float BlinkCloseTimeMin = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.01"))
	float BlinkCloseTimeMax = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.0"))
	float BlinkHoldTimeMin = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.0"))
	float BlinkHoldTimeMax = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.01"))
	float BlinkOpenTimeMin = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink", meta = (ClampMin = "0.01"))
	float BlinkOpenTimeMax = 0.15f;

	// 現在のJawOpen値(0.0〜1.0)。ABP_MH_LiveLinkのSet Controlを直接呼ぶ用途などに使う
	float GetCurrentJawOpen() const { return CurrentMorphValue; }

	// 現在画面に表示しているDirect Morph顔を表情適用先として登録する。
	void SetExpressionFaceMesh(USkeletalMeshComponent* InFaceMesh);
	bool CanApplyExpressions() const;
	bool IsKnownExpression(const FString& Emotion) const;
	bool SetExpressionTarget(const FString& Emotion, float Intensity);
	bool SetManualExpressionCurveTarget(FName MorphName, float Value);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float CurrentMorphValue = 0.0f;

	UPROPERTY(Transient)
	USkeletalMeshComponent* ExpressionFaceMesh = nullptr;

	TMap<FName, float> ExpressionCurrentValues;
	TMap<FName, float> ExpressionTargetValues;
	FString CurrentExpression = TEXT("neutral");
	bool bExpressionBackendAvailable = false;
	bool bExpressionConverged = true;
	bool bExpressionConvergenceLogged = true;
	bool bLoggedMissingExpressionMorphs = false;
	bool bLoggedMissingBlinkMorphs = false;

	enum class EBlinkPhase : uint8
	{
		Waiting,
		Closing,
		Closed,
		Opening
	};

	bool bBlinkAvailable = false;
	EBlinkPhase BlinkPhase = EBlinkPhase::Waiting;
	float BlinkPhaseElapsed = 0.0f;
	float BlinkTimeUntilNext = 0.0f;
	float CurrentBlinkCloseTime = 0.10f;
	float CurrentBlinkHoldTime = 0.05f;
	float CurrentBlinkOpenTime = 0.12f;

	void TickBlink(float DeltaTime);
	void ScheduleNextBlink();
	void TickExpression(float DeltaTime);
};
