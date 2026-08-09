#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MicTestActor.generated.h"

class UMicRecorderComponent;
class UWhisperTranscriberComponent;
class UTTSComponent;
class ULipSyncComponent;
class AChatManager;
class ACameraActor;

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
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UMicRecorderComponent* MicRecorder;

	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UWhisperTranscriberComponent* WhisperTranscriber;

	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	UTTSComponent* TTS;

	UPROPERTY(VisibleAnywhere, Category = "MicTest")
	ULipSyncComponent* LipSync;

	// レベルに配置済みのChatManager(BP_ChatManager等)を詳細パネルからドラッグして設定する
	UPROPERTY(EditAnywhere, Category = "MicTest")
	AChatManager* ChatManager;

	// AIキャラクター本体(MetaHumanのアクター)。LipSyncと起動時の向き調整の両方に使う
	UPROPERTY(EditAnywhere, Category = "MicTest")
	AActor* CharacterActor;

	// 起動時に視点を切り替える「顔アップ」用のカメラ(レベルにCameraActorを配置して設定)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	ACameraActor* IntroFaceCamera;

	// MetaHumanのメッシュ前方向とActor本体の前方向がズレている場合の補正角度(度)。
	// 90, -90, 180などを試して、実際に正面を向く値を見つけて設定する。
	UPROPERTY(EditAnywhere, Category = "MicTest")
	float FacingYawOffset = 0.0f;

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

	// 【割り込み機能】AIが喋っている間にスペースキーを押すと、その場で発話を止めて聞き取りに切り替える(自動音量検知はオフ)

	// 保存先のWAVファイルパス(デバッグ確認用。Whisper送信自体はメモリ上のデータを使う)
	UPROPERTY(EditAnywhere, Category = "MicTest")
	FString OutputWavPath = TEXT("C:/Temp/mic_test.wav");

	FTimerHandle StartRecordingTimerHandle;
	FTimerHandle RecordingMonitorTimerHandle;

	bool bSpeechDetectedInCurrentRecording = false;
	float SilenceElapsedSeconds = 0.0f;
	float RecordingElapsedSeconds = 0.0f;

	// ボトルネック計測用: 録音停止時刻からの経過時間をログに出す
	double TurnStartTimeSeconds = 0.0;

	void HandleStartRecording();
	void CheckRecordingProgress();
	void HandleStopRecording();
	void HandleInterruptKeyPressed();
	void BeginListeningAfterBargeInOrPlayback();

	UFUNCTION()
	void HandleTranscriptionComplete(const FString& TranscribedText);

	UFUNCTION()
	void HandleTranscriptionFailed(const FString& ErrorMessage);

	UFUNCTION()
	void HandleChatResponseReceived(const FString& ResponseText);

	UFUNCTION()
	void HandleTTSPlaybackStarted();

	UFUNCTION()
	void HandleTTSPlaybackFinished();

	UFUNCTION()
	void HandleTTSFailed(const FString& ErrorMessage);
};
