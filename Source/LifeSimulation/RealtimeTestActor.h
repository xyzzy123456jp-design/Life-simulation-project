#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RealtimeTestActor.generated.h"

class URealtimeVoiceComponent;
class ACameraActor;
class ULipSyncComponent;
class UMicRecorderComponent;
class UWhisperTranscriberComponent;
class UTTSComponent;
class AChatManager;
class APawn;
class USkeletalMesh;
class UStaticMesh;
class UAnimInstance;
struct FKeyEvent;

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

	// Paytonの外見はここから差し替える。変更後のC++ビルドは不要。
	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	TObjectPtr<USkeletalMesh> PaytonFaceMesh;

	// 顔を含まない髪専用Static Mesh。BP_Paytonの既存StaticMeshコンポーネントへ設定する。
	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	TObjectPtr<UStaticMesh> PaytonHairMesh;

	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	TSubclassOf<UAnimInstance> PaytonFaceAnimClass;

	// 以前の顔+髪一体Static Meshを隠し、口が動くSkeletal Faceを表示する。
	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	bool bUseAnimatedPaytonFace = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "RealtimeTest")
	URealtimeVoiceComponent* RealtimeVoice;

	UPROPERTY(VisibleAnywhere, Category = "RealtimeTest")
	ULipSyncComponent* LipSync;

	// 通常時に使う従来の音声会話（録音 -> Whisper -> Chat -> TTS）。
	UPROPERTY(VisibleAnywhere, Category = "LegacyVoice")
	UMicRecorderComponent* LegacyMicRecorder;
	UPROPERTY(VisibleAnywhere, Category = "LegacyVoice")
	UWhisperTranscriberComponent* LegacyWhisper;
	UPROPERTY(VisibleAnywhere, Category = "LegacyVoice")
	UTTSComponent* LegacyTTS;
	UPROPERTY()
	AChatManager* LegacyChatManager = nullptr;

	FTimerHandle LegacyStartRecordingTimerHandle;
	FTimerHandle LegacyRecordingMonitorTimerHandle;
	bool bLegacyVoiceEnabled = true;
	bool bLegacySpeechDetected = false;
	float LegacySilenceElapsed = 0.0f;
	float LegacyRecordingElapsed = 0.0f;
	float LegacyVadDiagnosticElapsed = 0.0f;
	double LegacySpeechEndTimeSeconds = 0.0;
	void StartLegacyVoice();
	void StopLegacyVoice();
	void StartLegacyRecording();
	void CheckLegacyRecording();
	void StopAndSubmitLegacyRecording();
	UFUNCTION() void HandleLegacyTranscriptionComplete(const FString& Text);
	UFUNCTION() void HandleLegacyTranscriptionFailed(const FString& ErrorMessage);
	UFUNCTION() void HandleLegacyChatResponse(const FString& Text);
	UFUNCTION() void HandleLegacyTTSStarted();
	UFUNCTION() void HandleLegacyTTSFinished();
	UFUNCTION() void HandleLegacyTTSFailed(const FString& ErrorMessage);

	void HandleInterruptKeyPressed();

	// 【表情診断】EキーでDirect Morph表情をAPIを通さず順番に切り替える。
	void HandleCycleExpressionKeyPressed();
	int32 DebugExpressionCycleIndex = 0;
	void HandleCycleExpressionTestKeyPressed();
	int32 DebugExpressionTestCycleIndex = 0;

	enum class ENodPhase : uint8
	{
		Idle,
		NodDown,
		Hold,
		Return
	};
	UFUNCTION(Exec)
	void TestNod();
	UFUNCTION() void HandleLegacyNodRequested();
	UFUNCTION() void HandleRealtimeNodRequested();
	void StartNod(const TCHAR* SourceTag);
	void TickTestNod(float DeltaTime);
	FString ActiveNodSource = TEXT("MANUAL");
	ENodPhase NodPhase = ENodPhase::Idle;
	float NodPhaseElapsed = 0.0f;
	float CurrentNodPitchDegrees = 0.0f;
	// Tool Callが省略された明確な同意文だけを補完し、同一応答で二重にうなずかない。
	bool bNodTriggeredForCurrentAssistantResponse = false;
	// Crimson FBXのComponent Spaceでは負のX回転が「下を向く」方向。
	static constexpr float NodTargetPitchDegrees = -7.0f;
	static constexpr float NodDownDurationSeconds = 0.18f;
	static constexpr float NodHoldDurationSeconds = 0.06f;
	static constexpr float NodReturnDurationSeconds = 0.24f;

	// 【手動ジェスチャーv1】MetaHuman BodyのAnimBP最終姿勢へ、右腕の
	// Component Space加算offsetを適用する。AI連動・自動ループは行わない。
	enum class EHandGesturePhase : uint8
	{
		Idle,
		Raise,
		Hold,
		Lower
	};
	void SetupHandGesture();
	void ResetHandGesture();
	void HandleBodyBoneTransformsFinalized();
	void ApplyRightArmGestureOffset(float Alpha);
	void TickHandGesture(float DeltaTime);
	float GetActiveHandGestureHoldSeconds() const;
	UFUNCTION(Exec) void TestGesture();
	void HandleCycleGestureKeyPressed();
	void HandleCycleGestureAiTestKeyPressed();
	UFUNCTION() void HandleRealtimeAssistantStartedSpeaking();
	FString RequestHandGesture(const FString& GestureId, const TCHAR* SourceTag);
	void StartHandGestureNow(const FString& GestureId, const FString& SourceTag);
	void StartPendingHandGestureAtPlayback();
	FString ActiveHandGestureSource = TEXT("MANUAL");
	FString PendingHandGestureSource;
	FString PendingHandGestureId;
	FString ActiveHandGestureId = TEXT("raise_right_arm");
	int32 DebugGestureCycleIndex = 0;
	int32 DebugGestureAiTestCycleIndex = 0;
	bool bHandGestureTriggeredForCurrentAssistantResponse = false;
	bool ShouldUseHandGestureFallback(const FString& AssistantText) const;
	UPROPERTY(Transient) class USkeletalMeshComponent* CachedBodyComponent = nullptr;
	FDelegateHandle BodyBoneTransformsFinalizedHandle;
	EHandGesturePhase HandGesturePhase = EHandGesturePhase::Idle;
	float HandGesturePhaseElapsed = 0.0f;
	float HandGestureAlpha = 0.0f;
	static constexpr float HandGestureRaiseSeconds = 0.30f;
	static constexpr float HandGestureHoldSeconds = 0.24f;
	static constexpr float HandGestureLowerSeconds = 0.36f;
	static constexpr float HandGestureUpperArmDegrees = -18.0f;
	static constexpr float HandGestureLowerArmDegrees = -28.0f;
	// 14度では前腕の回転に埋もれて見えなかったため、診断可能な角度へ拡大。
	static constexpr float HandGestureWristDegrees = 35.0f;

	// 【コスト対策】F9キーでRealtime API(音声会話)への接続/切断をトグルする
	void HandleToggleRealtimeVoiceKeyPressed();

