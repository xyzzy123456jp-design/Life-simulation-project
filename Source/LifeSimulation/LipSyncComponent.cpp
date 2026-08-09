#include "LipSyncComponent.h"
#include "RealtimeVoiceComponent.h"
#include "TTSComponent.h"
#include "ARKitLiveLinkSubsystem.h"
#include "Kismet/GameplayStatics.h"

ULipSyncComponent::ULipSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULipSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!VoiceSource && !TTSSource)
	{
		UE_LOG(LogTemp, Warning, TEXT("LipSync: VoiceSourceとTTSSourceのどちらも未設定です"));
	}
}

void ULipSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// VoiceSource(新Realtime構成)を優先し、なければTTSSource(旧TTS構成)を使う。
	// VoiceSource側は IsAssistantSpeaking()(内部でPlaybackAudioComponent->IsPlaying()を
	// 参照)によるゲートを使わず、GetCurrentAmplitude()を直接使う。
	// USoundWaveProceduralのIsPlaying()は数秒で不正確にfalseを返すことがある既知の問題があり、
	// 一方でGetCurrentAmplitude()自体はRealtimeVoiceComponent内部で
	// 再生残り時間の推定(RemainingPlaybackSeconds)に基づいて正しく0にリセットされるため、
	// こちらの方が信頼できる
	float TargetAmplitude = 0.0f;
	if (VoiceSource)
	{
		TargetAmplitude = VoiceSource->GetCurrentAmplitude();
	}
	else if (TTSSource)
	{
		TargetAmplitude = TTSSource->IsPlaying() ? TTSSource->GetCurrentAmplitude() : 0.0f;
	}

	const float TargetMorphValue = FMath::Clamp(TargetAmplitude * AmplitudeToMorphScale, 0.0f, 1.0f);

	CurrentMorphValue = FMath::FInterpTo(CurrentMorphValue, TargetMorphValue, DeltaTime, InterpSpeed);

	if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this))
	{
		if (UARKitLiveLinkSubsystem* ARKitSubsystem = GameInstance->GetSubsystem<UARKitLiveLinkSubsystem>())
		{
			ARKitSubsystem->PushJawOpen(CurrentMorphValue);
		}
	}

	if (bShowDebugMessage && GEngine)
	{
		const float RemainingSeconds = VoiceSource ? VoiceSource->GetRemainingPlaybackSeconds() : -1.0f;
		GEngine->AddOnScreenDebugMessage(6001, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("LipSync amp=%.3f jawOpen=%.3f remain=%.2f"), TargetAmplitude, CurrentMorphValue, RemainingSeconds));
	}
}
