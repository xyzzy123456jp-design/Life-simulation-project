#include "ARKitLiveLinkSubsystem.h"
#include "ARKitLiveLinkSource.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
	static TAutoConsoleVariable<float> CVarExpressionInterpSpeed(
		TEXT("expression.ARKitInterpSpeed"), 6.0f,
		TEXT("Jennifer facial-expression interpolation speed"));

	static const TArray<FName> GExpressionCurves = {
		TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"),
		TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
		TEXT("browInnerUp"), TEXT("eyeWideLeft"), TEXT("eyeWideRight"),
		TEXT("browDownLeft"), TEXT("browDownRight"),
		TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
		TEXT("mouthLeft"), TEXT("mouthRight"),
		TEXT("cheekPuff"), TEXT("eyeSquintLeft"), TEXT("eyeSquintRight"),
		TEXT("mouthPressLeft"), TEXT("mouthPressRight")
	};

	static const TSet<FString> GKnownEmotions = {
		TEXT("neutral"), TEXT("happy"), TEXT("surprised"),
		TEXT("sad"), TEXT("confused"), TEXT("embarrassed")
	};

	UARKitLiveLinkSubsystem* GetExpressionSubsystem(UWorld* World)
	{
		if (!World || !World->GetGameInstance())
		{
			return nullptr;
		}
		return World->GetGameInstance()->GetSubsystem<UARKitLiveLinkSubsystem>();
	}

	static FAutoConsoleCommandWithWorldAndArgs GTestExpressionCommand(
		TEXT("TestARKitExpression"),
		TEXT("TestExpression <neutral|happy|surprised|sad|confused|embarrassed> <0..1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] Usage: TestExpression happy 0.7"));
				return;
			}
			if (UARKitLiveLinkSubsystem* Subsystem = GetExpressionSubsystem(World))
			{
				Subsystem->SetExpressionTarget(Args[0].ToLower(), FCString::Atof(*Args[1]));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs GTestFaceCurveCommand(
		TEXT("TestARKitFaceCurve"),
		TEXT("TestFaceCurve <ARKitCurveName> <0..1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] Usage: TestFaceCurve mouthSmileLeft 1.0"));
				return;
			}
			if (UARKitLiveLinkSubsystem* Subsystem = GetExpressionSubsystem(World))
			{
				Subsystem->SetManualCurveTarget(FName(*Args[0]), FCString::Atof(*Args[1]));
			}
		}));
}

void UARKitLiveLinkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		UE_LOG(LogTemp, Warning, TEXT("ARKitLiveLinkSubsystem: LiveLinkClientモジュラーフィーチャーが利用できません"));
		return;
	}

	ILiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	Source = MakeShared<FARKitLiveLinkSource>(GetSubjectName());
	SourceGuid = Client->AddSource(Source);
	InitializeExpressionState(true);
	BlinkTimeUntilNext = FMath::FRandRange(2.0f, 5.0f);

	UE_LOG(LogTemp, Log, TEXT("ARKitLiveLinkSubsystem: LiveLinkソースを登録しました(サブジェクト名=%s)"), *GetSubjectName().ToString());
}

void UARKitLiveLinkSubsystem::Deinitialize()
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		if (SourceGuid.IsValid())
		{
			Client->RemoveSource(SourceGuid);
		}
	}

	Source.Reset();

	Super::Deinitialize();
}

void UARKitLiveLinkSubsystem::PushJawOpen(float Value)
{
	if (Source.IsValid())
	{
		Source->PushCurveValue(TEXT("jawOpen"), Value);
		// ABP_MH_LiveLink(新Face用AnimBP)がDNAのコントロール名を直接期待しているため、
		// 同じ値をCTRL_expressions.jawOpenという名前でも配信しておく
		Source->PushCurveValue(TEXT("CTRL_expressions.jawOpen"), Value);
	}
}

void UARKitLiveLinkSubsystem::InitializeExpressionState(bool bResetCurrent)
{
	for (const FName& Curve : GExpressionCurves)
	{
		ExpressionTargetValues.FindOrAdd(Curve) = 0.0f;
		if (bResetCurrent)
		{
			ExpressionCurrentValues.FindOrAdd(Curve) = 0.0f;
		}
		else
		{
			ExpressionCurrentValues.FindOrAdd(Curve);
		}
	}
	CurrentExpression = TEXT("neutral");
	bExpressionConvergenceLogged = bResetCurrent;
}

bool UARKitLiveLinkSubsystem::IsKnownExpression(const FString& Emotion) const
{
	return GKnownEmotions.Contains(Emotion.ToLower());
}

