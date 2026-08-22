#include "RealtimeVoiceComponent.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Json.h"
#include "Misc/Base64.h"
#include "ARKitLiveLinkSubsystem.h"
#include "LipSyncComponent.h"
#include "JenniferExpressionInstructions.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"

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

	ClearPendingFunctionCallState();
	ApplyExpression(TEXT("neutral"), 0.0f);

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
	ClearPendingFunctionCallState();
	bNextExpressionCallIsAiTest = false;
	PendingTextExpressionTestRequests = 0;
	PendingTextGestureTestRequests = 0;
}

bool URealtimeVoiceComponent::SendTextMessage(const FString& Text)
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected() || Text.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: input_textを送信できません(未接続または空文字)"));
		return false;
	}

	TSharedRef<FJsonObject> ContentPart = MakeShared<FJsonObject>();
	ContentPart->SetStringField(TEXT("type"), TEXT("input_text"));
	ContentPart->SetStringField(TEXT("text"), Text);
	TArray<TSharedPtr<FJsonValue>> Content;
	Content.Add(MakeShared<FJsonValueObject>(ContentPart));

	TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("type"), TEXT("message"));
	Item->SetStringField(TEXT("role"), TEXT("user"));
	Item->SetArrayField(TEXT("content"), Content);

	TSharedRef<FJsonObject> CreateItemEvent = MakeShared<FJsonObject>();
	CreateItemEvent->SetStringField(TEXT("type"), TEXT("conversation.item.create"));
	CreateItemEvent->SetObjectField(TEXT("item"), Item);
	SendJson(CreateItemEvent);

	if (Text.StartsWith(TEXT("Expression test"), ESearchCase::CaseSensitive))
	{
		++PendingTextExpressionTestRequests;
	}
	if (Text.StartsWith(TEXT("Gesture test"), ESearchCase::CaseSensitive))
	{
		++PendingTextGestureTestRequests;
	}

	// Starts generation for this new user input. The existing response.create
	// after function_call_output remains separately owned by
	// TryFinalizeFunctionResponse().
	SendResponseCreate();
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: input_text送信 + initial response.create: %s"), *Text);
	return true;
}

// ============================================================
// WebSocketイベント
// ============================================================

void URealtimeVoiceComponent::HandleWebSocketConnected()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: WebSocket接続成功"));
	bIsConnected = true;
	ClearPendingFunctionCallState();
	ApplyExpression(TEXT("neutral"), 0.0f);

	SendSessionUpdate();
	StartMicCapture();
	EnsurePlaybackReady();

	OnConnected.Broadcast();
}

void URealtimeVoiceComponent::HandleWebSocketConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeVoice: 接続エラー: %s"), *Error);
	bIsConnected = false;
	ClearPendingFunctionCallState();
	OnError.Broadcast(Error);
}

void URealtimeVoiceComponent::HandleWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: 接続が閉じられました(Status=%d, Reason=%s)"), StatusCode, *Reason);
	bIsConnected = false;
	bIsAssistantSpeaking = false;
	RemainingPlaybackSeconds = 0.0f;
	AmplitudeEnvelopeQueue.Empty();
	ClearPendingFunctionCallState();
	StopMicCapture();
	OnDisconnected.Broadcast(Reason);
}

