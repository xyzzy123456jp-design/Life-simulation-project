#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"

class ILiveLinkClient;

/**
 * MetaHumanのFace_AnimBP(ARKit Face経由)にカーブ値をリアルタイムで配信するための
 * 自前LiveLinkソース。マイク不要・実デバイス不要で、C++コードから直接
 * PushCurveValue()を呼ぶだけでMetaHumanの表情を駆動できる。
 *
 * BP_Payton側の設定が必要:
 * - Use ARKit Face を true にする
 * - ARKit Face Subj に、このソースが使うサブジェクト名(既定 "LifeSimARKitFace")を設定する
 */
class FARKitLiveLinkSource : public ILiveLinkSource, public TSharedFromThis<FARKitLiveLinkSource>
{
public:
	explicit FARKitLiveLinkSource(FName InSubjectName);

	// --- ILiveLinkSource ---
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceStillValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;

	// 指定したARKitカーブ名の値を更新し、フレームデータとしてLiveLinkへ送信する
	void PushCurveValue(FName CurveName, float Value);

private:
	void SendStaticData();

	ILiveLinkClient* Client = nullptr;
	FGuid SourceGuid;
	FName SubjectName;

	// このソースが配信するカーブ名の一覧(標準的なARKit互換の52種)。
	// 実際にはJawOpenだけ動かすが、Face_AnimBP側の入力名と一致させるため
	// 標準セット全体を静的データとして送っておく(未使用分は常に0)
	TArray<FName> CurveNames;
	TMap<FName, float> CurveValues;

	bool bStaticDataSent = false;
};
