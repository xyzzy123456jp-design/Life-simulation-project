#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Http.h"
#include "WhisperTranscriberComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperTranscriptionComplete, const FString&, TranscribedText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperTranscriptionFailed, const FString&, ErrorMessage);

class UMicRecorderComponent;

/**
 * MicRecorderComponentが録音したPCMデータを、メモリ上でWAV形式にラップし、
 * ディスクに保存することなく直接OpenAI Whisper APIへ送信して文字起こしを行う。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API UWhisperTranscriberComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWhisperTranscriberComponent();

	// OpenAIのAPIキー。詳細パネルから設定するか、コードから直接セットする。
	// ハードコードしてGit管理下に入れないよう注意すること。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	FString ApiKey;

	// /v1/audio/transcriptionsへ渡すSTTモデル。A/B比較時に切り替える。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	FString Model = TEXT("gpt-4o-mini-transcribe");

	// 認識対象言語を明示したい場合に指定(例: "ja")。空欄ならWhisperが自動判定する。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	FString Language;

	// 文字起こし成功時に発火
	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnWhisperTranscriptionComplete OnTranscriptionComplete;

	// 文字起こし失敗時に発火
	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnWhisperTranscriptionFailed OnTranscriptionFailed;

	// MicRecorderComponentが保持する録音済みPCMデータを、メモリ上でWAV化して直接送信する
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void TranscribeFromMicRecorder(UMicRecorderComponent* MicRecorder);

private:
	void SendWavBytesToWhisper(const TArray<uint8>& WavBytes, bool bIsRetry);
	void OnWhisperResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	FString Boundary;

	// 429(レート制限)エラー時、少し待ってから1回だけ再試行するための状態
	bool bLastRequestWasRetry = false;
	TArray<uint8> LastRequestedWavBytes;
	FTimerHandle RetryTimerHandle;
	double SttTotalStartTimeSeconds = 0.0;
	double SttRequestStartTimeSeconds = 0.0;
};