void URealtimeVoiceComponent::HandleWebSocketMessage(const FString& Message)
{
	// WebSocket実装やプラットフォーム差にかかわらず、UObject/TMap/LiveLinkへ
	// 触る処理は必ずゲームスレッドで行う。
	if (!IsInGameThread())
	{
		TWeakObjectPtr<URealtimeVoiceComponent> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Message]()
		{
			if (URealtimeVoiceComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->HandleWebSocketMessage(Message);
			}
		});
		return;
	}

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
			// Speech transcription may omit or replace punctuation. Require the
			// exact leading words and a boundary, while accepting space/:/,.
			static const FString TestPrefix = TEXT("Expression test");
			bNextExpressionCallIsAiTest = Transcript.StartsWith(TestPrefix, ESearchCase::CaseSensitive)
				&& (Transcript.Len() == TestPrefix.Len()
					|| FChar::IsWhitespace(Transcript[TestPrefix.Len()])
					|| Transcript[TestPrefix.Len()] == TEXT(':')
					|| Transcript[TestPrefix.Len()] == TEXT(','));
			UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: ユーザー発言 -> %s"), *Transcript);
			OnUserTranscript.Broadcast(Transcript);
		}
	}
	else if (EventType == TEXT("input_audio_buffer.speech_started"))
	{
		// response.created直後、マイクミュートが反映される前に残留音がVADへ届き、
		// AI自身の応答を即キャンセルする競合を防ぐ。明示的な割り込みはBキーで行う。
		if (bIsAssistantSpeaking)
		{
			UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: AI応答中の発話誤検知を無視しました"));
			return;
		}
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
	else if (EventType == TEXT("response.function_call_arguments.done"))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[EXPRESSION] Function Call event raw: %s"), *Message);
		HandleFunctionCallArgumentsDone(JsonObject);
	}
	else if (EventType == TEXT("response.done"))
	{
		HandleResponseDoneForFunctionCalls(JsonObject);
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
	// 接続直後の環境ノイズを発話開始として掴んだままspeech_stoppedが来なくなる
	// ケースを防ぐ。通常の会話音声は拾いつつ、常時ノイズは除外する。
	// ローカルゲートと二重に厳しくすると小さい声や語頭を取りこぼすため緩和する。
	// Realtime APIは小数16桁までなので、二進浮動小数点で正確な0.625を使う。
	// 環境ノイズは下のローカルRMSゲートで引き続き除外する。
	TurnDetection->SetNumberField(TEXT("threshold"), 0.625);
	TurnDetection->SetNumberField(TEXT("prefix_padding_ms"), 300);
	TurnDetection->SetNumberField(TEXT("silence_duration_ms"), 700);
	TurnDetection->SetBoolField(TEXT("interrupt_response"), false);

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

	TSharedRef<FJsonObject> EmotionProperties = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> EmotionProperty = MakeShared<FJsonObject>();
	EmotionProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> EmotionEnum;
	for (const TCHAR* Emotion : { TEXT("neutral"), TEXT("happy"), TEXT("surprised"), TEXT("sad"), TEXT("confused"), TEXT("embarrassed") })
	{
		EmotionEnum.Add(MakeShared<FJsonValueString>(Emotion));
	}
	EmotionProperty->SetArrayField(TEXT("enum"), EmotionEnum);
	EmotionProperties->SetObjectField(TEXT("emotion"), EmotionProperty);

	TSharedRef<FJsonObject> IntensityProperty = MakeShared<FJsonObject>();
	IntensityProperty->SetStringField(TEXT("type"), TEXT("number"));
	IntensityProperty->SetStringField(TEXT("description"), TEXT("0.0 (barely noticeable) to 1.0 (very strong)"));
	EmotionProperties->SetObjectField(TEXT("intensity"), IntensityProperty);

	TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
	Parameters->SetStringField(TEXT("type"), TEXT("object"));
	Parameters->SetObjectField(TEXT("properties"), EmotionProperties);
	TArray<TSharedPtr<FJsonValue>> RequiredParameters;
	RequiredParameters.Add(MakeShared<FJsonValueString>(TEXT("emotion")));
	RequiredParameters.Add(MakeShared<FJsonValueString>(TEXT("intensity")));
	Parameters->SetArrayField(TEXT("required"), RequiredParameters);

	TSharedRef<FJsonObject> ExpressEmotionTool = MakeShared<FJsonObject>();
	ExpressEmotionTool->SetStringField(TEXT("type"), TEXT("function"));
	ExpressEmotionTool->SetStringField(TEXT("name"), TEXT("express_emotion"));
	ExpressEmotionTool->SetStringField(TEXT("description"),
		TEXT("Call this whenever your emotional tone meaningfully changes, including when returning to neutral, so your facial expression matches what you say."));
	ExpressEmotionTool->SetObjectField(TEXT("parameters"), Parameters);
	TArray<TSharedPtr<FJsonValue>> Tools;
	Tools.Add(MakeShared<FJsonValueObject>(ExpressEmotionTool));

	TSharedRef<FJsonObject> NodParameters = MakeShared<FJsonObject>();
	NodParameters->SetStringField(TEXT("type"), TEXT("object"));
	NodParameters->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
	NodParameters->SetArrayField(TEXT("required"), {});
	TSharedRef<FJsonObject> NodTool = MakeShared<FJsonObject>();
	NodTool->SetStringField(TEXT("type"), TEXT("function"));
	NodTool->SetStringField(TEXT("name"), TEXT("nod_head"));
	NodTool->SetStringField(TEXT("description"),
		TEXT("Perform one natural head nod to meaningfully acknowledge, agree with, or confirm what the user said."));
	NodTool->SetObjectField(TEXT("parameters"), NodParameters);
	Tools.Add(MakeShared<FJsonValueObject>(NodTool));

	TSharedRef<FJsonObject> GestureProperty = MakeShared<FJsonObject>();
	GestureProperty->SetStringField(TEXT("type"), TEXT("string"));
	GestureProperty->SetArrayField(TEXT("enum"), {
		MakeShared<FJsonValueString>(TEXT("raise_right_arm")),
		MakeShared<FJsonValueString>(TEXT("wave_right")),
		MakeShared<FJsonValueString>(TEXT("shrug_right")),
		MakeShared<FJsonValueString>(TEXT("palm_up_right")) });
	TSharedRef<FJsonObject> GestureProperties = MakeShared<FJsonObject>();
	GestureProperties->SetObjectField(TEXT("gesture"), GestureProperty);
	TSharedRef<FJsonObject> GestureParameters = MakeShared<FJsonObject>();
	GestureParameters->SetStringField(TEXT("type"), TEXT("object"));
	GestureParameters->SetObjectField(TEXT("properties"), GestureProperties);
	GestureParameters->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("gesture")) });
	TSharedRef<FJsonObject> GestureTool = MakeShared<FJsonObject>();
	GestureTool->SetStringField(TEXT("type"), TEXT("function"));
	GestureTool->SetStringField(TEXT("name"), TEXT("trigger_gesture"));
	GestureTool->SetStringField(TEXT("description"), TEXT("Perform one brief natural right-arm conversational gesture."));
	GestureTool->SetObjectField(TEXT("parameters"), GestureParameters);
	Tools.Add(MakeShared<FJsonValueObject>(GestureTool));

	const FString ExpressionInstructions = Instructions + GetJenniferExpressionInstructions() + GetJenniferNodInstructions()
		+ GetJenniferHandGestureInstructions() + TEXT(
		"\n\n"
		"When a user text input begins with \"Expression test\", this is a hidden\n"
		"developer test command, not conversational content. Do not explain it.\n"
		"Do not ask what it means. Do not discuss the command itself.\n\n"
		"Immediately call express_emotion with the requested emotion and intensity.\n"
		"After the tool call, give only a very short response (one short sentence).\n\n"
		"Format: \"Expression test: <emotion>\" or \"Expression test: <emotion> <intensity>\"\n"
		"- If intensity is omitted, use 0.8 (or 0.0 if emotion is \"neutral\").\n"
		"- Valid emotions: neutral, happy, surprised, sad, confused, embarrassed.\n\n"
		"In expression test mode, do not apply automatic expression decay. Keep the\n"
		"requested expression until another Expression test command or an explicit\n"
		"neutral command is received.");

	TSharedRef<FJsonObject> Session = MakeShared<FJsonObject>();
	Session->SetStringField(TEXT("type"), TEXT("realtime"));
	Session->SetArrayField(TEXT("output_modalities"), OutputModalities);
	Session->SetStringField(TEXT("instructions"), ExpressionInstructions);
	Session->SetObjectField(TEXT("audio"), Audio);
	Session->SetArrayField(TEXT("tools"), Tools);
	Session->SetStringField(TEXT("tool_choice"), TEXT("auto"));

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("session.update"));
	Root->SetObjectField(TEXT("session"), Session);

	UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: session voice=%s tools=[express_emotion,nod_head,trigger_gesture] modelは接続URLで指定"), *Voice);
	SendJson(Root);
	UE_LOG(LogTemp, Log, TEXT("RealtimeVoice: session.updateを送信しました"));
}

