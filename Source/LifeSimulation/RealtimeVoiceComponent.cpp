#include "RealtimeVoiceComponent.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Json.h"
#include "Misc/Base64.h"

URealtimeVoiceComponent::URealtimeVoiceComponent()
{
	// RemainingPlaybackSecondsを毎フレーム減算するためにTickが必要
	PrimaryComponentTick.bCanEverTick = true;
}

void URealtimeVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URealtimeVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RemainingPlaybackSeconds > 0.0f)
	{
		RemainingPlaybackSeconds = FMath::Max(0.0f, RemainingPlaybackSeconds - DeltaTime);
	}

	// AmplitudeEnvelopeQueueを実時間で消費し、CurrentAmplitudeを
	// 「今まさにスピーカーで鳴っているはずの音量」に同期させる。
	// (ネットワークが先行してチャンクを送ってきていても、ここで実際の
	// 再生タイミングに合わせて1チャンクずつ順番に反映することで、
	// 「最後に届いたチャンクの音量で止まったまま」になる問題を防ぐ)
	float TimeToConsume = DeltaTime;
	while (TimeToConsume > 0.0f && AmplitudeEnvelopeQueue.Num() > 0)
	{
		TPair<float, float>& Front = AmplitudeEnvelopeQueue[0];
		CurrentAmplitude = Front.Value;

		if (Front.Key > TimeToConsume)
		{
			Front.Key -= TimeToConsume;
			TimeToConsume = 0.0f;
		}
		else
		{
			TimeToConsume -= Front.Key;
			AmplitudeEnvelopeQueue.RemoveAt(0);
		}
	}

	// キューを使い切り、かつ再生残り時間もなくなっていれば、実際に発話が
	// 終わったとみなして振幅をリセットする
	if (AmplitudeEnvelopeQueue.Num() == 0 && RemainingPlaybackSeconds <= 0.0f)
	{
		CurrentAmplitude = 0.0f;
	}
}

void URealtimeVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect();
	Super::EndPlay(EndPlayReason);
}

// ============================================================
// 接続 / 切断
// ============================================================

void URealtimeVoiceComponent::Connect()
{
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: ApiKeyが設定されていません"));
		OnError.Broadcast(TEXT("ApiKeyが設定されていません"));
		return;
	}

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModule("WebSockets");
	}

	const FString Url = FString::Printf(TEXT("wss://api.openai.com/v1/realtime?model=%s"), *Model);

	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, FString(), Headers);

	WebSocket->OnConnected().AddUObject(this, &URealtimeVoiceComponent::HandleWebSocketConnected);
	WebSocket->OnConnectionError().AddUObject(this, &URealtimeVoiceComponent::HandleWebSocketConnectionError);
	WebSocket->OnClosed().AddUObject(this, &URealtimeVoiceComponent::HandleWebSocketClosed);
	WebSocket->OnMessage().AddUObject(this, &URealtimeVoiceComponent::HandleWebSocketMessage);

	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: 接続を開始します(%s)"), *Url);
	WebSocket->Connect();
}

void URealtimeVoiceComponent::Disconnect()
{
	StopMicCapture();
	StopPlaybackImmediately();

	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		WebSocket->Close();
	}
	WebSocket.Reset();
	bIsConnected = false;
	bIsAssistantSpeaking = false;
	bDiscardIncomingAudioDeltas = false;
	RemainingPlaybackSeconds = 0.0f;
	AmplitudeEnvelopeQueue.Empty();
}

// ============================================================
// WebSocketイベント
// ============================================================

void URealtimeVoiceComponent::HandleWebSocketConnected()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: WebSocket接続成功"));
	bIsConnected = true;

	SendSessionUpdate();
	StartMicCapture();
	EnsurePlaybackReady();

	OnConnected.Broadcast();
}

void URealtimeVoiceComponent::HandleWebSocketConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: 接続エラー: %s"), *Error);
	bIsConnected = false;
	OnError.Broadcast(Error);
}

