#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MicTestActor.generated.h"

class UMicRecorderComponent;
class UWhisperTranscriberComponent;
class UTTSComponent;
class AChatManager;

/**
 * MicRecorderComponentの動作確認用テストActor。
 * 「録音開始→数秒待機→録音停止→WAV保存→Whisper API送信→ChatManagerへ転送→TTSで読み上げ」を
 * TTSの再生完了をトリガーに自動で繰り返す(会話ループ)。
 * レベルに1つ配置するだけで動作確認できる。
 */
UCLASS()
class LIFESIMULATION_API AMicTestActor : public AActor
{
	GENERATED_BODY()

public:
	AMicTestActor();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UMicRecorderComponent* MicRecorder;

	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UWhisperTranscriberComponent* WhisperTranscriber;

	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UTTSComponent* TTS;

	// レベルに配置済みのChatManager(BP_ChatManager等)を詳細パネルからドラッグして設定する
	UPROPERTY(EditAnywhere, Category = "MicTest")
	AChatManager* ChatManager;

	// OpenAIのAPIキー。詳細パネルから設定すること(コード直書き・Git管理下は避ける)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	FString WhisperApiKey;

	// TTS用のOpenAI APIキー(Whisperと同じキーでよい)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	FString TTSApiKey;

	// 最初の録音を開始するまでの待機秒数
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float StartDelaySeconds = 2.0f;

	// 録音中、これだけ音量が閾値を下回る状態(無音)が続いたら「話し終わった」とみなして録音を止める
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float SilenceDurationToStopSeconds = 1.2f;

	// 無音判定に使う音量の閾値(RMS)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float SilenceThreshold = 0.02f;

	// 録音の安全上限(この秒数に達したら、まだ話していても強制的に録音を止める)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float MaxRecordDurationSeconds = 20.0f;

	// 音量を監視する間隔(秒)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float RecordingMonitorIntervalSeconds = 0.2f;

	// TTS再生終了後、次の録音を始めるまでの待機秒数
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float NextTurnDelaySeconds = 1.0f;

	// 保存先のWAVファイルパス(デバッグ確認用。Whisper送信自体はメモリ上のデータを使う)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	FString OutputWavPath = TEXT("C:/Temp/mic_test.wav");

	FTimerHandle StartRecordingTimerHandle;
	FTimerHandle RecordingMonitorTimerHandle;

	bool bSpeechDetectedInCurrentRecording = false;
	float SilenceElapsedSeconds = 0.0f;
	float RecordingElapsedSeconds = 0.0f;

	void HandleStartRecording();
	void CheckRecordingProgress();
	void HandleStopRecording();

	UFUNCTION()
	void HandleTranscriptionComplete(const FString& TranscribedText);

	UFUNCTION()
	void HandleTranscriptionFailed(const FString& ErrorMessage);

	UFUNCTION()
	void HandleChatResponseReceived(const FString& ResponseText);

	UFUNCTION()
	void HandleTTSPlaybackFinished();

	UFUNCTION()
	void HandleTTSFailed(const FString& ErrorMessage);
};
