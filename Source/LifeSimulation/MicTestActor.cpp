#include "MicTestActor.h"
#include "MicRecorderComponent.h"
#include "WhisperTranscriberComponent.h"
#include "TTSComponent.h"
#include "LipSyncComponent.h"
#include "ChatManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"

AMicTestActor::AMicTestActor()
{
	PrimaryActorTick.bCanEverTick = true;

	MicRecorder = CreateDefaultSubobject<UMicRecorderComponent>(TEXT("MicRecorder"));
	WhisperTranscriber = CreateDefaultSubobject<UWhisperTranscriberComponent>(TEXT("WhisperTranscriber"));
	TTS = CreateDefaultSubobject<UTTSComponent>(TEXT("TTS"));
	LipSync = CreateDefaultSubobject<ULipSyncComponent>(TEXT("LipSync"));
}

void AMicTestActor::BeginPlay()
{
	Super::BeginPlay();

	WhisperTranscriber->ApiKey = WhisperApiKey;
	WhisperTranscriber->Language = TEXT("en"); // 英語固定(短い発言での言語誤判定を防ぐ)
	WhisperTranscriber->OnTranscriptionComplete.AddDynamic(this, &AMicTestActor::HandleTranscriptionComplete);
	WhisperTranscriber->OnTranscriptionFailed.AddDynamic(this, &AMicTestActor::HandleTranscriptionFailed);

	TTS->ApiKey = TTSApiKey;
	TTS->OnPlaybackStarted.AddDynamic(this, &AMicTestActor::HandleTTSPlaybackStarted);
	TTS->OnPlaybackFinished.AddDynamic(this, &AMicTestActor::HandleTTSPlaybackFinished);
	TTS->OnTTSFailed.AddDynamic(this, &AMicTestActor::HandleTTSFailed);

	// LipSyncComponentがTTSの音量とキャラクターを参照できるようにする
	LipSync->TTSSource = TTS;
	LipSync->CharacterActor = CharacterActor;

	// 重要: CharacterActor自身が(アイドルアニメーションやAIコントローラーなどで)
	// 毎フレーム自分の向きを制御している場合、単純に両方が毎フレーム上書きし合うと
	// 「同じフレーム内でどちらが後に実行されるか」次第で勝敗が決まってしまい不安定になる。
	// これを解消するため、CharacterActorのTickが必ず先に実行され、
	// このActorのTick(向きの強制)がその後に実行されるよう明示的に順序を指定する。
	if (CharacterActor)
	{
		AddTickPrerequisiteActor(CharacterActor);
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: CharacterActorの後にTickされるよう順序を設定しました"));
	}

	// 起動時、視点を顔アップカメラに切り替える(こちらは一度だけでOK)
	if (IntroFaceCamera)
	{
		if (APlayerController* IntroPC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			IntroPC->SetViewTargetWithBlend(IntroFaceCamera, 0.0f);
			UE_LOG(LogTemp, Log, TEXT("MicTestActor: 視点を顔アップカメラに切り替えました"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MicTestActor: IntroFaceCameraが未設定です"));
	}

	if (!CharacterActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("MicTestActor: CharacterActorが未設定です"));
	}

	if (ChatManager)
	{
		ChatManager->OnChatResponseReceived.AddDynamic(this, &AMicTestActor::HandleChatResponseReceived);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MicTestActor: ChatManagerが設定されていません。詳細パネルで設定してください"));
	}

	// キー入力で、AIが喋っている最中に割り込めるようにする
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: [INPUT-DEBUG] PlayerController取得 -> %s"), PC ? TEXT("成功") : TEXT("失敗(nullptr)"));

	if (PC)
	{
		EnableInput(PC);
	}

	UE_LOG(LogTemp, Log, TEXT("MicTestActor: [INPUT-DEBUG] EnableInput後のInputComponent -> %s"), InputComponent ? TEXT("存在する") : TEXT("nullptr"));

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AMicTestActor::HandleInterruptKeyPressed);
		UE_LOG(LogTemp, Log, TEXT("MicTestActor: [INPUT-DEBUG] Pキーのバインドを実行しました"));
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

void AMicTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 毎フレーム、キャラクターをカメラの方に向かせ続ける。
	// 他の何かが毎フレーム向きを上書きしていても、こちらも毎フレーム上書きし返すことで
	// 最終的に正しい向きを維持する。
	if (CharacterActor && IntroFaceCamera)
	{
		FVector ToCamera = IntroFaceCamera->GetActorLocation() - CharacterActor->GetActorLocation();
		FRotator FacingRotation = ToCamera.Rotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += FacingYawOffset; // メッシュの前方向オフセット補正
		CharacterActor->SetActorRotation(FacingRotation);
	}
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

	// ボトルネック計測: ここを T=0.00s として、以降の各ステップの経過時間をログに出す
	TurnStartTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[TIMING] T=+0.00s 録音停止"));

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
	const double Elapsed = FPlatformTime::Seconds() - TurnStartTimeSeconds;
	UE_LOG(LogTemp, Log, TEXT("[TIMING] T=+%.2fs Whisper文字起こし完了"), Elapsed);
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
	const double Elapsed = FPlatformTime::Seconds() - TurnStartTimeSeconds;
	UE_LOG(LogTemp, Log, TEXT("[TIMING] T=+%.2fs ChatGPT応答受信"), Elapsed);
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: ChatGPTからの返答 -> %s"), *ResponseText);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, FString::Printf(TEXT("AI: %s"), *ResponseText));
	}

	// 返答テキストをそのまま読み上げる
	TTS->SpeakText(ResponseText);
}

void AMicTestActor::HandleTTSPlaybackStarted()
{
	const double Elapsed = FPlatformTime::Seconds() - TurnStartTimeSeconds;
	UE_LOG(LogTemp, Log, TEXT("[TIMING] T=+%.2fs TTS再生開始(音が聞こえ始めるタイミング)"), Elapsed);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Silver, TEXT("(Press SPACE to interrupt)"));
	}
}

void AMicTestActor::HandleInterruptKeyPressed()
{
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: [INPUT-DEBUG] キー入力を検知しました(TTS再生中=%s)"), TTS->IsPlaying() ? TEXT("true") : TEXT("false"));

	if (!TTS->IsPlaying())
	{
		// AIが喋っていない時にキーを押しても何もしない
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("MicTestActor: スペースキーで割り込みました。AIの発話を停止します"));

	TTS->StopPlayback();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("(interrupted)"));
	}

	// ここから新しく録音を開始する(スペースを押した瞬間からの発言を録る)
	BeginListeningAfterBargeInOrPlayback();
}

void AMicTestActor::BeginListeningAfterBargeInOrPlayback()
{
	// 録音中であれば一度止めてから、クリーンに録音し直す
	MicRecorder->StopRecording();
	HandleStartRecording();
}

void AMicTestActor::HandleTTSPlaybackFinished()
{
	UE_LOG(LogTemp, Log, TEXT("MicTestActor: 読み上げ完了。次の録音を開始します"));
	BeginListeningAfterBargeInOrPlayback();
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
