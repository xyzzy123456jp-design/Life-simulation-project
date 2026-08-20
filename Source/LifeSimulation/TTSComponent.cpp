#include "TTSComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Json.h"

UTTSComponent::UTTSComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTTSComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UTTSComponent::HandleAudioFinished()
{
	UE_LOG(LogTemp, Log, TEXT("TTS: 音声再生が終了しました"));
	// Procedural SoundWaveは音声データを消費した後もAudioComponentが
	// IsPlaying=trueを返す場合がある。次の録音開始を妨げないよう、終了通知の前に
	// 明示的に停止・解放する。
	if (CurrentAudioComponent)
	{
		CurrentAudioComponent->Stop();
		CurrentAudioComponent = nullptr;
	}
	OnPlaybackFinished.Broadcast();
}

void UTTSComponent::SpeakText(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}

	SendSpeechRequest(Text, TEXT("neutral"), 0.0f, false);
}

void UTTSComponent::SpeakTextWithEmotion(const FString& Text, const FString& Emotion, float Intensity)
{
	if (Text.IsEmpty())
	{
		return;
	}

	SendSpeechRequest(Text, Emotion, Intensity, false);
}

FString UTTSComponent::BuildEmotionInstruction(const FString& Emotion, float Intensity) const
{
	const FString NormalizedEmotion = Emotion.ToLower();
	const float SafeIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	const TCHAR* Strength = SafeIntensity < 0.35f
		? TEXT("Keep the emotional coloring very subtle.")
		: (SafeIntensity < 0.70f
			? TEXT("Use a moderate emotional coloring.")
			: TEXT("Make the emotion clear, but restrained and natural."));

	FString Style;
	if (NormalizedEmotion == TEXT("happy"))
	{
		Style = TEXT("Speak in a warm and cheerful tone.");
	}
	else if (NormalizedEmotion == TEXT("sad"))
	{
		Style = TEXT("Speak softly in a subdued and slightly sad tone, while remaining clear.");
	}
	else if (NormalizedEmotion == TEXT("surprised"))
	{
		Style = TEXT("Speak with noticeable surprise and lively intonation.");
	}
	else if (NormalizedEmotion == TEXT("confused"))
	{
		Style = TEXT("Speak with mild uncertainty and a slightly questioning tone.");
	}
	else if (NormalizedEmotion == TEXT("embarrassed"))
	{
		Style = TEXT("Speak in a slightly bashful and flustered tone.");
	}
	else
	{
		return TEXT("Speak naturally in a calm conversational tone, at a normal pace and volume.");
	}

	return FString::Printf(TEXT("%s %s Keep a natural conversational pace and volume. Do not exaggerate, shout, or rush."),
		*Style, Strength);
}

void UTTSComponent::SendSpeechRequest(const FString& Text, const FString& Emotion, float Intensity, bool bIsRetry)
{
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("TTS: ApiKeyが設定されていません"));
		OnTTSFailed.Broadcast(TEXT("ApiKeyが設定されていません"));
		return;
	}

	LastRequestedText = Text;
	LastRequestedEmotion = Emotion.ToLower();
	LastRequestedEmotionIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	bLastRequestWasRetry = bIsRetry;
	if (!bIsRetry)
	{
		TtsRequestStartTimeSeconds = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] tts_request_start"));
	}
	const FString EmotionInstruction = BuildEmotionInstruction(
		LastRequestedEmotion, LastRequestedEmotionIntensity);

	TSharedPtr<FJsonObject> JsonBody = MakeShared<FJsonObject>();
	JsonBody->SetStringField(TEXT("model"), Model);
	JsonBody->SetStringField(TEXT("input"), Text);
	JsonBody->SetStringField(TEXT("voice"), Voice);
	JsonBody->SetStringField(TEXT("response_format"), TEXT("pcm")); // 生PCM(24kHz, 16bit, モノラル)で受け取る
	JsonBody->SetStringField(TEXT("instructions"), EmotionInstruction);

	FString RequestBodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBodyString);
	FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(TEXT("https://api.openai.com/v1/audio/speech"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBodyString);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UTTSComponent::OnSpeechResponseReceived);
	HttpRequest->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("[TTS][LEGACY][EMOTION] model=%s emotion=%s ai_intensity=%.2f instruction=\"%s\""),
		*Model, *LastRequestedEmotion, LastRequestedEmotionIntensity, *EmotionInstruction);
	UE_LOG(LogTemp, Log, TEXT("TTS: 音声合成をリクエストしました(model=%s voice=%s retry=%s): %s"),
		*Model, *Voice, bIsRetry ? TEXT("true") : TEXT("false"), *Text);
}

