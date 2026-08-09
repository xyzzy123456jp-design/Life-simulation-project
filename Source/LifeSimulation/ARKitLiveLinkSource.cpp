#include "ARKitLiveLinkSource.h"
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkBasicRole.h"

namespace
{
	// 標準的なARKit互換のブレンドシェイプ(カーブ)名。MetaHumanのFace_AnimBPが
	// "Use ARKit Face"時に期待する入力名と一致させるためのもの。
	static const TArray<FName> GStandardARKitCurveNames = {
		TEXT("eyeBlinkLeft"), TEXT("eyeLookDownLeft"), TEXT("eyeLookInLeft"), TEXT("eyeLookOutLeft"), TEXT("eyeLookUpLeft"), TEXT("eyeSquintLeft"), TEXT("eyeWideLeft"),
		TEXT("eyeBlinkRight"), TEXT("eyeLookDownRight"), TEXT("eyeLookInRight"), TEXT("eyeLookOutRight"), TEXT("eyeLookUpRight"), TEXT("eyeSquintRight"), TEXT("eyeWideRight"),
		TEXT("jawForward"), TEXT("jawLeft"), TEXT("jawRight"), TEXT("jawOpen"),
		TEXT("mouthClose"), TEXT("mouthFunnel"), TEXT("mouthPucker"), TEXT("mouthLeft"), TEXT("mouthRight"),
		TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"), TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
		TEXT("mouthDimpleLeft"), TEXT("mouthDimpleRight"), TEXT("mouthStretchLeft"), TEXT("mouthStretchRight"),
		TEXT("mouthRollLower"), TEXT("mouthRollUpper"), TEXT("mouthShrugLower"), TEXT("mouthShrugUpper"),
		TEXT("mouthPressLeft"), TEXT("mouthPressRight"), TEXT("mouthLowerDownLeft"), TEXT("mouthLowerDownRight"),
		TEXT("mouthUpperUpLeft"), TEXT("mouthUpperUpRight"),
		TEXT("browDownLeft"), TEXT("browDownRight"), TEXT("browInnerUp"), TEXT("browOuterUpLeft"), TEXT("browOuterUpRight"),
		TEXT("cheekPuff"), TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
		TEXT("noseSneerLeft"), TEXT("noseSneerRight"),
		TEXT("tongueOut")
	};
}

FARKitLiveLinkSource::FARKitLiveLinkSource(FName InSubjectName)
	: SubjectName(InSubjectName)
{
	CurveNames = GStandardARKitCurveNames;
	for (const FName& Name : CurveNames)
	{
		CurveValues.Add(Name, 0.0f);
	}
}

void FARKitLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
	SendStaticData();
}

bool FARKitLiveLinkSource::IsSourceStillValid() const
{
	return Client != nullptr;
}

bool FARKitLiveLinkSource::RequestSourceShutdown()
{
	Client = nullptr;
	return true;
}

FText FARKitLiveLinkSource::GetSourceType() const
{
	return FText::FromString(TEXT("LifeSimulation ARKit Bridge"));
}

FText FARKitLiveLinkSource::GetSourceMachineName() const
{
	return FText::FromString(TEXT("Local"));
}

FText FARKitLiveLinkSource::GetSourceStatus() const
{
	return FText::FromString(TEXT("Active"));
}

void FARKitLiveLinkSource::SendStaticData()
{
	if (!Client)
	{
		return;
	}

	// ARKit Face(Apple Live Link Face方式)はカーブのみのシンプルなロールを使う。
	// 骨格を持つULiveLinkAnimationRoleではなく、ULiveLinkBasicRole + 基底の
	// FLiveLinkBaseStaticData/FLiveLinkBaseFrameData(PropertyNames/PropertyValuesのみ)を使う。
	FLiveLinkStaticDataStruct StaticDataStruct(FLiveLinkBaseStaticData::StaticStruct());
	FLiveLinkBaseStaticData* BaseStaticData = StaticDataStruct.Cast<FLiveLinkBaseStaticData>();
	BaseStaticData->PropertyNames = CurveNames;

	Client->PushSubjectStaticData_AnyThread(
		FLiveLinkSubjectKey(SourceGuid, SubjectName),
		ULiveLinkBasicRole::StaticClass(),
		MoveTemp(StaticDataStruct));

	bStaticDataSent = true;
}

void FARKitLiveLinkSource::PushCurveValue(FName CurveName, float Value)
{
	if (!Client)
	{
		return;
	}

	if (!bStaticDataSent)
	{
		SendStaticData();
	}

	CurveValues.FindOrAdd(CurveName) = Value;

	FLiveLinkFrameDataStruct FrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
	FLiveLinkBaseFrameData* BaseFrameData = FrameDataStruct.Cast<FLiveLinkBaseFrameData>();

	BaseFrameData->PropertyValues.Reset(CurveNames.Num());
	for (const FName& Name : CurveNames)
	{
		BaseFrameData->PropertyValues.Add(CurveValues.FindRef(Name));
	}
	BaseFrameData->WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());

	Client->PushSubjectFrameData_AnyThread(
		FLiveLinkSubjectKey(SourceGuid, SubjectName),
		MoveTemp(FrameDataStruct));
}