void URealtimeVoiceComponent::HandleWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: 接続が閉じられました(Status=%d, Reason=%s)"), StatusCode, *Reason);
	bIsConnected = false;
	bIsAssistantSpeaking = false;
	RemainingPlaybackSeconds = 0.0f;
	AmplitudeEnvelopeQueue.Empty();
	StopMicCapture();
	OnDisconnected.Broadcast(Reason);
}

void URealtimeVoiceComponent::HandleWebSocketMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	FString EventType;
	if (!JsonObject->TryGetStringField(TEXT("type"), EventType))
	{
		return;
	}

	if (EventType == TEXT("response.output_audio.delta"))
	{
		// 割り込み直後の残留チャンクは再生しない
		if (bDiscardIncomingAudioDeltas)
		{
			return;
		}

		FString Base64Audio;
		if (JsonObject->TryGetStringField(TEXT("delta"), Base64Audio))
		{
			TArray<uint8> PcmBytes;
			if (FBase64::Decode(Base64Audio, PcmBytes))
			{
				QueuePlaybackAudio(PcmBytes);
			}
		}
	}
	else if (EventType == TEXT("response.output_audio.done"))
	{
		// CurrentAmplitudeはここでリセットしない。サーバーは送信完了だが、
		// スピーカーではまだ再生中の可能性があり(キュー済み音声の再生残り)、
		// リップシンクの振幅もその間は実際の再生に追従させ続ける必要がある。
		// 実際のリセットはTickComponentでRemainingPlaybackSecondsが0になった
		// 時点(=再生を実際に終えたと推定される時点)で行う。

		// サーバー側は送信完了だが、スピーカーではまだ再生中の可能性があるため
		// ここではフラグだけ倒す。実際のミュート解除はRemainingPlaybackSecondsが
		// 0になった時点(=キューした音声を実際に再生し終えたと推定される時点)
		bIsAssistantSpeaking = false;
		UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: AI発話(サーバー側)終了。再生完了まで引き続きマイクをミュートします"));
	}
	else if (EventType == TEXT("response.output_audio_transcript.done"))
	{
		FString Transcript;
		if (JsonObject->TryGetStringField(TEXT("transcript"), Transcript))
		{
			UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: AI発言 -> %s"), *Transcript);
			OnAssistantTranscript.Broadcast(Transcript);
		}
	}
	else if (EventType == TEXT("conversation.item.input_audio_transcription.completed"))
	{
		FString Transcript;
		if (JsonObject->TryGetStringField(TEXT("transcript"), Transcript))
		{
			UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: ユーザー発言 -> %s"), *Transcript);
			OnUserTranscript.Broadcast(Transcript);
		}
	}
	else if (EventType == TEXT("input_audio_buffer.speech_started"))
	{
		UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: ユーザーの発話を検知(AI発話を中断)"));
		StopPlaybackImmediately();
		OnUserStartedSpeaking.Broadcast();
	}
	else if (EventType == TEXT("response.created"))
	{
		// AIが喋り始める。エコー(スピーカー音のマイク回り込み)による誤検知を防ぐため、
		// 発話が終わる(response.output_audio.done)か中断されるまでマイク送信をミュートする
		bIsAssistantSpeaking = true;
		UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: AI発話開始。マイクをミュートします"));

		// 新しい発話が正式に始まったので、以降の音声チャンクは正規のものとして受け付ける
		bDiscardIncomingAudioDeltas = false;

		OnAssistantStartedSpeaking.Broadcast();
	}
	else if (EventType == TEXT("error"))
	{
		FString ErrorMessage = TEXT("不明なエラー");
		const TSharedPtr<FJsonObject>* ErrorObject;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject))
		{
			(*ErrorObject)->TryGetStringField(TEXT("message"), ErrorMessage);
		}

		// 「キャンセル対象のレスポンスが無かった」エラーは無害(Pキー割り込みが
		// AIの発話終了直後に押された場合などに起きるだけ)なので、ログにのみ残し
		// 画面表示(OnError)はしない
		if (ErrorMessage.Contains(TEXT("no active response")))
		{
			UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: サーバーエラー(無視): %s"), *ErrorMessage);
			return;
		}

		UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: サーバーエラー: %s"), *ErrorMessage);
		OnError.Broadcast(ErrorMessage);
	}
}

