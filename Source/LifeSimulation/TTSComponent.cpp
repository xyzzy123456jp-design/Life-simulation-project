#include "TTSComponent.h"
#include "Sound/SoundWaveProcedural.h"
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
	OnPlaybackFinished.Broadcast();
}

void UTTSComponent::SpeakText(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}

	SendSpeechRequest(Text, false);
}

void UTTSComponent::SendSpeechRequest(const FString& Text, bool bIsRetry)
{
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("TTS: ApiKeyが設定されていません"));
		OnTTSFailed.Broadcast(TEXT("ApiKeyが設定されていません"));
		return;
	}

	LastRequestedText = Text;
	bLastRequestWasRetry = bIsRetry;

	TSharedPtr<FJsonObject> JsonBody = MakeShared<FJsonObject>();
	JsonBody->SetStringField(TEXT("model"), Model);
	JsonBody->SetStringField(TEXT("input"), Text);
	JsonBody->SetStringField(TEXT("voice"), Voice);
	JsonBody->SetStringField(TEXT("response_format"), TEXT("pcm")); // 生PCM(24kHz, 16bit, モノラル)で受け取る

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

	UE_LOG(LogTemp, Log, TEXT("TTS: 音声合成をリクエストしました(retry=%s): %s"), bIsRetry ? TEXT("true") : TEXT("false"), *Text);
}

void UTTSComponent::OnSpeechResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
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
			SendSpeechRequest(LastRequestedText, true);
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

	// ファイルに保存せず、メモリ上のPCMバイト列から直接再生用のSoundWaveを組み立てる
	USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(this);
	SoundWave->SetSampleRate(TTSSampleRate);
	SoundWave->NumChannels = 1;
	SoundWave->SoundGroup = SOUNDGROUP_Default;
	SoundWave->bLooping = false;
	const float DurationSeconds = static_cast<float>(PcmBytes.Num()) / static_cast<float>(TTSSampleRate * sizeof(int16));
	SoundWave->Duration = DurationSeconds;
	SoundWave->QueueAudio(PcmBytes.GetData(), PcmBytes.Num());

	// 毎回新しい一時的なAudioComponentを生成して再生する。
	// 使い回すと内部状態が壊れて再生できなくなることがあるための対策。
	UGameplayStatics::SpawnSound2D(this, SoundWave);

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
