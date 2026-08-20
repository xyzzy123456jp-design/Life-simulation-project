#include "LipSyncComponent.h"
#include "RealtimeVoiceComponent.h"
#include "TTSComponent.h"
#include "ARKitLiveLinkSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

namespace
{
	static TAutoConsoleVariable<float> CVarDirectExpressionInterpSpeed(
		TEXT("expression.InterpSpeed"), 6.0f,
		TEXT("Jennifer facial-expression interpolation speed"));

	static const TArray<FName> GDirectExpressionMorphs = {
		TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"),
		TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
		TEXT("browInnerUp"), TEXT("eyeWideLeft"), TEXT("eyeWideRight"),
		TEXT("browDownLeft"), TEXT("browDownRight"),
		TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
		TEXT("mouthLeft"), TEXT("mouthRight"),
		TEXT("cheekPuff"), TEXT("eyeSquintLeft"), TEXT("eyeSquintRight"),
		TEXT("mouthPressLeft"), TEXT("mouthPressRight")
	};

	static const TSet<FString> GDirectKnownEmotions = {
		TEXT("neutral"), TEXT("happy"), TEXT("surprised"),
		TEXT("sad"), TEXT("confused"), TEXT("embarrassed")
	};

	ULipSyncComponent* FindLipSyncComponent(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TObjectIterator<ULipSyncComponent> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static FAutoConsoleCommandWithWorldAndArgs GDirectTestExpressionCommand(
		TEXT("TestExpression"),
		TEXT("TestExpression <neutral|happy|surprised|sad|confused|embarrassed> <0..1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] Usage: TestExpression happy 0.7"));
				return;
			}
			if (ULipSyncComponent* Component = FindLipSyncComponent(World))
			{
				Component->SetExpressionTarget(Args[0].ToLower(), FCString::Atof(*Args[1]));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GDirectTestFaceCurveCommand(
		TEXT("TestFaceCurve"),
		TEXT("TestFaceCurve <MorphTargetName> <0..1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] Usage: TestFaceCurve mouthSmileLeft 1.0"));
				return;
			}
			if (ULipSyncComponent* Component = FindLipSyncComponent(World))
			{
				Component->SetManualExpressionCurveTarget(FName(*Args[0]), FCString::Atof(*Args[1]));
			}
		}));
}

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
			FString::Printf(TEXT("LipSync amp=%.3f jawOpen=%.3f remain=%.2f"), TargetAmplitude, CurrentMorphValue, RemainingSeconds),
			true, FVector2D(1.75f, 1.75f));
	}

	// Blinkはemotionの収束状態とは独立して毎フレーム更新する。
	TickBlink(DeltaTime);
	TickExpression(DeltaTime);
}

void ULipSyncComponent::SetExpressionFaceMesh(USkeletalMeshComponent* InFaceMesh)
{
	if (ExpressionFaceMesh && bBlinkAvailable)
	{
		ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkLeft"), 0.0f, false);
		ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkRight"), 0.0f, false);
	}
	ExpressionFaceMesh = InFaceMesh;
	ExpressionCurrentValues.Empty();
	ExpressionTargetValues.Empty();
	bExpressionBackendAvailable = false;
	bExpressionConverged = true;
	bLoggedMissingExpressionMorphs = false;
	bBlinkAvailable = ExpressionFaceMesh
		&& ExpressionFaceMesh->FindMorphTarget(TEXT("eyeBlinkLeft"))
		&& ExpressionFaceMesh->FindMorphTarget(TEXT("eyeBlinkRight"));
	BlinkPhase = EBlinkPhase::Waiting;
	BlinkPhaseElapsed = 0.0f;
	if (bBlinkAvailable)
	{
		ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkLeft"), 0.0f, false);
		ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkRight"), 0.0f, false);
		ScheduleNextBlink();
		UE_LOG(LogTemp, Log, TEXT("[BLINK] available mesh=%s first_interval=%.2f"),
			*ExpressionFaceMesh->GetName(), BlinkTimeUntilNext);
	}
	else
	{
		if (!bLoggedMissingBlinkMorphs)
		{
			bLoggedMissingBlinkMorphs = true;
			UE_LOG(LogTemp, Warning, TEXT("[BLINK] unavailable: required blink morphs not found mesh=%s"),
				ExpressionFaceMesh ? *ExpressionFaceMesh->GetName() : TEXT("null"));
		}
	}
	TArray<FString> MissingMorphNames;
	for (const FName& Morph : GDirectExpressionMorphs)
	{
		if (ExpressionFaceMesh && ExpressionFaceMesh->FindMorphTarget(Morph))
		{
			ExpressionCurrentValues.Add(Morph, 0.0f);
			ExpressionTargetValues.Add(Morph, 0.0f);
		}
		else
		{
			MissingMorphNames.Add(Morph.ToString());
		}
	}
	bExpressionBackendAvailable = ExpressionFaceMesh != nullptr && MissingMorphNames.Num() == 0;
	if (!bExpressionBackendAvailable)
	{
		bLoggedMissingExpressionMorphs = true;
		UE_LOG(LogTemp, Error, TEXT("[EXPRESSION] Direct Morph利用不可。不足Morph(%d): %s"),
			MissingMorphNames.Num(), *FString::Join(MissingMorphNames, TEXT(", ")));
	}
	UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] Direct Morph適用先=%s 対応表情Morph=%d"),
		ExpressionFaceMesh ? *ExpressionFaceMesh->GetName() : TEXT("null"), ExpressionCurrentValues.Num());
}

