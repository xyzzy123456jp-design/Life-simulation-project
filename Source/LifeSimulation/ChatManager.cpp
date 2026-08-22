#include "ChatManager.h"
#include "Json.h"
#include "LipSyncComponent.h"
#include "JenniferExpressionInstructions.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

AChatManager::AChatManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AChatManager::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, TEXT("ChatManager BeginPlay called!"));
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		EnableInput(PC);
	}

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AChatManager::OnTestKeyPressed);
	}
}

void AChatManager::OnTestKeyPressed()
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green, TEXT("Enter pressed! Sending..."));
	}
	SendMessage(TEXT("こんにちは"));
}

void AChatManager::SendMessage(const FString& UserText)
{
	ToolRoundCount = 0;
	ChatRequestRound = 0;
	ChatTurnStartTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] chat_start"));
	EnsureSystemMessage();

	TSharedPtr<FJsonObject> UserMessageObject = MakeShared<FJsonObject>();
	UserMessageObject->SetStringField(TEXT("role"), TEXT("user"));
	UserMessageObject->SetStringField(TEXT("content"), UserText);
	PendingMessages.Add(MakeShared<FJsonValueObject>(UserMessageObject));

	SendChatRequest();
}

void AChatManager::EnsureSystemMessage()
{
	if (PendingMessages.Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstMessage = PendingMessages[0].IsValid()
			? PendingMessages[0]->AsObject()
			: nullptr;
		FString FirstRole;
		if (FirstMessage.IsValid()
			&& FirstMessage->TryGetStringField(TEXT("role"), FirstRole)
			&& FirstRole == TEXT("system"))
		{
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[CHAT][LEGACY] 履歴先頭にsystemがないため履歴を再初期化します"));
		PendingMessages.Reset();
	}

	TSharedPtr<FJsonObject> SystemMessageObject = MakeShared<FJsonObject>();
	SystemMessageObject->SetStringField(TEXT("role"), TEXT("system"));
	const FString BaseInstructions = SystemInstructions.IsEmpty()
		? TEXT("You are Jennifer. Reply naturally in English in 1-3 short spoken-style sentences.")
		: SystemInstructions;
	SystemMessageObject->SetStringField(TEXT("content"), BaseInstructions + GetJenniferExpressionInstructions()
		+ GetJenniferNodInstructions() + GetJenniferHandGestureInstructions());
	PendingMessages.Add(MakeShared<FJsonValueObject>(SystemMessageObject));
}

void AChatManager::TrimConversationHistory()
{
	// この処理は最終assistant回答が確定したターン境界でのみ呼ぶ。
	// userから次のuser直前までを1ターンとして扱い、tool往復を分断しない。
	TArray<int32> UserMessageIndices;
	for (int32 Index = 1; Index < PendingMessages.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Message = PendingMessages[Index].IsValid()
			? PendingMessages[Index]->AsObject()
			: nullptr;
		FString MessageRole;
		if (Message.IsValid()
			&& Message->TryGetStringField(TEXT("role"), MessageRole)
			&& MessageRole == TEXT("user"))
		{
			UserMessageIndices.Add(Index);
		}
	}

	if (UserMessageIndices.Num() <= MaxConversationTurns)
	{
		return;
	}

	const int32 FirstKeptUserIndex = UserMessageIndices[UserMessageIndices.Num() - MaxConversationTurns];
	PendingMessages.RemoveAt(1, FirstKeptUserIndex - 1, EAllowShrinking::No);
	UE_LOG(LogTemp, Verbose, TEXT("[CHAT][LEGACY] 会話履歴を直近%dターンへ切り詰めました(messages=%d)"),
		MaxConversationTurns, PendingMessages.Num());
}

void AChatManager::SendChatRequest()
{
	++ChatRequestRound;
	ChatRequestStartTimeSeconds = FPlatformTime::Seconds();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("https://api.openai.com/v1/chat/completions"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField(TEXT("model"), TEXT("gpt-4o"));

	RootObject->SetArrayField(TEXT("messages"), PendingMessages);

	TSharedRef<FJsonObject> EmotionProperty = MakeShared<FJsonObject>();
	EmotionProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> EmotionValues;
	for (const TCHAR* Emotion : { TEXT("neutral"), TEXT("happy"), TEXT("surprised"), TEXT("sad"), TEXT("confused"), TEXT("embarrassed") })
	{
		EmotionValues.Add(MakeShared<FJsonValueString>(Emotion));
	}
	EmotionProperty->SetArrayField(TEXT("enum"), EmotionValues);

	TSharedRef<FJsonObject> IntensityProperty = MakeShared<FJsonObject>();
	IntensityProperty->SetStringField(TEXT("type"), TEXT("number"));
	IntensityProperty->SetNumberField(TEXT("minimum"), 0.0);
	IntensityProperty->SetNumberField(TEXT("maximum"), 1.0);

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("emotion"), EmotionProperty);
	Properties->SetObjectField(TEXT("intensity"), IntensityProperty);
	TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
	Parameters->SetStringField(TEXT("type"), TEXT("object"));
	Parameters->SetObjectField(TEXT("properties"), Properties);
	Parameters->SetArrayField(TEXT("required"), {
		MakeShared<FJsonValueString>(TEXT("emotion")),
		MakeShared<FJsonValueString>(TEXT("intensity")) });

	TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
	Function->SetStringField(TEXT("name"), TEXT("express_emotion"));
	Function->SetStringField(TEXT("description"),
		TEXT("Set Jennifer's facial expression to match her meaningful emotional tone change."));
	Function->SetObjectField(TEXT("parameters"), Parameters);
	TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
	Tool->SetStringField(TEXT("type"), TEXT("function"));
	Tool->SetObjectField(TEXT("function"), Function);

	TSharedRef<FJsonObject> NodParameters = MakeShared<FJsonObject>();
	NodParameters->SetStringField(TEXT("type"), TEXT("object"));
	NodParameters->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
	NodParameters->SetArrayField(TEXT("required"), {});
	TSharedRef<FJsonObject> NodFunction = MakeShared<FJsonObject>();
	NodFunction->SetStringField(TEXT("name"), TEXT("nod_head"));
	NodFunction->SetStringField(TEXT("description"),
		TEXT("Perform one natural head nod to meaningfully acknowledge, agree with, or confirm what the user said."));
	NodFunction->SetObjectField(TEXT("parameters"), NodParameters);
	TSharedRef<FJsonObject> NodTool = MakeShared<FJsonObject>();
	NodTool->SetStringField(TEXT("type"), TEXT("function"));
	NodTool->SetObjectField(TEXT("function"), NodFunction);

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
	TSharedRef<FJsonObject> GestureFunction = MakeShared<FJsonObject>();
	GestureFunction->SetStringField(TEXT("name"), TEXT("trigger_gesture"));
	GestureFunction->SetStringField(TEXT("description"), TEXT("Perform one brief natural right-arm conversational gesture."));
	GestureFunction->SetObjectField(TEXT("parameters"), GestureParameters);
	TSharedRef<FJsonObject> GestureTool = MakeShared<FJsonObject>();
	GestureTool->SetStringField(TEXT("type"), TEXT("function"));
	GestureTool->SetObjectField(TEXT("function"), GestureFunction);
	RootObject->SetArrayField(TEXT("tools"), {
		MakeShared<FJsonValueObject>(Tool),
		MakeShared<FJsonValueObject>(NodTool),
		MakeShared<FJsonValueObject>(GestureTool) });
	RootObject->SetStringField(TEXT("tool_choice"), TEXT("auto"));

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindUObject(this, &AChatManager::OnResponseReceived);
	if (!Request->ProcessRequest())
	{
		OnChatResponseReceived.Broadcast(TEXT("(通信エラー：リクエストを開始できませんでした)"));
	}
}

void AChatManager::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	const double RequestElapsedSeconds = ChatRequestStartTimeSeconds > 0.0
		? FPlatformTime::Seconds() - ChatRequestStartTimeSeconds
		: 0.0;
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] chat_request_done round=%d elapsed=%.3f sec"),
		ChatRequestRound, RequestElapsedSeconds);

	if (!bWasSuccessful || !Response.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("HTTP request failed!"));
		}
		OnChatResponseReceived.Broadcast(TEXT("(通信エラー：返答を取得できませんでした)"));
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHAT][LEGACY] HTTP %d: %s"),
			ResponseCode, *Response->GetContentAsString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				FString::Printf(TEXT("Legacy Chat HTTP error: %d"), ResponseCode));
		}
		OnChatResponseReceived.Broadcast(TEXT("(通信エラー：AI APIがエラーを返しました)"));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Choices;
		if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
		{
			TSharedPtr<FJsonObject> FirstChoice = (*Choices)[0]->AsObject();
			const TSharedPtr<FJsonObject> MessageObj = FirstChoice->GetObjectField(TEXT("message"));
			const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
			if (MessageObj->TryGetArrayField(TEXT("tool_calls"), ToolCalls) && ToolCalls && ToolCalls->Num() > 0)
			{
				if (++ToolRoundCount > MaxToolRounds)
				{
					UE_LOG(LogTemp, Error, TEXT("[EXPRESSION][LEGACY] tool call回数上限を超えました"));
					OnChatResponseReceived.Broadcast(TEXT("(ツール呼び出しが完了しませんでした)"));
					return;
				}

				// 空のtool_call_idはtool resultと対応付けられず、次のAPI要求を壊すため、
				// 表情適用や履歴追加を行う前に全callを検査する。
				for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
				{
					const TSharedPtr<FJsonObject> ToolCall = ToolCallValue.IsValid() ? ToolCallValue->AsObject() : nullptr;
					FString ToolCallId;
					if (!ToolCall.IsValid()
						|| !ToolCall->TryGetStringField(TEXT("id"), ToolCallId)
						|| ToolCallId.IsEmpty())
					{
						UE_LOG(LogTemp, Error, TEXT("[CHAT][LEGACY] tool_call_idが空のためtool callを中止しました"));
						OnChatResponseReceived.Broadcast(TEXT("(ツール呼び出しIDが不正でした)"));
						return;
					}
				}

				// Chat Completionsではassistantのtool_callsメッセージをそのまま履歴へ戻す。
				PendingMessages.Add(MakeShared<FJsonValueObject>(MessageObj));
				for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
				{
					const TSharedPtr<FJsonObject> ToolCall = ToolCallValue.IsValid() ? ToolCallValue->AsObject() : nullptr;
					FString ToolCallId;
					FString ToolName;
					FString ArgumentsJson;
					const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
					if (ToolCall.IsValid())
					{
						ToolCall->TryGetStringField(TEXT("id"), ToolCallId);
						if (ToolCall->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject && FunctionObject->IsValid())
						{
							(*FunctionObject)->TryGetStringField(TEXT("name"), ToolName);
							(*FunctionObject)->TryGetStringField(TEXT("arguments"), ArgumentsJson);
						}
					}

					FString ToolResult;
					if (ToolName == TEXT("express_emotion"))
					{
						ToolResult = ExecuteExpressionTool(ArgumentsJson);
					}
					else if (ToolName == TEXT("nod_head"))
					{
						ToolResult = ExecuteNodTool();
					}
					else if (ToolName == TEXT("trigger_gesture"))
					{
						ToolResult = ExecuteHandGestureTool(ArgumentsJson);
					}
					else
					{
						ToolResult = TEXT("{\"status\":\"error\",\"reason\":\"unsupported_function\"}");
					}
					TSharedPtr<FJsonObject> ToolMessage = MakeShared<FJsonObject>();
					ToolMessage->SetStringField(TEXT("role"), TEXT("tool"));
					ToolMessage->SetStringField(TEXT("tool_call_id"), ToolCallId);
					ToolMessage->SetStringField(TEXT("content"), ToolResult);
					PendingMessages.Add(MakeShared<FJsonValueObject>(ToolMessage));
				}
				const double EmotionElapsedSeconds = ChatTurnStartTimeSeconds > 0.0
					? FPlatformTime::Seconds() - ChatTurnStartTimeSeconds
					: 0.0;
				UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] emotion_done elapsed=%.3f sec"),
					EmotionElapsedSeconds);
				SendChatRequest();
				return;
			}

			FString ReplyText;
			if (!MessageObj->TryGetStringField(TEXT("content"), ReplyText) || ReplyText.IsEmpty())
			{
				OnChatResponseReceived.Broadcast(TEXT("(最終回答が空でした)"));
				return;
			}

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::White, ReplyText);
			}

			// 最終assistant回答も次ターンの文脈として保持する。
			PendingMessages.Add(MakeShared<FJsonValueObject>(MessageObj));
			TrimConversationHistory();
			const double ChatElapsedSeconds = ChatTurnStartTimeSeconds > 0.0
				? FPlatformTime::Seconds() - ChatTurnStartTimeSeconds
				: 0.0;
			UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] chat_done elapsed=%.3f sec rounds=%d"),
				ChatElapsedSeconds, ChatRequestRound);
			OnChatResponseReceived.Broadcast(ReplyText);
			return;
		}
	}

	OnChatResponseReceived.Broadcast(TEXT("(返答の解析に失敗しました)"));
}