bool UARKitLiveLinkSubsystem::SetExpressionTarget(const FString& Emotion, float Intensity)
{
	const FString NormalizedEmotion = Emotion.ToLower();
	if (!IsKnownExpression(NormalizedEmotion))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: 未知のemotion \"%s\"、現在の表情を維持"), *Emotion);
		return false;
	}
	if (!FMath::IsFinite(Intensity))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: intensityが非有限値"));
		return false;
	}

	for (const FName& Curve : GExpressionCurves)
	{
		ExpressionTargetValues.FindOrAdd(Curve) = 0.0f;
		ExpressionCurrentValues.FindOrAdd(Curve);
	}

	const float I = FMath::Clamp(Intensity, 0.0f, 1.0f);
	if (NormalizedEmotion == TEXT("happy"))
	{
		ExpressionTargetValues[TEXT("mouthSmileLeft")] = I * 0.70f;
		ExpressionTargetValues[TEXT("mouthSmileRight")] = I * 0.70f;
		ExpressionTargetValues[TEXT("cheekSquintLeft")] = I * 0.30f;
		ExpressionTargetValues[TEXT("cheekSquintRight")] = I * 0.30f;
	}
	else if (NormalizedEmotion == TEXT("surprised"))
	{
		ExpressionTargetValues[TEXT("browInnerUp")] = I * 0.80f;
		ExpressionTargetValues[TEXT("eyeWideLeft")] = I * 0.60f;
		ExpressionTargetValues[TEXT("eyeWideRight")] = I * 0.60f;
	}
	else if (NormalizedEmotion == TEXT("sad"))
	{
		ExpressionTargetValues[TEXT("browDownLeft")] = I * 0.50f;
		ExpressionTargetValues[TEXT("browDownRight")] = I * 0.50f;
		ExpressionTargetValues[TEXT("mouthFrownLeft")] = I * 0.60f;
		ExpressionTargetValues[TEXT("mouthFrownRight")] = I * 0.60f;
	}
	else if (NormalizedEmotion == TEXT("confused"))
	{
		ExpressionTargetValues[TEXT("browDownLeft")] = I * 0.60f;
		ExpressionTargetValues[TEXT("mouthLeft")] = I * 0.40f;
	}
	else if (NormalizedEmotion == TEXT("embarrassed"))
	{
		ExpressionTargetValues[TEXT("cheekPuff")] = I * 0.30f;
		ExpressionTargetValues[TEXT("eyeSquintLeft")] = I * 0.40f;
		ExpressionTargetValues[TEXT("eyeSquintRight")] = I * 0.40f;
		ExpressionTargetValues[TEXT("mouthPressLeft")] = I * 0.30f;
		ExpressionTargetValues[TEXT("mouthPressRight")] = I * 0.30f;
	}

	CurrentExpression = NormalizedEmotion;
	bExpressionConvergenceLogged = false;
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] Target更新: %s, intensity=%.2f"), *NormalizedEmotion, I);
	return true;
}

bool UARKitLiveLinkSubsystem::SetManualCurveTarget(FName CurveName, float Value)
{
	if (!GExpressionCurves.Contains(CurveName) || !FMath::IsFinite(Value))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] 手動カーブが不正または未対応: %s"), *CurveName.ToString());
		return false;
	}
	for (const FName& Curve : GExpressionCurves)
	{
		ExpressionTargetValues.FindOrAdd(Curve) = 0.0f;
		ExpressionCurrentValues.FindOrAdd(Curve);
	}
	ExpressionTargetValues.FindOrAdd(CurveName) = FMath::Clamp(Value, 0.0f, 1.0f);
	CurrentExpression = FString::Printf(TEXT("manual:%s"), *CurveName.ToString());
	bExpressionConvergenceLogged = false;
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] 手動Target更新: %s=%.2f"), *CurveName.ToString(), Value);
	return true;
}

void UARKitLiveLinkSubsystem::TickExpression(float DeltaTime)
{
	if (!Source.IsValid())
	{
		return;
	}

	const float InterpSpeed = FMath::Max(0.01f, CVarExpressionInterpSpeed.GetValueOnGameThread());
	bool bConverged = true;
	for (auto& Pair : ExpressionCurrentValues)
	{
		const float Target = ExpressionTargetValues.FindRef(Pair.Key);
		Pair.Value = FMath::FInterpTo(Pair.Value, Target, DeltaTime, InterpSpeed);
		if (!FMath::IsNearlyEqual(Pair.Value, Target, 0.002f))
		{
			bConverged = false;
		}
		Source->PushCurveValue(Pair.Key, Pair.Value);
	}

	if (bConverged && !bExpressionConvergenceLogged)
	{
		bExpressionConvergenceLogged = true;
		UE_LOG(LogTemp, Log, TEXT("[EXPRESSION] 適用完了: %s"), *CurrentExpression);
	}

	TickBlink(DeltaTime);
}

void UARKitLiveLinkSubsystem::TickBlink(float DeltaTime)
{
	if (!Source.IsValid())
	{
		return;
	}

	float BlinkValue = 0.0f;
	if (!bBlinkInProgress)
	{
		BlinkTimeUntilNext -= DeltaTime;
		if (BlinkTimeUntilNext <= 0.0f)
		{
			bBlinkInProgress = true;
			BlinkElapsed = 0.0f;
		}
	}

	if (bBlinkInProgress)
	{
		BlinkElapsed += DeltaTime;
		if (BlinkElapsed < 0.10f)
		{
			BlinkValue = BlinkElapsed / 0.10f;
		}
		else if (BlinkElapsed < 0.18f)
		{
			BlinkValue = 1.0f;
		}
		else if (BlinkElapsed < 0.32f)
		{
			BlinkValue = 1.0f - ((BlinkElapsed - 0.18f) / 0.14f);
		}
		else
		{
			bBlinkInProgress = false;
			BlinkElapsed = 0.0f;
			BlinkTimeUntilNext = FMath::FRandRange(2.0f, 5.5f);
			BlinkValue = 0.0f;
		}
	}

	Source->PushCurveValue(TEXT("eyeBlinkLeft"), FMath::Clamp(BlinkValue, 0.0f, 1.0f));
	Source->PushCurveValue(TEXT("eyeBlinkRight"), FMath::Clamp(BlinkValue, 0.0f, 1.0f));
}