void URealtimeVoiceComponent::SendJson(const TSharedRef<FJsonObject>& JsonObject)
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		return;
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject, Writer);
	WebSocket->Send(OutputString);
}

void URealtimeVoiceComponent::SendSessionUpdate()
{
	TSharedRef<FJsonObject> InputFormat = MakeShared<FJsonObject>();
	InputFormat->SetStringField(TEXT("type"), TEXT("audio/pcm"));
	InputFormat->SetNumberField(TEXT("rate"), RealtimeSampleRate);

	TSharedRef<FJsonObject> TurnDetection = MakeShared<FJsonObject>();
	TurnDetection->SetStringField(TEXT("type"), TEXT("server_vad"));

	TSharedRef<FJsonObject> Transcription = MakeShared<FJsonObject>();
	Transcription->SetStringField(TEXT("model"), TEXT("gpt-4o-mini-transcribe"));

	TSharedRef<FJsonObject> AudioInput = MakeShared<FJsonObject>();
	AudioInput->SetObjectField(TEXT("format"), InputFormat);
	AudioInput->SetObjectField(TEXT("turn_detection"), TurnDetection);
	AudioInput->SetObjectField(TEXT("transcription"), Transcription);

	TSharedRef<FJsonObject> OutputFormat = MakeShared<FJsonObject>();
	OutputFormat->SetStringField(TEXT("type"), TEXT("audio/pcm"));
	OutputFormat->SetNumberField(TEXT("rate"), RealtimeSampleRate);

	TSharedRef<FJsonObject> AudioOutput = MakeShared<FJsonObject>();
	AudioOutput->SetObjectField(TEXT("format"), OutputFormat);
	AudioOutput->SetStringField(TEXT("voice"), Voice);

	TSharedRef<FJsonObject> Audio = MakeShared<FJsonObject>();
	Audio->SetObjectField(TEXT("input"), AudioInput);
	Audio->SetObjectField(TEXT("output"), AudioOutput);

	TArray<TSharedPtr<FJsonValue>> OutputModalities;
	OutputModalities.Add(MakeShared<FJsonValueString>(TEXT("audio")));

	TSharedRef<FJsonObject> Session = MakeShared<FJsonObject>();
	Session->SetStringField(TEXT("type"), TEXT("realtime"));
	Session->SetStringField(TEXT("model"), Model);
	Session->SetArrayField(TEXT("output_modalities"), OutputModalities);
	Session->SetStringField(TEXT("instructions"), Instructions);
	Session->SetObjectField(TEXT("audio"), Audio);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("session.update"));
	Root->SetObjectField(TEXT("session"), Session);

	SendJson(Root);
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: session.updateを送信しました"));
}

// ============================================================
// マイクキャプチャ(24kHzへダウンサンプリングしながらストリーミング送信)
// ============================================================

