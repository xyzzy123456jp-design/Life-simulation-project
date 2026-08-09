#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RealtimeTestActor.generated.h"

class URealtimeVoiceComponent;
class ACameraActor;
class ULipSyncComponent;
class APawn;

/**
 * RealtimeVoiceComponentの動作確認用テストActor。
 * BeginPlayから自動的に接続し、あとは常時マイクが開きっぱなしで会話できる。
 * レベルに1つ配置するだけで動作確認できる。
 * Pキーで、AIが喋っている最中に手動で割り込める。
 * 起動時に視点を顔アップカメラへ切り替え、キャラクターを常にカメラの方へ向かせる。
 */
UCLASS()
class LIFESIMULATION_API ARealtimeTestActor : public AActor
{
	GENERATED_BODY()

public:
	ARealtimeTestActor();

	// OpenAIのAPIキー
	UPROPERTY(EditAnywhere, Category = "RealtimeTest")
	FString ApiKey;

	// AIキャラクター本体(MetaHumanのアクター)。起動時の向き調整に使う
	UPROPERTY(EditAnywhere, Category = "RealtimeTest")
	AActor* CharacterActor;

	// 起動時に視点を切り替える「顔アップ」用のカメラ(レベルにCameraActorを配置して設定)
	UPROPERTY(EditAnywhere, Category = "RealtimeTest")
	ACameraActor* IntroFaceCamera;

	// MetaHumanのメッシュ前方向とActor本体の前方向がズレている場合の補正角度(度)。
	// 90, -90, 180などを試して、実際に正面を向く値を見つけて設定する。
	UPROPERTY(EditAnywhere, Category = "RealtimeTest")
	float FacingYawOffset = 0.0f;

	// VRでIntroFaceCameraをビューターゲットにする際、そのZ座標から
	// 差し引く「目の高さ」推定値(cm)。VRのHMDトラッキングはビューターゲットの
	// ワールド座標に実際の頭の高さを上乗せして描画するため、そのままだと
	// カメラが高すぎる位置になってしまう。これを補正するためのオフセット。
	// 実際に被って試しながら、ちょうどよい値に調整すること。
	UPROPERTY(EditAnywhere, Category = "RealtimeTest")
	float VREyeHeightOffsetCm = 170.0f;

	// 【車モード】Paytonを車のポーンに対してどれだけずらして助手席に座らせるか(車のローカル座標系)
	UPROPERTY(EditAnywhere, Category = "VehicleMode")
	FVector VehiclePassengerSeatOffset = FVector(0.0f, -50.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "VehicleMode")
	FRotator VehiclePassengerSeatRotationOffset = FRotator::ZeroRotator;

	// GameModeの自動スポーンが一定時間たっても成功しない場合に、代わりに
	// 自分でスポーン&憑依させる車のクラス(未設定ならこのフォールバックは行わない)
	UPROPERTY(EditAnywhere, Category = "VehicleMode")
	TSubclassOf<APawn> VehiclePawnClassFallback;

	// VRでコックピットカメラ(FrontCamera)を使う際、そのZ座標から差し引く
	// 「目の高さ」補正値(cm)。VRのHMDトラッキングはビューターゲットのワールド座標に
	// 実際の頭の高さを上乗せして描画するため、そのままだと高すぎる位置になる。
	// 被って試しながら調整すること(矢印キーでの微調整は非VR時のみ行うこと)
	UPROPERTY(EditAnywhere, Category = "VehicleMode")
	float VehicleCockpitVREyeHeightOffsetCm = 100.0f;

	// 【シーン切り替え】部屋でキャラクターが座る位置(レベルに空のアクター、
	// 例えばTarget Pointを配置してソファの位置・向きに合わせて設定する)
	UPROPERTY(EditAnywhere, Category = "SceneSwitch")
	AActor* RoomCharacterSeat;