EExpressionApplyResult URealtimeVoiceComponent::ApplyExpression(const FString& Emotion, float Intensity)
{
	if (!GetWorld())
	{
		return EExpressionApplyResult::InvalidWorld;
	}
	ULipSyncComponent* LipSyncComponent = GetOwner() ? GetOwner()->FindComponentByClass<ULipSyncComponent>() : nullptr;
	if (!LipSyncComponent)
	{
		return EExpressionApplyResult::TargetComponentUnavailable;
	}
	if (!LipSyncComponent->IsKnownExpression(Emotion))
	{
		return EExpressionApplyResult::UnknownEmotion;
	}
	if (!LipSyncComponent->CanApplyExpressions())
	{
		return EExpressionApplyResult::TargetComponentUnavailable;
	}
	if (!LipSyncComponent->SetExpressionTarget(Emotion, Intensity))
	{
		return EExpressionApplyResult::TargetComponentUnavailable;
	}
	return EExpressionApplyResult::Applied;
}

FString URealtimeVoiceComponent::MakeFunctionCallCompletionKey(
	const FString& CallId, const FString& ItemId, const FString& ResponseId, int32 OutputIndex)
{
	if (!CallId.IsEmpty())
	{
		return FString::Printf(TEXT("call:%s"), *CallId);
	}
	if (!ItemId.IsEmpty())
	{
		return FString::Printf(TEXT("item:%s"), *ItemId);
	}
	return FString::Printf(TEXT("response:%s:%d"), *ResponseId, OutputIndex);
}