void UTTSComponent::OnSpeechResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	const double TtsResponseTimeSeconds = FPlatformTime::Seconds();
	const bool bFailed = !bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200;

	if (bFailed)
	{
		const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		const FString ErrorBody = Response.IsValid() ? Response->GetContentAsString() : TEXT("");
		UE_LOG(LogTemp, Error, TEXT("TTS: リクエスト失敗(Status=%d): %s"), StatusCode, *ErrorBody);

		// まだリトライしていなければ、1回だけ同じテキストで再送する
		if (!bLastRequestWasRetry)
		{
			UE_LOG(LogTemp, Warning, TEXT("TTS: 1回だけ再試行します"));
			SendSpeechRequest(LastRequestedText, LastRequestedEmotion, LastRequestedEmotionIntensity, true);
			return;
		}

		OnTTSFailed.Broadcast(FString::Printf(TEXT("TTS APIエラー(Status=%d)"), StatusCode));
		return;
	}

	const TArray<uint8>& PcmBytes = Response->GetContent();
	if (PcmBytes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TTS: 受信した音声データが空です"));
		OnTTSFailed.Broadcast(TEXT("受信した音声データが空です"));
		return;
	}
	const double TtsElapsedSeconds = TtsRequestStartTimeSeconds > 0.0
		? TtsResponseTimeSeconds - TtsRequestStartTimeSeconds
		: 0.0;
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] tts_response_done elapsed=%.3f sec bytes=%d"),
		TtsElapsedSeconds, PcmBytes.Num());

	// ファイルに保存せず、メモリ上のPCMバイト列から直接再生用のSoundWaveを組み立てる
	USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(this);
	SoundWave->SetSampleRate(TTSSampleRate);
	SoundWave->NumChannels = 1;
	SoundWave->SoundGroup = SOUNDGROUP_Default;
	SoundWave->bLooping = false;
	const float DurationSeconds = static_cast<float>(PcmBytes.Num()) / static_cast<float>(TTSSampleRate * sizeof(int16));
	SoundWave->Duration = DurationSeconds;
	SoundWave->QueueAudio(PcmBytes.GetData(), PcmBytes.Num());

	// リップシンク用: 一定間隔(EnvelopeStepSeconds)ごとのRMS音量をあらかじめ計算しておく
	AmplitudeEnvelope.Reset();
	{
		const int32 SamplesPerStep = FMath::Max(1, FMath::RoundToInt(EnvelopeStepSeconds * TTSSampleRate));
		const int32 NumSamples = PcmBytes.Num() / sizeof(int16);
		const int16* SamplesPtr = reinterpret_cast<const int16*>(PcmBytes.GetData());

		for (int32 i = 0; i < NumSamples; i += SamplesPerStep)
		{
			const int32 EndIndex = FMath::Min(i + SamplesPerStep, NumSamples);
			double SumOfSquares = 0.0;
			for (int32 j = i; j < EndIndex; ++j)
			{
				const float NormalizedSample = SamplesPtr[j] / 32768.0f;
				SumOfSquares += static_cast<double>(NormalizedSample) * NormalizedSample;
			}
			const float Rms = static_cast<float>(FMath::Sqrt(SumOfSquares / FMath::Max(1, EndIndex - i)));
			// 経験的に0.3程度を「口を大きく開ける」音量の目安として正規化(LipSyncComponent側でも倍率調整可能)
			AmplitudeEnvelope.Add(FMath::Clamp(Rms / 0.3f, 0.0f, 1.0f));
		}
	}
	PlaybackStartTimeSeconds = FPlatformTime::Seconds();

	// 毎回新しい一時的なAudioComponentを生成して再生する。
	// 使い回すと内部状態が壊れて再生できなくなることがあるための対策。
	CurrentAudioComponent = UGameplayStatics::SpawnSound2D(this, SoundWave, PlaybackVolumeMultiplier);
	const double PlaybackStartLatencySeconds = FPlatformTime::Seconds() - TtsResponseTimeSeconds;
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] playback_start pcm_prepare_elapsed=%.3f sec"),
		PlaybackStartLatencySeconds);

	UE_LOG(LogTemp, Log, TEXT("TTS: 音声再生を開始しました(%d bytes, %.2f秒)"), PcmBytes.Num(), DurationSeconds);
	OnPlaybackStarted.Broadcast();

	// USoundWaveProceduralはOnAudioFinishedが確実に発火しないため、
	// 再生時間からタイマーで「終了」を判定する
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlaybackFinishedTimerHandle);
		World->GetTimerManager().SetTimer(
			PlaybackFinishedTimerHandle,
			this,
			&UTTSComponent::HandleAudioFinished,
			DurationSeconds + 0.3f,
			false
		);
	}
}

bool UTTSComponent::IsPlaying() const
{
	return CurrentAudioComponent != nullptr && CurrentAudioComponent->IsPlaying();
}

float UTTSComponent::GetCurrentAmplitude() const
{
	if (!IsPlaying() || AmplitudeEnvelope.Num() == 0)
	{
		return 0.0f;
	}

	const double Elapsed = FPlatformTime::Seconds() - PlaybackStartTimeSeconds;
	const int32 Index = FMath::Clamp(FMath::FloorToInt(Elapsed / EnvelopeStepSeconds), 0, AmplitudeEnvelope.Num() - 1);
	return AmplitudeEnvelope[Index];
}

void UTTSComponent::StopPlayback()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlaybackFinishedTimerHandle);
	}

	if (CurrentAudioComponent && CurrentAudioComponent->IsPlaying())
	{
		CurrentAudioComponent->Stop();
		UE_LOG(LogTemp, Log, TEXT("TTS: 割り込みにより再生を停止しました"));
	}

	CurrentAudioComponent = nullptr;
	// 意図的な停止なのでOnPlaybackFinishedは発火させない(呼び出し側が状況を把握しているため)
}