	// 【シーン切り替え】部屋でプレイヤーが座る位置(同上。ソファのプレイヤー側)
	UPROPERTY(EditAnywhere, Category = "SceneSwitch")
	AActor* RoomPlayerSeat;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "RealtimeTest")
	URealtimeVoiceComponent* RealtimeVoice;

	UPROPERTY(VisibleAnywhere, Category = "RealtimeTest")
	ULipSyncComponent* LipSync;

	void HandleInterruptKeyPressed();

	// 【シーン切り替え】Mキーで車モード⇔部屋モードをトグルする
	void HandleToggleSceneModeKeyPressed();

	// プレイヤー・キャラクターをVRの目の高さ補正込みで指定の位置・向きへ移動させる
	void TeleportPlayerPawnTo(const FVector& Location, const FRotator& Rotation);
	void TeleportCharacterActorTo(const FVector& Location, const FRotator& Rotation);

	// 【車モード】Paytonを車の助手席位置にアタッチする
	void AttachCharacterToVehicle();

	// 【車モード】車のコックピットカメラ(FrontCamera)を有効化し、後方カメラ(BackCamera)を無効化する
	void ActivateVehicleCockpitCamera();

	// 名前でカメラコンポーネントを検索するヘルパー
	static class UCameraComponent* FindCameraComponentByName(AActor* Actor, FName ComponentName);

	// 車のポーン(実行時にGameModeがスポーンするので、BeginPlayで取得してキャッシュする)
	UPROPERTY()
	AActor* VehiclePawn = nullptr;

	// BeginPlay時点ではまだ車のポーンがスポーン/憑依されていないことがあるため、
	// 見つかるまで一定間隔で再試行する
	FTimerHandle VehicleSetupRetryTimerHandle;
	void TrySetupVehicleMode();
	int32 VehicleSetupRetryCount = 0;

	// 【調整用】コックピットカメラ(FrontCamera)の「親」(FrontSpringArm)への直接参照。
	// VRではFrontCamera自身の位置はbLockToHmdにより毎フレームHMDトラッキングで
	// 上書きされてしまうため、位置調整は必ず親(FrontSpringArm)側に対して行う
	UPROPERTY()
	class USceneComponent* CachedFrontCameraMount = nullptr;

	// 名前でシーンコンポーネントを検索するヘルパー(FrontSpringArm等、カメラ以外用)
	static class USceneComponent* FindSceneComponentByName(AActor* Actor, FName ComponentName);

	// 【調整用】Paytonがどこにいても暗く潰れないよう、常について回る複数のライトを用意する
	UPROPERTY()
	TArray<class UPointLightComponent*> PaytonFillLights;
	void EnsurePaytonFillLight();

	// 部屋モードでは元々照明があるため、車モードの時だけこのライトを点ける
	void SetPaytonFillLightsEnabled(bool bEnabled);

	// 【調整用】車内(ダッシュボード周り)が暗すぎるため、車自体にも常時ライトを付ける
	UPROPERTY()
	TArray<class UPointLightComponent*> CockpitFillLights;
	void EnsureCockpitFillLights();

	// 車のBlueprint自身がこの位置を毎フレーム上書きしてくることがあるため、
	// こちらも毎フレーム、望みの位置を強制的に再設定する(Tickから呼ぶ)
	FVector DesiredFrontCameraOffset = FVector::ZeroVector;
	void EnforceFrontCameraOffset();

	// 【調整用】矢印キー/PageUp/PageDownでコックピットカメラの位置を1回押しごとに動かす
	void NudgeCockpitCamera(const FVector& LocalDelta);
	void HandleCockpitNudgeForward();
	void HandleCockpitNudgeBackward();
	void HandleCockpitNudgeLeft();
	void HandleCockpitNudgeRight();
	void HandleCockpitNudgeUp();
	void HandleCockpitNudgeDown();

	// 【調整用】[ ]キーでVRの目の高さ補正を1回押しごとに調整する
	void AdjustVREyeHeightOffset(float DeltaCm);
	void HandleVREyeHeightIncrease();
	void HandleVREyeHeightDecrease();

	// 【VR】Homeキーで、VRのセンター位置(向き・立ち位置の基準)をリセットする
	void HandleResetVRCenter();

	// Escapeキーでゲームを終了する
	void HandleQuitGame();

	// 現在「部屋モード」かどうか(false=車モード、初期状態)
	bool bIsInRoomMode = false;

	UFUNCTION()
	void HandleConnected();

	UFUNCTION()
	void HandleDisconnected(const FString& Reason);

	UFUNCTION()
	void HandleError(const FString& ErrorMessage);

	UFUNCTION()
	void HandleUserTranscript(const FString& Text);

	UFUNCTION()
	void HandleAssistantTranscript(const FString& Text);

	UFUNCTION()
	void HandleUserStartedSpeaking();
};