void URealtimeVoiceComponent::HandleFunctionCallArgumentsDone(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	FString FunctionName;
	FString CallId;
	FString ArgumentsJson;
	FString ResponseId;
	FString ItemId;
	double OutputIndexNumber = 0.0;
	JsonObject->TryGetStringField(TEXT("name"), FunctionName);
	JsonObject->TryGetStringField(TEXT("call_id"), CallId);
	JsonObject->TryGetStringField(TEXT("arguments"), ArgumentsJson);
	JsonObject->TryGetStringField(TEXT("response_id"), ResponseId);
	JsonObject->TryGetStringField(TEXT("item_id"), ItemId);
	JsonObject->TryGetNumberField(TEXT("output_index"), OutputIndexNumber);
	const int32 OutputIndex = static_cast<int32>(OutputIndexNumber);
	const FString CompletionKey = MakeFunctionCallCompletionKey(CallId, ItemId, ResponseId, OutputIndex);

	if (FunctionName == TEXT("nod_head"))
	{
		if (CallId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[NOD][REALTIME] エラー: call_idが空"));
			MarkFunctionCallCompleted(ResponseId, CompletionKey);
			return;
		}
		if (!OnNodRequested.IsBound())
		{
			UE_LOG(LogTemp, Warning, TEXT("[NOD][REALTIME] apply failed: no listener"));
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"target_component_unavailable\"}"));
		}
		else
		{
			OnNodRequested.Broadcast();
			UE_LOG(LogTemp, Log, TEXT("[NOD][REALTIME] tool applied call_id=%s"), *CallId);
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"applied\"}"));
		}
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	if (FunctionName == TEXT("trigger_gesture"))
	{
		if (CallId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[GESTURE][REALTIME] error: empty call_id"));
			MarkFunctionCallCompleted(ResponseId, CompletionKey);
			return;
		}
		TSharedPtr<FJsonObject> GestureArgs;
		const TSharedRef<TJsonReader<>> GestureReader = TJsonReaderFactory<>::Create(ArgumentsJson);
		FString Gesture;
		if (!FJsonSerializer::Deserialize(GestureReader, GestureArgs) || !GestureArgs.IsValid())
		{
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"parse_failed\"}"));
		}
		else if (!GestureArgs->TryGetStringField(TEXT("gesture"), Gesture))
		{
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"missing_argument\"}"));
		}
		else if (!HandGestureExecutor)
		{
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"gesture_backend_unavailable\"}"));
		}
		else
		{
			const bool bAiTest = PendingTextGestureTestRequests > 0;
			if (bAiTest) --PendingTextGestureTestRequests;
			const FString Output = HandGestureExecutor(Gesture.ToLower(), bAiTest);
			UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] tool result gesture=%s call_id=%s output=%s"),
				bAiTest ? TEXT("AI_TEST") : TEXT("REALTIME"), *Gesture.ToLower(), *CallId, *Output);
			SendFunctionCallOutput(CallId, Output);
		}
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	if (FunctionName != TEXT("express_emotion"))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] 未対応Function Call: %s"), *FunctionName);
		if (!CallId.IsEmpty())
		{
			SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"unsupported_function\"}"));
		}
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	if (CallId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: call_idが空。適用とoutput送信をスキップ"));
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	TSharedPtr<FJsonObject> ArgsObject;
	TSharedRef<TJsonReader<>> ArgsReader = TJsonReaderFactory<>::Create(ArgumentsJson);
	if (!FJsonSerializer::Deserialize(ArgsReader, ArgsObject) || !ArgsObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: argumentsのJSONパースに失敗"));
		SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"parse_failed\"}"));
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	FString Emotion;
	double IntensityNumber = 0.0;
	if (!ArgsObject->TryGetStringField(TEXT("emotion"), Emotion)
		|| !ArgsObject->TryGetNumberField(TEXT("intensity"), IntensityNumber))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: 必須argument欠落または型不正"));
		SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"missing_argument\"}"));
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	const float Intensity = static_cast<float>(IntensityNumber);
	if (!FMath::IsFinite(Intensity))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: intensityが非有限値"));
		SendFunctionCallOutput(CallId, TEXT("{\"status\":\"error\",\"reason\":\"invalid_intensity\"}"));
		MarkFunctionCallCompleted(ResponseId, CompletionKey);
		return;
	}

	const float ClampedIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	const bool bTextAiTestSource = PendingTextExpressionTestRequests > 0;
	const bool bAiTestSource = bTextAiTestSource || bNextExpressionCallIsAiTest;
	if (bTextAiTestSource)
	{
		--PendingTextExpressionTestRequests;
	}
	bNextExpressionCallIsAiTest = false;
	const TCHAR* SourceTag = bAiTestSource ? TEXT("AI_TEST") : TEXT("AI");

	FString OutputJson;
	const EExpressionApplyResult ApplyResult = ApplyExpression(Emotion.ToLower(), ClampedIntensity);
	switch (ApplyResult)
	{
	case EExpressionApplyResult::Applied:
		OutputJson = TEXT("{\"status\":\"applied\"}");
		break;
	case EExpressionApplyResult::UnknownEmotion:
		OutputJson = TEXT("{\"status\":\"error\",\"reason\":\"unknown_emotion\"}");
		break;
	case EExpressionApplyResult::SubsystemUnavailable:
		OutputJson = TEXT("{\"status\":\"error\",\"reason\":\"subsystem_unavailable\"}");
		break;
	case EExpressionApplyResult::TargetComponentUnavailable:
		OutputJson = TEXT("{\"status\":\"error\",\"reason\":\"target_component_unavailable\"}");
		break;
	case EExpressionApplyResult::InvalidWorld:
		OutputJson = TEXT("{\"status\":\"error\",\"reason\":\"invalid_world\"}");
		break;
	default:
		OutputJson = TEXT("{\"status\":\"error\",\"reason\":\"apply_failed\"}");
		break;
	}

	const bool bApplied = ApplyResult == EExpressionApplyResult::Applied;
	const FString SourceMessage = FString::Printf(TEXT("[EXPRESSION][%s] %s intensity=%.2f%s"),
		SourceTag, *Emotion.ToLower(), ClampedIntensity, bApplied ? TEXT("") : TEXT(" APPLY_FAILED"));
	UE_LOG(LogTemp, Log, TEXT("%s call_id=%s"), *SourceMessage, *CallId);
	if (GEngine)
	{
		const FColor DisplayColor = bApplied
			? (bAiTestSource ? FColor::Cyan : FColor::Yellow)
			: FColor::Red;
		GEngine->AddOnScreenDebugMessage(9102, 4.0f, DisplayColor, SourceMessage,
			true, FVector2D(1.75f, 1.75f));
	}

	SendFunctionCallOutput(CallId, OutputJson);
	MarkFunctionCallCompleted(ResponseId, CompletionKey);
}