bool ULipSyncComponent::CanApplyExpressions() const
{
	return bExpressionBackendAvailable;
}

bool ULipSyncComponent::IsKnownExpression(const FString& Emotion) const
{
	return GDirectKnownEmotions.Contains(Emotion.ToLower());
}

bool ULipSyncComponent::SetExpressionTarget(const FString& Emotion, float Intensity)
{
	const FString NormalizedEmotion = Emotion.ToLower();
	if (!IsKnownExpression(NormalizedEmotion))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] 未知emotion \"%s\"、現在の表情を維持"), *Emotion);
		return false;
	}
	if (!CanApplyExpressions())
	{
		if (!bLoggedMissingExpressionMorphs)
		{
			bLoggedMissingExpressionMorphs = true;
			UE_LOG(LogTemp, Error, TEXT("[EXPRESSION] 必須表情Morphが不足しているためDirect Morphを適用できません"));
		}
		return false;
	}

	for (auto& Pair : ExpressionTargetValues)
	{
		Pair.Value = 0.0f;
	}
	const float I = FMath::Clamp(Intensity, 0.0f, 1.0f);
	auto SetIfAvailable = [this](FName Morph, float Value)
	{
		if (float* Target = ExpressionTargetValues.Find(Morph))
		{
			*Target = Value;
		}
	};

	if (NormalizedEmotion == TEXT("happy"))
	{
		SetIfAvailable(TEXT("mouthSmileLeft"), I * 0.70f);
		SetIfAvailable(TEXT("mouthSmileRight"), I * 0.70f);
		SetIfAvailable(TEXT("cheekSquintLeft"), I * 0.30f);
		SetIfAvailable(TEXT("cheekSquintRight"), I * 0.30f);
	}
	else if (NormalizedEmotion == TEXT("surprised"))
	{
		SetIfAvailable(TEXT("browInnerUp"), I * 0.80f);
		SetIfAvailable(TEXT("eyeWideLeft"), I * 0.60f);
		SetIfAvailable(TEXT("eyeWideRight"), I * 0.60f);
	}
	else if (NormalizedEmotion == TEXT("sad"))
	{
		SetIfAvailable(TEXT("browDownLeft"), I * 0.50f);
		SetIfAvailable(TEXT("browDownRight"), I * 0.50f);
		SetIfAvailable(TEXT("mouthFrownLeft"), I * 0.60f);
		SetIfAvailable(TEXT("mouthFrownRight"), I * 0.60f);
	}
	else if (NormalizedEmotion == TEXT("confused"))
	{
		SetIfAvailable(TEXT("browDownLeft"), I * 0.60f);
		SetIfAvailable(TEXT("mouthLeft"), I * 0.40f);
	}
	else if (NormalizedEmotion == TEXT("embarrassed"))
	{
		SetIfAvailable(TEXT("cheekPuff"), I * 0.30f);
		SetIfAvailable(TEXT("eyeSquintLeft"), I * 0.40f);
		SetIfAvailable(TEXT("eyeSquintRight"), I * 0.40f);
		SetIfAvailable(TEXT("mouthPressLeft"), I * 0.30f);
		SetIfAvailable(TEXT("mouthPressRight"), I * 0.30f);
	}

	CurrentExpression = NormalizedEmotion;
	bExpressionConverged = false;
	bExpressionConvergenceLogged = false;
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] Direct Morph Target更新: %s intensity=%.2f"), *NormalizedEmotion, I);
	return true;
}

bool ULipSyncComponent::SetManualExpressionCurveTarget(FName MorphName, float Value)
{
	if (MorphName == TEXT("eyeBlinkLeft") || MorphName == TEXT("eyeBlinkRight"))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BLINK] manual expression rejected: blink Morph is owned by TickBlink (%s)"),
			*MorphName.ToString());
		return false;
	}
	if (!ExpressionFaceMesh || !ExpressionFaceMesh->FindMorphTarget(MorphName))
	{
		UE_LOG(LogTemp, Error, TEXT("[EXPRESSION] 表示顔にMorphが存在しません: %s"), *MorphName.ToString());
		return false;
	}
	for (auto& Pair : ExpressionTargetValues)
	{
		Pair.Value = 0.0f;
	}
	ExpressionCurrentValues.FindOrAdd(MorphName) = ExpressionFaceMesh->GetMorphTarget(MorphName);
	ExpressionTargetValues.FindOrAdd(MorphName) = FMath::Clamp(Value, 0.0f, 1.0f);
	CurrentExpression = FString::Printf(TEXT("manual:%s"), *MorphName.ToString());
	bExpressionConverged = false;
	bExpressionConvergenceLogged = false;
	return true;
}