FString AChatManager::ExecuteExpressionTool(const FString& ArgumentsJson)
{
	TSharedPtr<FJsonObject> Arguments;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
	if (!FJsonSerializer::Deserialize(Reader, Arguments) || !Arguments.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"parse_failed\"}");
	}

	FString Emotion;
	double IntensityNumber = 0.0;
	if (!Arguments->TryGetStringField(TEXT("emotion"), Emotion)
		|| !Arguments->TryGetNumberField(TEXT("intensity"), IntensityNumber))
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"missing_argument\"}");
	}
	if (!FMath::IsFinite(IntensityNumber))
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"invalid_intensity\"}");
	}

	Emotion = Emotion.ToLower();
	const float RequestedIntensity = FMath::Clamp(static_cast<float>(IntensityNumber), 0.0f, 1.0f);
	// Legacy表情経路の配線・見た目を切り分けるための診断用下限。
	// neutral、Realtime、Morph係数には影響させない。
	const float Intensity = Emotion == TEXT("neutral")
		? RequestedIntensity
		: FMath::Max(RequestedIntensity, 0.8f);
	FString ErrorReason;
	if (!ExpressionComponent)
	{
		ErrorReason = TEXT("target_component_unavailable");
	}
	else if (!ExpressionComponent->IsKnownExpression(Emotion))
	{
		ErrorReason = TEXT("unknown_emotion");
	}
	else if (!ExpressionComponent->CanApplyExpressions()
		|| !ExpressionComponent->SetExpressionTarget(Emotion, Intensity))
	{
		ErrorReason = TEXT("target_component_unavailable");
	}

	const bool bApplied = ErrorReason.IsEmpty();
	if (bApplied)
	{
		// TTSには顔用minimum 0.8ではなく、AIが生成した元の強度を渡す。
		CurrentLegacyEmotion = Emotion;
		CurrentLegacyEmotionIntensity = RequestedIntensity;
	}
	const FString FailureSuffix = bApplied ? TEXT("") : FString::Printf(TEXT(" APPLY_FAILED(%s)"), *ErrorReason);
	const FString IntensityDetails = FMath::IsNearlyEqual(RequestedIntensity, Intensity)
		? FString::Printf(TEXT("intensity=%.2f"), Intensity)
		: FString::Printf(TEXT("intensity=%.2f (AI=%.2f, diagnostic minimum)"), Intensity, RequestedIntensity);
	const FString Message = FString::Printf(TEXT("[EXPRESSION][LEGACY] %s %s%s"),
		*Emotion, *IntensityDetails, *FailureSuffix);
	if (bApplied)
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9104, 4.0f, bApplied ? FColor::Purple : FColor::Red,
			Message, true, FVector2D(1.75f, 1.75f));
	}
	return bApplied
		? TEXT("{\"status\":\"applied\"}")
		: FString::Printf(TEXT("{\"status\":\"error\",\"reason\":\"%s\"}"), *ErrorReason);
}

FString AChatManager::ExecuteNodTool()
{
	if (!OnNodRequested.IsBound())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NOD][LEGACY] apply failed: no listener"));
		return TEXT("{\"status\":\"error\",\"reason\":\"target_component_unavailable\"}");
	}

	OnNodRequested.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[NOD][LEGACY] tool applied"));
	return TEXT("{\"status\":\"applied\"}");
}

FString AChatManager::ExecuteHandGestureTool(const FString& ArgumentsJson)
{
	TSharedPtr<FJsonObject> Arguments;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
	if (!FJsonSerializer::Deserialize(Reader, Arguments) || !Arguments.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"parse_failed\"}");
	}
	FString Gesture;
	if (!Arguments->TryGetStringField(TEXT("gesture"), Gesture))
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"missing_argument\"}");
	}
	if (!HandGestureExecutor)
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"gesture_backend_unavailable\"}");
	}
	const FString Result = HandGestureExecutor(Gesture.ToLower(), false);
	UE_LOG(LogTemp, Log, TEXT("[GESTURE][LEGACY] tool result gesture=%s output=%s"), *Gesture.ToLower(), *Result);
	return Result;
}