void URealtimeVoiceComponent::HandleResponseDoneForFunctionCalls(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
	if (!JsonObject->TryGetObjectField(TEXT("response"), ResponseObject) || !ResponseObject || !ResponseObject->IsValid())
	{
		return;
	}

	FString ResponseId;
	(*ResponseObject)->TryGetStringField(TEXT("id"), ResponseId);
	if (ResponseId.IsEmpty())
	{
		return;
	}
	FString ResponseStatus;
	(*ResponseObject)->TryGetStringField(TEXT("status"), ResponseStatus);
	if (ResponseStatus != TEXT("completed"))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] response.doneを破棄: response_id=%s status=%s。response.createは送信しません"),
			*ResponseId, ResponseStatus.IsEmpty() ? TEXT("missing") : *ResponseStatus);
		CleanupFunctionResponse(ResponseId);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* OutputItems = nullptr;
	if (!(*ResponseObject)->TryGetArrayField(TEXT("output"), OutputItems) || !OutputItems)
	{
		return;
	}

	FPendingFunctionResponse& Pending = PendingFunctionResponses.FindOrAdd(ResponseId);
	for (int32 Index = 0; Index < OutputItems->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Item = (*OutputItems)[Index].IsValid() ? (*OutputItems)[Index]->AsObject() : nullptr;
		if (!Item.IsValid())
		{
			continue;
		}
		FString Type;
		Item->TryGetStringField(TEXT("type"), Type);
		if (Type != TEXT("function_call"))
		{
			continue;
		}

		FString CallId;
		FString ItemId;
		Item->TryGetStringField(TEXT("call_id"), CallId);
		Item->TryGetStringField(TEXT("id"), ItemId);
		const FString Key = MakeFunctionCallCompletionKey(CallId, ItemId, ResponseId, Index);
		Pending.ExpectedCallKeys.Add(Key);
		if (GloballyCompletedFunctionCallKeys.Contains(Key))
		{
			Pending.CompletedCallKeys.Add(Key);
		}
	}

	if (Pending.ExpectedCallKeys.Num() == 0)
	{
		CleanupFunctionResponse(ResponseId);
		return;
	}

	Pending.bResponseDoneReceived = true;
	// Function CallのみのResponseではoutput_audio.doneが来ないため、ここでサーバー側の
	// 発話中フラグを解除し、output送信後の次Responseを待つ。
	bIsAssistantSpeaking = false;
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] response.done: response_id=%s calls=%d completed=%d"),
		*ResponseId, Pending.ExpectedCallKeys.Num(), Pending.CompletedCallKeys.Num());
	TryFinalizeFunctionResponse(ResponseId);
}

