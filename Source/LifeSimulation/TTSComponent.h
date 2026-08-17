#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Http.h"
#include "TTSComponent.generated.h"

class USoundWaveProcedural;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTTSPlaybackStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTTSPlaybackFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTTSFailed, const FString&, ErrorMessage);

/**
 * テキストをOpenAI TTS APIで音声合成し、ディスクに保存せず
 * メモリ上のPCMデータから直接再生する。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API UTTSComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTTSComponent();

	// OpenAIのAPIキー。詳細パネルから設定すること(コード直書き・Git管理下は避ける)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
	FString ApiKey;

	// 使用する声(alloy, echo, fable, onyx, nova, shimmer など)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
	FString Voice = TEXT("coral");

	// 使用するモデル
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
	FString Model = TEXT("tts-1");

	// 再生音量の倍率(声が小さいと感じる場合はここを上げる。1.0が等倍)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
	float PlaybackVolumeMultiplier = 2.0f;

	// 再生開始時に発火
	UPROPERTY(BlueprintAssignable, Category = "TTS")
	FOnTTSPlaybackStarted OnPlaybackStarted;

	// 再生が終わった時に発火(次の録音を始めるトリガーなどに使う)
	UPROPERTY(BlueprintAssignable, Category = "TTS")
	FOnTTSPlaybackFinished OnPlaybackFinished;

	// 失敗時に発火
	UPROPERTY(BlueprintAssignable, Category = "TTS")
	FOnTTSFailed OnTTSFailed;

	// テキストを音声合成してそのまま再生する
	UFUNCTION(BlueprintCallable, Category = "TTS")
	void SpeakText(const FString& Text);

	// 再生中の音声を即座に停止する(ユーザーの割り込み発話を検知した時などに使う)。
	// これで止めた場合、OnPlaybackFinishedは発火しない(呼び出し側が状況を把握しているため)。
	UFUNCTION(BlueprintCallable, Category = "TTS")
	void StopPlayback();

	UFUNCTION(BlueprintCallable, Category = "TTS")
	bool IsPlaying() const;

	// 現在再生中の音量(振幅)を0.0〜1.0で取得する。リップシンクなどに使う。
	// 再生していない時は0を返す。
	UFUNCTION(BlueprintCallable, Category = "TTS")
	float GetCurrentAmplitude() const;

protected:
	virtual void BeginPlay() override;

private:
	void SendSpeechRequest(const FString& Text, bool bIsRetry);
	void OnSpeechResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION()
	void HandleAudioFinished();

	UPROPERTY()
	UAudioComponent* CurrentAudioComponent;

	FTimerHandle PlaybackFinishedTimerHandle;

	// 直近のリクエストがリトライかどうか(失敗時に1回だけ再送するため)
	bool bLastRequestWasRetry = false;
	FString LastRequestedText;

	// 再生中の音量をリップシンク用に一定間隔でサンプリングしたもの(0.0〜1.0)
	TArray<float> AmplitudeEnvelope;
	double PlaybackStartTimeSeconds = 0.0;
	static constexpr float EnvelopeStepSeconds = 0.05f;

	// OpenAI TTSのPCM出力は24kHz固定
	static constexpr int32 TTSSampleRate = 24000;
};
