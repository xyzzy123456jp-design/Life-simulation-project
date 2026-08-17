#include "ChatManager.h"
#include "Json.h"
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
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("https://api.openai.com/v1/chat/completions"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField(TEXT("model"), TEXT("gpt-4o"));

	TArray<TSharedPtr<FJsonValue>> MessagesArray;

	// Realtime方式と共通のJennifer設定を使用する。
	TSharedPtr<FJsonObject> SystemMessageObject = MakeShareable(new FJsonObject);
	SystemMessageObject->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessageObject->SetStringField(TEXT("content"), SystemInstructions.IsEmpty()
		? TEXT("You are Jennifer. Reply naturally in English in 1-3 short spoken-style sentences.")
		: SystemInstructions);
	MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessageObject)));

	TSharedPtr<FJsonObject> MessageObject = MakeShareable(new FJsonObject);
	MessageObject->SetStringField(TEXT("role"), TEXT("user"));
	MessageObject->SetStringField(TEXT("content"), UserText);
	MessagesArray.Add(MakeShareable(new FJsonValueObject(MessageObject)));

	RootObject->SetArrayField(TEXT("messages"), MessagesArray);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindUObject(this, &AChatManager::OnResponseReceived);
	Request->ProcessRequest();
}

void AChatManager::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("HTTP request failed!"));
		}
		OnChatResponseReceived.Broadcast(TEXT("(通信エラー：返答を取得できませんでした)"));
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
			TSharedPtr<FJsonObject> MessageObj = FirstChoice->GetObjectField(TEXT("message"));
			FString ReplyText = MessageObj->GetStringField(TEXT("content"));

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::White, ReplyText);
			}

			OnChatResponseReceived.Broadcast(ReplyText);
			return;
		}
	}

	OnChatResponseReceived.Broadcast(TEXT("(返答の解析に失敗しました)"));
}