void URealtimeVoiceComponent::SendFunctionCallOutput(const FString& CallId, const FString& OutputJson)
{
	if (CallId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("type"), TEXT("function_call_output"));
	Item->SetStringField(TEXT("call_id"), CallId);
	Item->SetStringField(TEXT("output"), OutputJson);

	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("type"), TEXT("conversation.item.create"));
	Event->SetObjectField(TEXT("item"), Item);
	SendJson(Event);
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] function_call_output送信: call_id=%s output=%s"), *CallId, *OutputJson);
}

void URealtimeVoiceComponent::SendResponseCreate()
{
	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("type"), TEXT("response.create"));
	SendJson(Event);
}

void URealtimeVoiceComponent::MarkFunctionCallCompleted(const FString& ResponseId, const FString& CompletionKey)
{
	GloballyCompletedFunctionCallKeys.Add(CompletionKey);
	if (!ResponseId.IsEmpty())
	{
		FPendingFunctionResponse& Pending = PendingFunctionResponses.FindOrAdd(ResponseId);
		Pending.CompletedCallKeys.Add(CompletionKey);
		TryFinalizeFunctionResponse(ResponseId);
	}
}

void URealtimeVoiceComponent::TryFinalizeFunctionResponse(const FString& ResponseId)
{
	FPendingFunctionResponse* Pending = PendingFunctionResponses.Find(ResponseId);
	if (!Pending || !Pending->bResponseDoneReceived || Pending->bResponseCreateSent
		|| Pending->ExpectedCallKeys.Num() == 0)
	{
		return;
	}

	for (const FString& ExpectedKey : Pending->ExpectedCallKeys)
	{
		if (!Pending->CompletedCallKeys.Contains(ExpectedKey))
		{
			return;
		}
	}

	Pending->bResponseCreateSent = true;
	SendResponseCreate();
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] 全Function Call完了。response.create送信: response_id=%s"), *ResponseId);
	CleanupFunctionResponse(ResponseId);
}

