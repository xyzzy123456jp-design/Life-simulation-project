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
	FString Voice = TEXT("alloy");

	// 使用するモデル
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
	FString Model = TEXT("tts-1");

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

protected:
	virtual void BeginPlay() override;

private:
	void SendSpeechRequest(const FString& Text, bool bIsRetry);
	void OnSpeechResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION()
	void HandleAudioFinished();

	FTimerHandle PlaybackFinishedTimerHandle;

	// 直近のリクエストがリトライかどうか(失敗時に1回だけ再送するため)
	bool bLastRequestWasRetry = false;
	FString LastRequestedText;

	// OpenAI TTSのPCM出力は24kHz固定
	static constexpr int32 TTSSampleRate = 24000;
};
