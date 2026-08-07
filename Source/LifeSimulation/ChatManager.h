#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Http.h"
#include "ChatManager.generated.h"

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

protected:
	virtual void BeginPlay() override;

	void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void OnTestKeyPressed();
};