void URealtimeVoiceComponent::CleanupFunctionResponse(const FString& ResponseId)
{
	if (FPendingFunctionResponse* Pending = PendingFunctionResponses.Find(ResponseId))
	{
		for (const FString& Key : Pending->ExpectedCallKeys)
		{
			GloballyCompletedFunctionCallKeys.Remove(Key);
		}
		for (const FString& Key : Pending->CompletedCallKeys)
		{
			GloballyCompletedFunctionCallKeys.Remove(Key);
		}
	}
	PendingFunctionResponses.Remove(ResponseId);
}

void URealtimeVoiceComponent::ClearPendingFunctionCallState()
{
	PendingFunctionResponses.Empty();
	GloballyCompletedFunctionCallKeys.Empty();
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

	// WASAPI開始直後の最初のバッファはOverflowと大きなスパイクを含むことがあり、
	// server_vadが発話開始のまま固まる原因になる。各キャプチャ開始後1秒だけ破棄する。
	static double CaptureStartStreamTime = -1.0;
	if (CaptureStartStreamTime < 0.0 || StreamTime < CaptureStartStreamTime)
	{
		CaptureStartStreamTime = StreamTime;
	}
	if (StreamTime - CaptureStartStreamTime < 1.0)
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

	// 実測で無音時RMSが0.02～0.05あり、server_vadがspeech_stoppedを返せないため、
	// ローカルで無音ノイズだけをゼロ化する。発話末尾を切らないよう350msの保持時間を設ける。
	double GateSumSquares = 0.0;
	for (const float Sample : MonoSamples)
	{
		GateSumSquares += static_cast<double>(Sample) * static_cast<double>(Sample);
	}
	const double GateRms = MonoSamples.IsEmpty() ? 0.0 : FMath::Sqrt(GateSumSquares / MonoSamples.Num());
	static double LastLocalVoiceStreamTime = -1000.0;
	// 無音時の上限約0.05より少し上に置きつつ、従来0.065で落ちていた小声を通す。
	if (GateRms >= 0.055)
	{
		LastLocalVoiceStreamTime = StreamTime;
	}
	else if (StreamTime - LastLocalVoiceStreamTime > 0.35)
	{
		for (float& Sample : MonoSamples)
		{
			Sample = 0.0f;
		}
	}

	// 【診断】サーバーへ送る直前のマイク信号を1秒ごとに測る。
	// 入力デバイスや音声変換が常時大音量になっていないかを実測するためのログ。
	static double LastMicLevelLogTime = -1.0;
	if (LastMicLevelLogTime < 0.0 || StreamTime - LastMicLevelLogTime >= 1.0)
	{
		double SumSquares = 0.0;
		float Peak = 0.0f;
		for (const float Sample : MonoSamples)
		{
			SumSquares += static_cast<double>(Sample) * static_cast<double>(Sample);
			Peak = FMath::Max(Peak, FMath::Abs(Sample));
		}
		const double Rms = MonoSamples.IsEmpty() ? 0.0 : FMath::Sqrt(SumSquares / MonoSamples.Num());
		UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: MicLevel RMS=%.6f Peak=%.6f Rate=%d Channels=%d Overflow=%s"),
			Rms, Peak, InSampleRate, InNumChannels, bOverflow ? TEXT("true") : TEXT("false"));
		LastMicLevelLogTime = StreamTime;
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

	UE_LOG(LogTemp, Warning, TEXT("RealtimeVoice: 再生コンポーネント作成 PlaybackVolumeMultiplier=%.2f"), PlaybackVolumeMultiplier);
	PlaybackAudioComponent = UGameplayStatics::SpawnSound2D(this, PlaybackSoundWave, PlaybackVolumeMultiplier);
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
