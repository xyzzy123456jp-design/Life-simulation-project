#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Http.h"
#include "Dom/JsonValue.h"
#include "ChatManager.generated.h"

class ULipSyncComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatResponseReceived, const FString&, ResponseText);

UCLASS()
class LIFESIMULATION_API AChatManager : public AActor
{
	GENERATED_BODY()

public:
	AChatManager();

	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SendMessage(const FString& UserText);

	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FOnChatResponseReceived OnChatResponseReceived;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString ApiKey;

	// Legacy方式にもRealtime方式と同じJenniferの人物設定を渡す。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat")
	FString SystemInstructions;

	// Legacy Chat Completionsのtool callから使う共通表情適用先。
	UPROPERTY(Transient)
	ULipSyncComponent* ExpressionComponent = nullptr;

	// Legacy TTSへ渡す、顔用下限適用前のAI生成値。
	FString GetCurrentLegacyEmotion() const { return CurrentLegacyEmotion; }
	float GetCurrentLegacyEmotionIntensity() const { return CurrentLegacyEmotionIntensity; }

protected:
	virtual void BeginPlay() override;

	void SendChatRequest();
	void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	FString ExecuteExpressionTool(const FString& ArgumentsJson);
	void EnsureSystemMessage();
	void TrimConversationHistory();
	void OnTestKeyPressed();

	TArray<TSharedPtr<FJsonValue>> PendingMessages;
	int32 ToolRoundCount = 0;
	static constexpr int32 MaxToolRounds = 4;
	static constexpr int32 MaxConversationTurns = 8;
	FString CurrentLegacyEmotion = TEXT("neutral");
	float CurrentLegacyEmotionIntensity = 0.0f;
	double ChatTurnStartTimeSeconds = 0.0;
	double ChatRequestStartTimeSeconds = 0.0;
	int32 ChatRequestRound = 0;
};
