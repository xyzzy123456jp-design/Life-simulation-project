#include "ARKitLiveLinkSubsystem.h"
#include "ARKitLiveLinkSource.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"

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
	}
}