#if WITH_EDITOR
	void HandleSlatePreInputKeyDown(const FKeyEvent& KeyEvent);
	FDelegateHandle SlatePreInputKeyDownHandle;
#endif

	// 【シーン切り替え】Mキーで車モード⇔部屋モードをトグルする
	void HandleToggleSceneModeKeyPressed();
	void HandleForceMyRoomKeyPressed();
	void HandleForceClassroomKeyPressed();
	void HandleForceCinemaKeyPressed();
	void HandleForceDriveKeyPressed();
	void HandleForceJenniferRoomKeyPressed();
	void HandleForceWalkKeyPressed();
	void HandleForceRestaurantKeyPressed();

	// 【会話による場所移動】プレイヤーの移動提案を記憶し、Jenniferが明確に
	// 同意した場合だけ対応する場所へ移動する。
	enum class EConversationLocation : uint8
	{
		None,
		MyRoom,
		Classroom,
		Cinema,
		Drive,
		JenniferRoom,
		Walk,
		Restaurant
	};

	EConversationLocation DetectProposedLocation(const FString& UserText) const;
	bool IsJenniferAgreement(const FString& AssistantText) const;
	bool IsExplicitRejection(const FString& AssistantText) const;
	void TryMoveToConversationLocation(EConversationLocation Location);

	// 「はい」等の同意テキストが確定した時点では、まだ音声の再生が始まっていない/残っている
	// ことが多い。実際に喋り終わるまで一定間隔でチェックしてから、シーンを切り替える。
	// Realtime接続中はRealtime側、従来方式ではLegacy TTS側の再生状態を見る。
	FTimerHandle PendingLocationMoveTimerHandle;
	EConversationLocation PendingLocationMoveTarget = EConversationLocation::None;
	float PendingLocationMoveElapsedSeconds = 0.0f;
	// 「喋り終わった」と判定してからの継続時間。音声チャンクの間の一瞬の途切れで
	// 誤判定しないよう、一定時間連続で静かだった場合だけ確定する。
	float PendingLocationMoveQuietSeconds = 0.0f;
	void CheckPendingLocationMove();
	bool IsCurrentVoiceStillSpeaking() const;
	void BuildFallbackConversationScenes();
	void BuildFallbackConversationScene(EConversationLocation Location, const FVector& Origin);
	AActor* FindConversationSceneAnchor(EConversationLocation Location, bool bPlayerAnchor) const;
	static FString GetConversationLocationDisplayName(EConversationLocation Location);
	static FString GetConversationLocationTagStem(EConversationLocation Location);

	EConversationLocation PendingProposedLocation = EConversationLocation::None;
	// Jenniferがすぐに明確なYes/Noを返さず、聞き返す等した場合に何ターンまで
	// 提案を保持しておくか(0になったら諦めて破棄する)
	int32 PendingProposalTurnsRemaining = 0;
	EConversationLocation CurrentConversationLocation = EConversationLocation::MyRoom;

	// プレイヤー・キャラクターをVRの目の高さ補正込みで指定の位置・向きへ移動させる
	void TeleportPlayerPawnTo(const FVector& Location, const FRotator& Rotation);
	void TeleportCharacterActorTo(const FVector& Location, const FRotator& Rotation);
	FVector ResolveJenniferFaceTarget() const;
	void CaptureCanonicalConversationFraming();
	void ApplyConversationSceneExposure(EConversationLocation Location);
	void RefreshClassroomJenniferLightingAfterMove();
	void LogConversationCameraDiagnostics(EConversationLocation Location);
	void LogRenderEnvironmentDiagnostics(EConversationLocation Location) const;
	void LogCrimsonRenderDiagnostics(EConversationLocation Location) const;
	void CaptureJenniferCanonicalScale();
	void RestoreJenniferCanonicalScale(const TCHAR* Context);
	FVector JenniferCanonicalActorScale = FVector::OneVector;
	bool bHasJenniferCanonicalActorScale = false;
	float CanonicalConversationCameraDistanceCm = 270.0f;
	float CanonicalConversationCameraFOVDegrees = 90.0f;
	bool bHasCanonicalConversationFraming = false;

	// 【車モード】Paytonを車の助手席位置にアタッチする
	void AttachCharacterToVehicle();

	// 【口パク修正】PaytonのFaceメッシュを、MetaHuman Character Editorで正式に
	// アセンブルしたSKM_Payton_FB_Character_FaceMeshに差し替え、対応するAnimClassを
	// Face_AnimBPではなくABP_MH_LiveLinkに設定し、LiveLink購読先をLifeSimARKitFaceに
	// 設定する。SKM_Payton_FB_Character_FaceMeshはFace_AnimBPが前提とするSkeletonと
	// 非互換のため、専用のABP_MH_LiveLinkを使う必要がある
	void SetupPaytonNewFace();

	// 【口パク修正】ABP_MH_LiveLinkのAnimInstance(SetupPaytonNewFaceでキャッシュ)への参照。
	// LiveLink経由のカーブ解釈に頼らず、毎フレームSet Control関数を直接呼ぶために使う
	UPROPERTY()
	UAnimInstance* CachedFaceAnimInstance = nullptr;

	// 【口パク修正】実際にRigLogicを実行しているPost-Process AnimInstanceへの参照。
	// メイン側のOverrideCurveValueだけでは反映されないため、こちらにも直接書き込む
	UPROPERTY()
	class UAnimInstance* CachedFacePostProcessInstance = nullptr;

	// 【口パク修正】Face_AnimBPのJawOpenAlpha変数(Control Rigへの直接入力)への
	// リフレクション経由の書き込みに使う
	class FDoubleProperty* CachedJawOpenAlphaProperty = nullptr;

	// 【口パク修正】JawOpenAlpha書き込み直後に強制的にアニメーションを再評価させるため、
	// FaceのSkeletalMeshComponentをキャッシュしておく
	UPROPERTY()
	class USkeletalMeshComponent* CachedFaceComponent = nullptr;

	// 元FBXの見た目をそのまま使い、追加したjaw骨だけを音声で動かす表示コンポーネント。
	UPROPERTY()
	class UPoseableMeshComponent* OriginalPaytonPoseableMesh = nullptr;
	class USkeletalMeshComponent* OriginalPaytonMorphMesh = nullptr;
	UPROPERTY()
	class UStaticMeshComponent* OriginalPaytonMouthComponent = nullptr;
	UPROPERTY()
	class UStaticMeshComponent* OriginalPaytonUpperTeethComponent = nullptr;
	UPROPERTY()
	class UStaticMeshComponent* PaytonV9FaceComponent = nullptr;
	UPROPERTY()
	class UStaticMeshComponent* PaytonV9HairComponent = nullptr;
	UPROPERTY()
	class UJenniferNodSkeletalMeshComponent* CrimsonGazeMorphComponent = nullptr;
	UPROPERTY()
	class UStaticMeshComponent* CrimsonGazeUpperTeethComponent = nullptr;

	// 歯(Static Mesh)は口の開閉モーフに追従しないため、口の開き具合(0〜1)に応じて
	// 疑似的に位置をずらして動いて見せるためのオフセット量(cm、口が全開の時の値)
	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	float CrimsonGazeTeethOpenOffsetForward = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Payton Appearance")
	float CrimsonGazeTeethOpenOffsetDown = 0.5f;

	bool bShowingPaytonV9 = false;
	void HandleTogglePaytonV9();

	FTransform OriginalPaytonJawReferenceTransform = FTransform::Identity;
	bool bHasOriginalPaytonJawReference = false;
	FTransform OriginalPaytonMouthCavityReferenceTransform = FTransform::Identity;
	bool bHasOriginalPaytonMouthCavityReference = false;
	bool bLoggedOriginalPaytonJawFixedTest = false;
	float OriginalPaytonJawLogCooldown = 0.0f;

	// 上記AnimInstanceの「Set Control」関数への参照(毎フレームのFindFunction呼び出しを避けるため)
	UPROPERTY()
	class UFunction* CachedSetControlFunction = nullptr;

	// CachedFaceAnimInstanceの「Set Control」関数を直接呼び出すヘルパー
	void CallFaceSetControl(FName ControlName, float Value);

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

	// Sceneごとの直接光からJenniferを分離し、全会話地点で同じ顔・髪・衣服の
	// ライティングを使うための専用Lighting Channel 1ライト。
	UPROPERTY()
	TArray<class USpotLightComponent*> JenniferConversationLights;
	void EnsureJenniferConversationLighting();
	void UpdateJenniferConversationLightingTransform();

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

	// 【診断用】Nキーで、MetaHuman Audio LiveLink SubjectのProperty名一覧をログに出す
	void HandleDebugDumpLiveLinkSubject();

	// 【診断用】Tキーで、FaceのPost-Process AnimBPの有効/無効を切り替える
	void HandleDebugTogglePostProcess();

	// 【診断用】Bキーで、BodyコンポーネントのVisibilityを切り替える
	// (画面の顔がBody側から描画されているか確認するため)
	void HandleDebugToggleBodyVisibility();

	// 【診断用】Vキーで、FaceコンポーネントのVisibility/HiddenInGameを切り替える
	// (今操作しているFaceが本当に画面の顔かを実測するため)
	void HandleDebugToggleFaceVisibility();

	// 【診断用】BP_Paytonに追加されているStaticMeshコンポーネントだけを切り替える
	void HandleDebugToggleAddedStaticMeshVisibility();
	void HandleToggleScenePointLightDiagnostic();
	void LogLightingEnvironmentAtJennifer() const;
	bool bDiagnosticScenePointLightsEnabled = true;
	void HandleCycleMyRoomLightDiagnostic();
	void ResetMyRoomLightDiagnostic();
	TArray<TWeakObjectPtr<class ULightComponent>> DiagnosticMyRoomLights;
	TWeakObjectPtr<class ULightComponent> DiagnosticMyRoomDisabledLight;
	int32 DiagnosticMyRoomLightIndex = 0;
	bool bDiagnosticMyRoomPreviousVisibility = true;
	void HandleCycleDirectionalLightDiagnostic();
	void ResetDirectionalLightDiagnostic();
	TArray<TWeakObjectPtr<class ULightComponent>> DiagnosticDirectionalLights;
	TWeakObjectPtr<class ULightComponent> DiagnosticDisabledDirectionalLight;
	int32 DiagnosticDirectionalLightIndex = 0;
	bool bDiagnosticDirectionalPreviousVisibility = true;
	void HandleToggleNeutralBackgroundDiagnostic();
	class AStaticMeshActor* DiagnosticNeutralBackgroundActor = nullptr;
	bool bDiagnosticNeutralBackgroundEnabled = false;
	void HandleCycleCrimsonBufferDiagnostic();
	// -1から開始し、最初のF2で最初の直接光グループ診断を表示する。
	int32 DiagnosticCrimsonBufferModeIndex = -1;
	void ResetDirectLightGroupDiagnostic();
	TArray<TWeakObjectPtr<class ULightComponent>> DiagnosticGroupDisabledLights;
	void HandleToggleJenniferConversationLightsDiagnostic();
	void LogJenniferConversationLightingDiagnostics(EConversationLocation Location) const;
	bool bDiagnosticJenniferKeyLightProbeEnabled = false;

	// 【診断用】MetaHumanの頭髪Groomだけを個別に切り替える
	void HandleDebugToggleHairVisibility();

	/** 診断用: 実際に描画されている元FBX V2 PoseableMeshだけを表示/非表示にする */
	void HandleDebugToggleOriginalPoseableVisibility();
	void HandleDebugToggleOriginalPoseableRootOffset();
	bool bDebugOriginalPoseableRootRaised = false;
	void HandleDebugToggleOriginalPoseableJawOffset();
	bool bDebugOriginalPoseableJawOffset = false;
	void HandleDebugToggleOriginalPoseableJawDown();
	bool bDebugOriginalPoseableJawDown = false;

	// 現在「部屋モード」かどうか(false=車モード、初期状態)
	bool bIsInRoomMode = false;
	bool bInitialRoomModeApplied = false;

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

	// 【記憶システム】Jenniferのキャラクター設定(名前・ボーイフレンドHiro・国籍・居住地等)と、
	// 前回セッションの会話ログを組み合わせて、Realtime APIに渡すInstructionsを組み立てる
	FString BuildJenniferInstructions() const;

	// 今回のセッション中の会話ログ(発言のやり取りを時系列で記録し、次回セッションの記憶として使う)
	TArray<FString> SessionTranscriptLog;

	// セッション記憶の保存先ファイルパス(Saved/JenniferMemory.txt)
	static FString GetMemoryFilePath();

	// EndPlay時に今回の会話ログをファイルへ保存し、次回起動時に読み込めるようにする
	void SaveSessionMemory();
};
