#include "MicTestActor.h"
#include "MicRecorderComponent.h"
#include "WhisperTranscriberComponent.h"
#include "TTSComponent.h"
#include "ChatManager.h"

AMicTestActor::AMicTestActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MicRecorder = CreateDefaultSubobject<UMicRecorderComponent>(TEXT("MicRecorder"));
	WhisperTranscriber = CreateDefaultSubobject<UWhisperTranscriberComponent>(TEXT("WhisperTranscriber"));
	TTS = CreateDefaultSubobject<UTTSComponent>(TEXT("TTS"));
}

void AMicTestActor::BeginPlay()
{
	Super::BeginPlay();

	WhisperTranscriber->ApiKey = WhisperApiKey;
	WhisperTranscriber->Language = TEXT("en"); // 英語固定(短い発言での言語誤判定を防ぐ)
	WhisperTranscriber->OnTranscriptionComplete.AddDynamic(this, &AMicTestActor::HandleTranscriptionComplete);
	WhisperTranscriber->OnTranscriptionFailed.AddDynamic(this, &AMicTestActor::HandleTranscriptionFailed);

	TTS->ApiKey = TTSApiKey;
	TTS->OnPlaybackFinished.AddDynamic(this, &AMicTestActor::HandleTTSPlaybackFinished);
	TTS->OnTTSFailed.AddDynamic(this, &AMicTestActor::HandleTTSFailed);

	if (ChatManager)
	{
		ChatManager->OnChatResponseReceived.AddDynamic(this, &AMicTestActor::HandleChatResponseReceived);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MicTestActor: ChatManagerが設定されていません。詳細パネルで設定してください"));
	}

	UE_LOG(LogTemp, Log, TEXT("MicTestActor: %.1f秒後に録音を開始します"), StartDelaySeconds);

	GetWorldTimerManager().SetTimer(
		StartRecordingTimerHandle,
		this,
		&AMicTestActor::HandleStartRecording,
		StartDelaySeconds,
		false
	);
}

void AMicTestActor::HandleStartRecording()
{
	if (MicRecorder->StartRecording())
	{
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: 録音開始。話し終わって%.1f秒無音になったら自動停止します(上限%.1f秒)"), SilenceDurationToStopSeconds, MaxRecordDurationSeconds);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, MaxRecordDurationSeconds, FColor::Green, TEXT("Listening..."));
		}

		bSpeechDetectedInCurrentRecording = false;
		SilenceElapsedSeconds = 0.0f;
		RecordingElapsedSeconds = 0.0f;

		GetWorldTimerManager().SetTimer(
			RecordingMonitorTimerHandle,
			this,
			&AMicTestActor::CheckRecordingProgress,
			RecordingMonitorIntervalSeconds,
			true // 繰り返し実行
		);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MicTestActor: 録音開始に失敗しました"));
	}
}

void AMicTestActor::CheckRecordingProgress()
{
	RecordingElapsedSeconds += RecordingMonitorIntervalSeconds;

	const float RecentRms = MicRecorder->GetRecentRms(RecordingMonitorIntervalSeconds);

	if (RecentRms >= SilenceThreshold)
	{
		bSpeechDetectedInCurrentRecording = true;
		SilenceElapsedSeconds = 0.0f;
	}
	else if (bSpeechDetectedInCurrentRecording)
	{
		SilenceElapsedSeconds += RecordingMonitorIntervalSeconds;
	}

	const bool bSilenceTimeoutReached = bSpeechDetectedInCurrentRecording && SilenceElapsedSeconds >= SilenceDurationToStopSeconds;
	const bool bMaxDurationReached = RecordingElapsedSeconds >= MaxRecordDurationSeconds;

	if (bSilenceTimeoutReached || bMaxDurationReached)
	{
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: 録音を自動停止します(無音タイムアウト=%s, 上限到達=%s)"),
			bSilenceTimeoutReached ? TEXT("true") : TEXT("false"),
			bMaxDurationReached ? TEXT("true") : TEXT("false"));

		GetWorldTimerManager().ClearTimer(RecordingMonitorTimerHandle);
		HandleStopRecording();
	}
}

void AMicTestActor::HandleStopRecording()
{
	MicRecorder->StopRecording();

	// 無音・ノイズだけならWhisperに送らず、次の録音サイクルへ進む(ハルシネーション対策)
	if (!MicRecorder->HasSignificantAudio())
	{
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: 有意な音声が検出されなかったため、Whisperには送らず次の録音に移ります"));

		GetWorldTimerManager().SetTimer(
			StartRecordingTimerHandle,
			this,
			&AMicTestActor::HandleStartRecording,
			NextTurnDelaySeconds,
			false
		);
		return;
	}

	// デバッグ確認用に一応WAVファイルとしても保存しておく
	const bool bSaved = MicRecorder->SaveRecordedAudioToWav(OutputWavPath);
	if (bSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: WAV保存に成功しました -> %s"), *OutputWavPath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MicTestActor: WAV保存に失敗しました"));
	}

	// 本番の流れ: メモリ上のPCMデータを直接Whisper APIへ送信する
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: Whisper APIへ送信します..."));
	WhisperTranscriber->TranscribeFromMicRecorder(MicRecorder);
}

void AMicTestActor::HandleTranscriptionComplete(const FString& TranscribedText)
{
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: 文字起こし結果 -> %s"), *TranscribedText);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Cyan, FString::Printf(TEXT("You: %s"), *TranscribedText));
	}

	if (ChatManager)
	{
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: ChatManagerへ転送します"));
		ChatManager->SendMessage(TranscribedText);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MicTestActor: ChatManagerが未設定のため転送できません"));
	}
}

void AMicTestActor::HandleTranscriptionFailed(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("MicTestActor: 文字起こし失敗 -> %s"), *ErrorMessage);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::Printf(TEXT("Whisper failed: %s"), *ErrorMessage));
	}

	// Whisperが失敗しても会話ループが止まらないよう、次の録音を開始する
	GetWorldTimerManager().SetTimer(
		StartRecordingTimerHandle,
		this,
		&AMicTestActor::HandleStartRecording,
		NextTurnDelaySeconds,
		false
	);
}

void AMicTestActor::HandleChatResponseReceived(const FString& ResponseText)
{
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: ChatGPTからの返答 -> %s"), *ResponseText);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, FString::Printf(TEXT("AI: %s"), *ResponseText));
	}

	// 返答テキストをそのまま読み上げる
	TTS->SpeakText(ResponseText);
}

void AMicTestActor::HandleTTSPlaybackFinished()
{
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: 読み上げ完了。%.1f秒後に次の録音を開始します"), NextTurnDelaySeconds);

	GetWorldTimerManager().SetTimer(
		StartRecordingTimerHandle,
		this,
		&AMicTestActor::HandleStartRecording,
		NextTurnDelaySeconds,
		false
	);
}

void AMicTestActor::HandleTTSFailed(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("MicTestActor: TTS失敗 -> %s"), *ErrorMessage);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::Printf(TEXT("TTS failed: %s"), *ErrorMessage));
	}

	// TTSが失敗しても会話ループが止まらないよう、次の録音を開始する
	GetWorldTimerManager().SetTimer(
		StartRecordingTimerHandle,
		this,
		&AMicTestActor::HandleStartRecording,
		NextTurnDelaySeconds,
		false
	);
}