bool URealtimeVoiceComponent::StartMicCapture()
{
	Audio::FAudioCaptureDeviceParams Params = Audio::FAudioCaptureDeviceParams();

	Audio::FOnAudioCaptureFunction OnCapture =
		[this](const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
	{
		OnMicAudioCapture(AudioData, NumFrames, InNumChannels, InSampleRate, StreamTime, bOverflow);
	};

	if (!AudioCapture.OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
	{
		UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: マイクキャプチャストリームのオープンに失敗しました"));
		return false;
	}

	if (!AudioCapture.StartStream())
	{
		UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: マイクキャプチャの開始に失敗しました"));
		return false;
	}

	ResamplePhase = 0.0;
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: マイクキャプチャを開始しました(常時ストリーミング)"));
	return true;
}

void URealtimeVoiceComponent::StopMicCapture()
{
	AudioCapture.StopStream();
	AudioCapture.CloseStream();
}

void URealtimeVoiceComponent::OnMicAudioCapture(const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
{
	if (!bIsConnected)
	{
		return;
	}

	// 【エコー対策】AIが喋っている間、および実際にスピーカーで再生し終わったと
	// 推定されるまでの間は、マイクデータを送信しない。
	// bIsAssistantSpeaking: サーバーが発話開始/終了を通知したタイミング(即時性重視)
	// RemainingPlaybackSeconds > 0: ローカルでのキュー済み音声の再生がまだ残っている推定時間
	// (サーバーのdoneイベントより後まで実際の再生は続くため、こちらを優先してミュートを延長する)
	if (bMuteMicWhileAssistantSpeaking && (bIsAssistantSpeaking || RemainingPlaybackSeconds > 0.0f))
	{
		return;
	}

	CaptureSampleRate = InSampleRate;
	CaptureNumChannels = InNumChannels;

	const float* FloatAudioData = static_cast<const float*>(AudioData);

	// マルチチャンネルの場合はモノラルへ平均ミックス
	TArray<float> MonoSamples;
	MonoSamples.SetNumUninitialized(NumFrames);
	for (int32 i = 0; i < NumFrames; ++i)
	{
		float Sum = 0.0f;
		for (int32 c = 0; c < InNumChannels; ++c)
		{
			Sum += FloatAudioData[i * InNumChannels + c];
		}
		MonoSamples[i] = Sum / FMath::Max(1, InNumChannels);
	}

	// 24kHzへダウンサンプリング(連続的な位相アキュムレータ方式で、コールバック間のズレを蓄積させない)
	TArray<int16> OutSamples;
	if (InSampleRate == RealtimeSampleRate)
	{
		OutSamples.SetNumUninitialized(NumFrames);
		for (int32 i = 0; i < NumFrames; ++i)
		{
			OutSamples[i] = static_cast<int16>(FMath::Clamp(MonoSamples[i], -1.0f, 1.0f) * 32767.0f);
		}
	}
	else
	{
		const double Ratio = static_cast<double>(InSampleRate) / static_cast<double>(RealtimeSampleRate);
		while (ResamplePhase < NumFrames)
		{
			const int32 Index = FMath::Clamp(static_cast<int32>(ResamplePhase), 0, NumFrames - 1);
			const int16 Sample = static_cast<int16>(FMath::Clamp(MonoSamples[Index], -1.0f, 1.0f) * 32767.0f);
			OutSamples.Add(Sample);
			ResamplePhase += Ratio;
		}
		ResamplePhase -= NumFrames;
	}

	if (OutSamples.Num() == 0)
	{
		return;
	}

	TArray<uint8> Pcm16Bytes;
	Pcm16Bytes.SetNumUninitialized(OutSamples.Num() * sizeof(int16));
	FMemory::Memcpy(Pcm16Bytes.GetData(), OutSamples.GetData(), Pcm16Bytes.Num());

	const FString Base64Audio = FBase64::Encode(Pcm16Bytes);

	// AsyncTaskでゲームスレッドに戻してから送信する(WebSocket送信はゲームスレッド外からでも
	// 概ね安全だが、念のためWeakObjectPtr経由でゲームスレッドに委譲する)
	TWeakObjectPtr<URealtimeVoiceComponent> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Base64Audio]()
	{
		if (URealtimeVoiceComponent* StrongThis = WeakThis.Get())
		{
			TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
			Event->SetStringField(TEXT("type"), TEXT("input_audio_buffer.append"));
			Event->SetStringField(TEXT("audio"), Base64Audio);
			StrongThis->SendJson(Event);
		}
	});
}

// ============================================================
// AIの音声再生
// ============================================================

void URealtimeVoiceComponent::EnsurePlaybackReady()
{
	if (PlaybackSoundWave && PlaybackAudioComponent)
	{
		return;
	}

	PlaybackSoundWave = NewObject<USoundWaveProcedural>(this);
	PlaybackSoundWave->SetSampleRate(RealtimeSampleRate);
	PlaybackSoundWave->NumChannels = 1;
	PlaybackSoundWave->SoundGroup = SOUNDGROUP_Default;
	PlaybackSoundWave->bLooping = false;
	PlaybackSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;

	PlaybackAudioComponent = UGameplayStatics::SpawnSound2D(this, PlaybackSoundWave);
}

void URealtimeVoiceComponent::QueuePlaybackAudio(const TArray<uint8>& Pcm16Bytes)
{
	if (Pcm16Bytes.Num() == 0)
	{
		return;
	}

	EnsurePlaybackReady();

	if (!PlaybackSoundWave)
	{
		return;
	}

	PlaybackSoundWave->QueueAudio(Pcm16Bytes.GetData(), Pcm16Bytes.Num());

	if (PlaybackAudioComponent && !PlaybackAudioComponent->IsPlaying())
	{
		PlaybackAudioComponent->Play();
	}

	// リップシンク用にRMS音量を計算
	const int16* Samples = reinterpret_cast<const int16*>(Pcm16Bytes.GetData());
	const int32 NumSamples = Pcm16Bytes.Num() / sizeof(int16);
	double SumOfSquares = 0.0;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Normalized = Samples[i] / 32768.0f;
		SumOfSquares += static_cast<double>(Normalized) * Normalized;
	}
	const float Rms = NumSamples > 0 ? static_cast<float>(FMath::Sqrt(SumOfSquares / NumSamples)) : 0.0f;
	const float ChunkAmplitude = FMath::Clamp(Rms / 0.3f, 0.0f, 1.0f);

	// このチャンクの実際の再生時間を、マイクミュート延長用の残り時間に加算する。
	// RemainingPlaybackSecondsが0(=新しい発話の最初のチャンク)の場合のみ、
	// 出力デバイス側の遅延を見込んだ安全マージンを一度だけ上乗せする。
	const float ChunkDurationSeconds = static_cast<float>(NumSamples) / static_cast<float>(RealtimeSampleRate);
	if (RemainingPlaybackSeconds <= 0.0f)
	{
		RemainingPlaybackSeconds = ChunkDurationSeconds + MicUnmuteSafetyMarginSeconds;
	}
	else
	{
		RemainingPlaybackSeconds += ChunkDurationSeconds;
	}

	// CurrentAmplitudeを直接書き換えるのではなく、(再生時間, 音量)としてキューに積む。
	// 実際の値への反映はTickComponentで、実時間の経過に合わせて行う
	// (ネットワーク到着タイミングではなく、実際の再生タイミングに同期させるため)
	AmplitudeEnvelopeQueue.Add(TPair<float, float>(ChunkDurationSeconds, ChunkAmplitude));
}