void ULipSyncComponent::ScheduleNextBlink()
{
	BlinkTimeUntilNext = FMath::FRandRange(
		FMath::Min(BlinkIntervalMin, BlinkIntervalMax),
		FMath::Max(BlinkIntervalMin, BlinkIntervalMax));
}

void ULipSyncComponent::TickBlink(float DeltaTime)
{
	if (!ExpressionFaceMesh || !bBlinkAvailable)
	{
		return;
	}

	float BlinkValue = 0.0f;
	switch (BlinkPhase)
	{
	case EBlinkPhase::Waiting:
		BlinkTimeUntilNext -= DeltaTime;
		if (BlinkTimeUntilNext > 0.0f)
		{
			return;
		}
		CurrentBlinkCloseTime = FMath::FRandRange(
			FMath::Min(BlinkCloseTimeMin, BlinkCloseTimeMax),
			FMath::Max(BlinkCloseTimeMin, BlinkCloseTimeMax));
		CurrentBlinkHoldTime = FMath::FRandRange(
			FMath::Min(BlinkHoldTimeMin, BlinkHoldTimeMax),
			FMath::Max(BlinkHoldTimeMin, BlinkHoldTimeMax));
		CurrentBlinkOpenTime = FMath::FRandRange(
			FMath::Min(BlinkOpenTimeMin, BlinkOpenTimeMax),
			FMath::Max(BlinkOpenTimeMin, BlinkOpenTimeMax));
		BlinkPhase = EBlinkPhase::Closing;
		BlinkPhaseElapsed = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("[BLINK] start close=%.3f hold=%.3f open=%.3f"),
			CurrentBlinkCloseTime, CurrentBlinkHoldTime, CurrentBlinkOpenTime);
		break;

	case EBlinkPhase::Closing:
		BlinkPhaseElapsed += DeltaTime;
		BlinkValue = FMath::Clamp(BlinkPhaseElapsed / FMath::Max(CurrentBlinkCloseTime, 0.01f), 0.0f, 1.0f);
		if (BlinkPhaseElapsed >= CurrentBlinkCloseTime)
		{
			BlinkPhase = EBlinkPhase::Closed;
			BlinkPhaseElapsed = 0.0f;
			BlinkValue = 1.0f;
		}
		break;

	case EBlinkPhase::Closed:
		BlinkPhaseElapsed += DeltaTime;
		BlinkValue = 1.0f;
		if (BlinkPhaseElapsed >= CurrentBlinkHoldTime)
		{
			BlinkPhase = EBlinkPhase::Opening;
			BlinkPhaseElapsed = 0.0f;
		}
		break;

	case EBlinkPhase::Opening:
		BlinkPhaseElapsed += DeltaTime;
		BlinkValue = 1.0f - FMath::Clamp(BlinkPhaseElapsed / FMath::Max(CurrentBlinkOpenTime, 0.01f), 0.0f, 1.0f);
		if (BlinkPhaseElapsed >= CurrentBlinkOpenTime)
		{
			BlinkValue = 0.0f;
			BlinkPhase = EBlinkPhase::Waiting;
			BlinkPhaseElapsed = 0.0f;
			ScheduleNextBlink();
			UE_LOG(LogTemp, Log, TEXT("[BLINK] end next_interval=%.2f"), BlinkTimeUntilNext);
		}
		break;
	}

	// Blink Morphはemotion Morph群に含まれないため、この処理だけが最終値を所有する。
	// 両目を同じ0..1値へClampして同期させる。
	BlinkValue = FMath::Clamp(BlinkValue, 0.0f, 1.0f);
	ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkLeft"), BlinkValue, false);
	ExpressionFaceMesh->SetMorphTarget(TEXT("eyeBlinkRight"), BlinkValue, false);
}

void ULipSyncComponent::TickExpression(float DeltaTime)
{
	if (!ExpressionFaceMesh || ExpressionCurrentValues.Num() == 0 || bExpressionConverged)
	{
		return;
	}
	const float Speed = FMath::Max(0.01f, CVarDirectExpressionInterpSpeed.GetValueOnGameThread());
	bool bConverged = true;
	for (auto& Pair : ExpressionCurrentValues)
	{
		const float Target = ExpressionTargetValues.FindRef(Pair.Key);
		Pair.Value = FMath::FInterpTo(Pair.Value, Target, DeltaTime, Speed);
		const bool bMorphConverged = FMath::IsNearlyEqual(Pair.Value, Target, 0.002f);
		if (bMorphConverged)
		{
			Pair.Value = Target;
		}
		ExpressionFaceMesh->SetMorphTarget(Pair.Key, Pair.Value, false);
		bConverged &= bMorphConverged;
	}
	if (bConverged)
	{
		bExpressionConverged = true;
		bExpressionConvergenceLogged = true;
		UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] Direct Morph適用完了: %s"), *CurrentExpression);
	}
}
