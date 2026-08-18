#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LipSyncComponent.generated.h"

class URealtimeVoiceComponent;
class UTTSComponent;

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

	// 現在のJawOpen値(0.0〜1.0)。ABP_MH_LiveLinkのSet Controlを直接呼ぶ用途などに使う
	float GetCurrentJawOpen() const { return CurrentMorphValue; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float CurrentMorphValue = 0.0f;
};