void URealtimeVoiceComponent::StopPlaybackImmediately()
{
	if (PlaybackAudioComponent)
	{
		PlaybackAudioComponent->Stop();
	}

	// Stop()だけではUSoundWaveProceduralの内部キューに残った音声データは消えず、
	// 次にPlay()した瞬間に残り分から再生されてしまうため、明示的にクリアする
	if (PlaybackSoundWave)
	{
		PlaybackSoundWave->ResetAudio();
	}

	CurrentAmplitude = 0.0f;
	AmplitudeEnvelopeQueue.Empty();

	// 再生を止めた = AIの発話が終わった/中断されたとみなし、マイクのミュートを即座に解除する
	// (ユーザーの発話検知による割り込みの場合、これ以上ミュートを続ける理由がないため)
	bIsAssistantSpeaking = false;
	RemainingPlaybackSeconds = 0.0f;

	// キャンセルがサーバーに届くまでの間に飛んでくる残留チャンクを再生しないよう、
	// 次のresponse.createdが来るまで音声を破棄する
	bDiscardIncomingAudioDeltas = true;
}

bool URealtimeVoiceComponent::IsAssistantSpeaking() const
{
	return PlaybackAudioComponent != nullptr && PlaybackAudioComponent->IsPlaying();
}

void URealtimeVoiceComponent::Interrupt()
{
	if (!bIsConnected)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: 手動割り込み(Interrupt)が呼ばれました"));

	// サーバー側に、現在生成中/送信中のレスポンスを打ち切るよう伝える
	TSharedRef<FJsonObject> CancelEvent = MakeShared<FJsonObject>();
	CancelEvent->SetStringField(TEXT("type"), TEXT("response.cancel"));
	SendJson(CancelEvent);

	// ローカルの再生とマイクミュートも即座に解除する
	StopPlaybackImmediately();
	bIsAssistantSpeaking = false;
}
