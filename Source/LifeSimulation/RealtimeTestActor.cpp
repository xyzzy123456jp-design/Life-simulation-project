#include "RealtimeTestActor.h"
#include "RealtimeVoiceComponent.h"
#include "LipSyncComponent.h"
#include "MicRecorderComponent.h"
#include "WhisperTranscriberComponent.h"
#include "TTSComponent.h"
#include "ChatManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "IXRTrackingSystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "JenniferNodSkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/Light.h"
#include "Engine/SkyLight.h"
#include "Engine/ReflectionCapture.h"
#include "Engine/TextureCube.h"
#include "Engine/PostProcessVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#endif
#include "Animation/AnimInstance.h"
#include "Animation/MorphTarget.h"
#include "LiveLinkTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "LiveLinkInstance.h"
#include "LiveLinkRemapAsset.h"
#include "DNAAsset.h"
#include "DNAAssetUserData.h"
#include "DNA.h"
#include "DNAReader.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "HAL/PlatformMisc.h"

namespace
{
	struct FConversationSceneConfig
	{
		FVector OriginOffset = FVector::ZeroVector;
		FVector PlayerOffset = FVector(-500.0f, 0.0f, 170.0f);
		FRotator PlayerRotation = FRotator::ZeroRotator;
		FVector PlayerScale = FVector::OneVector;
		FVector JenniferOffset = FVector(0.0f, 0.0f, -90.0f);
		FRotator JenniferRotation = FRotator(0.0f, 180.0f, 0.0f);
		FVector JenniferScale = FVector::OneVector;
		FVector BackgroundOffset = FVector::ZeroVector;
		FRotator BackgroundRotationOffset = FRotator::ZeroRotator;
		FVector BackgroundScale = FVector::OneVector;
		FVector CameraOffset = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;
		float CameraFOV = 52.0f;
		bool bHasCameraOffset = false;
		bool bHasCameraRotation = false;
		// 点光源の強さを変えても見た目の明るさがほぼ変わらなかったため(自動露出が
		// 打ち消してしまう)、カメラの露出補正を直接指定できるようにする。0=補正なし、
		// 正の値で明るく、負の値で暗くなる。
		float ExposureBias = 0.0f;
		bool bHasExposureBias = false;
	};

	bool ReadSceneVector(const FConfigFile& Config, const TCHAR* Section, const TCHAR* Key, FVector& Value)
	{
		FString Text;
		return Config.GetString(Section, Key, Text) && Value.InitFromString(Text);
	}

	bool ReadSceneRotator(const FConfigFile& Config, const TCHAR* Section, const TCHAR* Key, FRotator& Value)
	{
		FString Text;
		return Config.GetString(Section, Key, Text) && Value.InitFromString(Text);
	}

	FConversationSceneConfig LoadConversationSceneConfig(const FString& Stem)
	{
		FConversationSceneConfig Result;
		FConfigFile Config;
		Config.Read(FPaths::ProjectConfigDir() / TEXT("ConversationScenes.ini"));
		const FString Section = FString::Printf(TEXT("ConversationScene.%s"), *Stem);
		ReadSceneVector(Config, *Section, TEXT("OriginOffset"), Result.OriginOffset);
		ReadSceneVector(Config, *Section, TEXT("PlayerOffset"), Result.PlayerOffset);
		ReadSceneRotator(Config, *Section, TEXT("PlayerRotation"), Result.PlayerRotation);
		ReadSceneVector(Config, *Section, TEXT("PlayerScale"), Result.PlayerScale);
		ReadSceneVector(Config, *Section, TEXT("JenniferOffset"), Result.JenniferOffset);
		ReadSceneRotator(Config, *Section, TEXT("JenniferRotation"), Result.JenniferRotation);
		ReadSceneVector(Config, *Section, TEXT("JenniferScale"), Result.JenniferScale);
		ReadSceneVector(Config, *Section, TEXT("BackgroundOffset"), Result.BackgroundOffset);
		ReadSceneRotator(Config, *Section, TEXT("BackgroundRotationOffset"), Result.BackgroundRotationOffset);
		ReadSceneVector(Config, *Section, TEXT("BackgroundScale"), Result.BackgroundScale);
		Result.bHasCameraOffset = ReadSceneVector(Config, *Section, TEXT("CameraOffset"), Result.CameraOffset);
		Result.bHasCameraRotation = ReadSceneRotator(Config, *Section, TEXT("CameraRotation"), Result.CameraRotation);
		Config.GetFloat(*Section, TEXT("CameraFOV"), Result.CameraFOV);
		Result.bHasExposureBias = Config.GetFloat(*Section, TEXT("ExposureBias"), Result.ExposureBias);
		return Result;
	}

	// FrontCameraの位置調整結果を保存する専用ファイル(Blueprintアセット自体は書き換えない)
	FString GetCockpitCameraConfigPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Config/CockpitCameraOffset.ini");
	}

	// VRの目の高さ補正値を保存する専用ファイル
	FString GetVREyeHeightConfigPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Config/VehicleCockpitVREyeHeight.ini");
	}
}

ARealtimeTestActor::ARealtimeTestActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RealtimeVoice = CreateDefaultSubobject<URealtimeVoiceComponent>(TEXT("RealtimeVoice"));
	LipSync = CreateDefaultSubobject<ULipSyncComponent>(TEXT("LipSync"));
	LegacyMicRecorder = CreateDefaultSubobject<UMicRecorderComponent>(TEXT("LegacyMicRecorder"));
	LegacyWhisper = CreateDefaultSubobject<UWhisperTranscriberComponent>(TEXT("LegacyWhisper"));
	LegacyTTS = CreateDefaultSubobject<UTTSComponent>(TEXT("LegacyTTS"));
}

void ARealtimeTestActor::BeginPlay()
{
	// Super::BeginPlay()は子コンポーネント(LipSync含む)のBeginPlayを内部で呼び出すため、
	// LipSyncがCharacterActor/VoiceSourceを参照する前に、必ずここで先に設定しておく
	LipSync->CharacterActor = CharacterActor;
	LipSync->VoiceSource = nullptr;
	LipSync->TTSSource = LegacyTTS;

	// 【診断用】CharacterActorが実際にどのクラスのActorを指しているか確認する
	if (CharacterActor)
	{
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: CharacterActorクラス=%s 名前=%s"),
			*CharacterActor->GetClass()->GetPathName(), *CharacterActor->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CharacterActorが未設定です"));
	}

	Super::BeginPlay();
	CaptureJenniferCanonicalScale();

	// Blueprintと全コンポーネントのBeginPlay後に表示用顔を構築する。以前はSuperより
	// 前だったため、初回ロード時だけSkeletalMeshの初期化に表示状態を上書きされていた。
	SetupPaytonNewFace();
	EnsureJenniferConversationLighting();
	SetupHandGesture();

	// 非ドライブ地点用カメラは、レベル上の参照や親子関係を引き継がない独立Actorとして
	// 毎回生成する。これで車両Blueprintに視点を奪われないようにする。
	if (GetWorld())
	{
		const float InitialCameraFOV = IntroFaceCamera && IntroFaceCamera->GetCameraComponent()
			? IntroFaceCamera->GetCameraComponent()->FieldOfView
			: 90.0f;
		const FTransform InitialCameraTransform = IntroFaceCamera
			? IntroFaceCamera->GetActorTransform()
			: FTransform(GetActorRotation(), GetActorLocation());
		IntroFaceCamera = GetWorld()->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), InitialCameraTransform);
		if (IntroFaceCamera)
		{
			IntroFaceCamera->SetActorLabel(TEXT("LifeSimConversationCamera"));
			if (UCameraComponent* CameraComponent = IntroFaceCamera->GetCameraComponent())
			{
				CameraComponent->SetFieldOfView(InitialCameraFOV);
			}
			CanonicalConversationCameraFOVDegrees = InitialCameraFOV;
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 会話地点用カメラをC++で生成しました"));
		}
	}
	UpdateJenniferConversationLightingTransform();
	// ゲーム開始時のMyRoom表示が正解なので、別シーンへ移る前に実測値を保存する。
	CaptureCanonicalConversationFraming();
	ApplyConversationSceneExposure(EConversationLocation::MyRoom);

	// プロジェクトに専用背景がまだ無い5地点は、最低限遊べる簡易セットを自動生成する。
	// 同じタグの手作り地点が存在する場合は生成せず、後から本格背景へ差し替え可能。
	BuildFallbackConversationScenes();

	// APIキーはBlueprintアセットへ保存せず、ユーザー環境変数から優先して読む。
	// これによりソース管理・ログ・スクリーンショットへの秘密情報の混入を防ぐ。
	FString RuntimeApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("LIFESIM_OPENAI_API_KEY"));
	RuntimeApiKey.TrimStartAndEndInline();
	RealtimeVoice->ApiKey = RuntimeApiKey.IsEmpty() ? ApiKey : RuntimeApiKey;
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: APIキー取得元=%s"),
		RuntimeApiKey.IsEmpty() ? TEXT("Actor設定") : TEXT("環境変数"));

	// 【記憶システム】Jenniferのキャラクター設定+前回セッションの記憶を組み立てて、
	// Realtime APIのInstructionsとして渡す(ゲーム開始時に「覚えている」状態にする)
	RealtimeVoice->Instructions = BuildJenniferInstructions();

	RealtimeVoice->OnConnected.AddDynamic(this, &ARealtimeTestActor::HandleConnected);
	RealtimeVoice->OnDisconnected.AddDynamic(this, &ARealtimeTestActor::HandleDisconnected);
	RealtimeVoice->OnError.AddDynamic(this, &ARealtimeTestActor::HandleError);
	RealtimeVoice->OnUserTranscript.AddDynamic(this, &ARealtimeTestActor::HandleUserTranscript);
	RealtimeVoice->OnAssistantTranscript.AddDynamic(this, &ARealtimeTestActor::HandleAssistantTranscript);
	RealtimeVoice->OnUserStartedSpeaking.AddDynamic(this, &ARealtimeTestActor::HandleUserStartedSpeaking);
	RealtimeVoice->OnAssistantStartedSpeaking.AddDynamic(this, &ARealtimeTestActor::HandleRealtimeAssistantStartedSpeaking);
	RealtimeVoice->OnNodRequested.AddDynamic(this, &ARealtimeTestActor::HandleRealtimeNodRequested);
	RealtimeVoice->HandGestureExecutor = [this](const FString& GestureId, bool bAiTest)
	{
		return RequestHandGesture(GestureId, bAiTest ? TEXT("AI_TEST") : TEXT("REALTIME"));
	};

	// デフォルトは従来方式。Realtime APIはF9を押した時だけ使用する。
	LegacyWhisper->ApiKey = RealtimeVoice->ApiKey;
	LegacyWhisper->Language = TEXT("en");
	LegacyTTS->ApiKey = RealtimeVoice->ApiKey;
	// Jenniferの声をLegacy/Realtimeで統一する。既存Blueprintに以前の値が
	// 保存されていても、起動時に同じ若く明るい声へ確実に揃える。
	LegacyTTS->Voice = TEXT("coral");
	RealtimeVoice->Voice = TEXT("coral");
	LegacyWhisper->OnTranscriptionComplete.AddDynamic(this, &ARealtimeTestActor::HandleLegacyTranscriptionComplete);
	LegacyWhisper->OnTranscriptionFailed.AddDynamic(this, &ARealtimeTestActor::HandleLegacyTranscriptionFailed);
	LegacyTTS->OnPlaybackFinished.AddDynamic(this, &ARealtimeTestActor::HandleLegacyTTSFinished);
	LegacyTTS->OnPlaybackStarted.AddDynamic(this, &ARealtimeTestActor::HandleLegacyTTSStarted);
	LegacyTTS->OnTTSFailed.AddDynamic(this, &ARealtimeTestActor::HandleLegacyTTSFailed);
	if (GetWorld())
	{
		LegacyChatManager = GetWorld()->SpawnActor<AChatManager>();
		if (LegacyChatManager)
		{
			LegacyChatManager->ApiKey = RealtimeVoice->ApiKey;
			LegacyChatManager->SystemInstructions = RealtimeVoice->Instructions;
			LegacyChatManager->ExpressionComponent = LipSync;
			LegacyChatManager->OnChatResponseReceived.AddDynamic(this, &ARealtimeTestActor::HandleLegacyChatResponse);
			LegacyChatManager->OnNodRequested.AddDynamic(this, &ARealtimeTestActor::HandleLegacyNodRequested);
			LegacyChatManager->HandGestureExecutor = [this](const FString& GestureId, bool)
			{
				return RequestHandGesture(GestureId, TEXT("LEGACY"));
			};
		}
	}
	StartLegacyVoice();

	// 【コスト対策】Realtime APIは常時接続すると課金がかさむため、デフォルトでは接続しない。
	// F9キーを押した時だけ接続/切断をトグルする(HandleToggleRealtimeVoiceKeyPressed参照)。
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 起動時は未接続です。F9キーでRealtime APIへの接続/切断を切り替えます"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("Voice chat: Standard mode (F9 = Realtime API)"), true, FVector2D(1.75f, 1.75f));
	}

	// 【車モード】車のポーンを取得してキャッシュし、Paytonを助手席にアタッチ、
	// コックピットカメラ(FrontCamera)を有効化してそこへ視点を切り替える。
	// BeginPlay時点ではGameModeがまだ車のポーンをスポーン/憑依させていないことが
	// あるため、見つかるまで0.2秒ごとに再試行する
	TrySetupVehicleMode();
	GetWorldTimerManager().SetTimer(VehicleSetupRetryTimerHandle, this, &ARealtimeTestActor::TrySetupVehicleMode, 0.2f, true);

	// Pキーで、AIが喋っている最中に手動で割り込めるようにする
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		EnableInput(PC);
	}

#if WITH_EDITOR
	// PIEではF9がゲームのInputComponentへ届く前にエディタ側で処理される場合がある。
	// Slateの事前通知で捕捉し、ゲームへの配送順に依存せず切り替える。
	if (FSlateApplication::IsInitialized())
	{
		SlatePreInputKeyDownHandle = FSlateApplication::Get().OnApplicationPreInputKeyDownListener().AddUObject(
			this, &ARealtimeTestActor::HandleSlatePreInputKeyDown);
	}
#endif

	if (InputComponent)
	{
		// P/Mキーは車のEnhanced Input(ハンドブレーキ等)と衝突していたため変更。
		// F1/F2はVRソフト(SteamVR/Pimaxクライアント等)に横取りされるため、
		// 普通の文字キー(Y/H)に変更する
		InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ARealtimeTestActor::HandleInterruptKeyPressed);
		InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ARealtimeTestActor::HandleToggleSceneModeKeyPressed);
		InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ARealtimeTestActor::HandleCycleExpressionKeyPressed);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARealtimeTestActor::HandleCycleExpressionTestKeyPressed);
		InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &ARealtimeTestActor::TestNod);
		InputComponent->BindKey(EKeys::F2, IE_Pressed, this, &ARealtimeTestActor::HandleCycleCrimsonBufferDiagnostic);
		InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &ARealtimeTestActor::HandleToggleJenniferConversationLightsDiagnostic);
		// F11はUnreal Editorの没入モードと競合するため、未使用の数字0を診断専用にする。
		InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &ARealtimeTestActor::HandleToggleNeutralBackgroundDiagnostic);
		InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ARealtimeTestActor::TestGesture);
		InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ARealtimeTestActor::HandleCycleGestureKeyPressed);
		InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARealtimeTestActor::HandleCycleGestureAiTestKeyPressed);

		// 【コスト対策】F9キーでRealtime API(音声会話)への接続/切断をトグルする。
		// 車のEnhanced Input(P/Mキーがハンドブレーキ等と衝突)やVRソフト(F1/F2を横取り)を
		// 避けて、既存のF3〜F8デバッグキーの並びに合わせてF9にした。
		// Realtime切替はTickでPlayerControllerの押下状態を直接確認する。
		// PIEの入力スタック順によりActorのBindKeyが呼ばれないケースを避ける。

		// 【場所の強制切り替え】音声会話を使わず各移動先を直接テストする。
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ARealtimeTestActor::HandleForceMyRoomKeyPressed);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ARealtimeTestActor::HandleForceClassroomKeyPressed);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ARealtimeTestActor::HandleForceCinemaKeyPressed);
		InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ARealtimeTestActor::HandleForceDriveKeyPressed);
		InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ARealtimeTestActor::HandleForceJenniferRoomKeyPressed);
		InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ARealtimeTestActor::HandleForceWalkKeyPressed);
		InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &ARealtimeTestActor::HandleForceRestaurantKeyPressed);

		// 【Xboxコントローラー】Yボタンで部屋⇔車切り替え、Bボタンで会話割り込み、XボタンでVRセンターリセット
		InputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &ARealtimeTestActor::HandleToggleSceneModeKeyPressed);
		InputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &ARealtimeTestActor::HandleInterruptKeyPressed);
		InputComponent->BindKey(EKeys::Gamepad_FaceButton_Left, IE_Pressed, this, &ARealtimeTestActor::HandleResetVRCenter);

		// 【調整用】I/K/J/L(前後左右)+U/O(下/上)でコックピットカメラの位置を1回押しごとに調整
		// (矢印キー・テンキーはXboxコントローラーやNumLock状態と干渉するため使わない)
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeForward);
		InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeBackward);
		InputComponent->BindKey(EKeys::J, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeLeft);
		InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeRight);
		InputComponent->BindKey(EKeys::O, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeUp);
		InputComponent->BindKey(EKeys::U, IE_Pressed, this, &ARealtimeTestActor::HandleCockpitNudgeDown);

		// 【VR】[ ]キーで目の高さ補正を1回押しごとに調整
		InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ARealtimeTestActor::HandleVREyeHeightDecrease);
		InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ARealtimeTestActor::HandleVREyeHeightIncrease);

		// 【VR】Homeキーでセンター位置をリセット
		InputComponent->BindKey(EKeys::Home, IE_Pressed, this, &ARealtimeTestActor::HandleResetVRCenter);

		// Escapeキーでゲーム終了
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ARealtimeTestActor::HandleQuitGame);

		// 【診断用】Nキーで、MetaHuman Audio LiveLink SubjectのProperty名一覧をログに出す
		InputComponent->BindKey(EKeys::N, IE_Pressed, this, &ARealtimeTestActor::HandleDebugDumpLiveLinkSubject);

		// 【診断用】Tキーで、FaceのPost-Process AnimBPの有効/無効を切り替える
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ARealtimeTestActor::HandleDebugTogglePostProcess);

		// 【診断用】Bキーで、BodyコンポーネントのVisibilityを切り替える
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleBodyVisibility);

		// 【診断用】Vキーで、FaceコンポーネントのVisibility/HiddenInGameを切り替える
		InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleFaceVisibility);

		// 【診断用】Gキーで、追加されたStaticMeshコンポーネントだけを切り替える
		InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleAddedStaticMeshVisibility);

		// 【診断用】F3キーで、頭髪Groomだけを個別に切り替える
		InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleHairVisibility);

		// 診断用: 元FBX V2 PoseableMeshが画面上の顔かを物理的に確認する
		InputComponent->BindKey(EKeys::F4, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleOriginalPoseableVisibility);
		InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleOriginalPoseableRootOffset);
		InputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleOriginalPoseableJawOffset);
		InputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ARealtimeTestActor::HandleDebugToggleOriginalPoseableJawDown);
		// V8を保持したまま、写真ベースV9の外観を比較する。
		InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &ARealtimeTestActor::HandleTogglePaytonV9);

		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: キーバインド完了（Realtime切替: F9 または 9）"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: InputComponentが取得できず、キーのバインドに失敗しました"));
	}
}

void ARealtimeTestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DiagnosticNeutralBackgroundActor)
	{
		DiagnosticNeutralBackgroundActor->Destroy();
		DiagnosticNeutralBackgroundActor = nullptr;
	}
	ResetDirectLightGroupDiagnostic();
	if (GetWorld())
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.SkyLighting 1"));
	}
	ResetDirectionalLightDiagnostic();
	ResetMyRoomLightDiagnostic();
	ResetHandGesture();
#if WITH_EDITOR
	if (FSlateApplication::IsInitialized() && SlatePreInputKeyDownHandle.IsValid())
	{
		FSlateApplication::Get().OnApplicationPreInputKeyDownListener().Remove(SlatePreInputKeyDownHandle);
		SlatePreInputKeyDownHandle.Reset();
	}
#endif
	StopLegacyVoice();
	// 【記憶システム】今回のセッションの会話ログを保存し、次回起動時にBuildJenniferInstructionsで
	// 読み込めるようにする
	SaveSessionMemory();

	Super::EndPlay(EndPlayReason);
}

FString ARealtimeTestActor::GetMemoryFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("JenniferMemory.txt");
}

FString ARealtimeTestActor::BuildJenniferInstructions() const
{
	FString Instructions = TEXT(
		"You are Jennifer, a 19-year-old Canadian college student living in Vancouver. "
		"You are the girlfriend of the person you're talking with, whose name is Hiro. "
		"You know these facts about yourself and remember them throughout the conversation. "
		"Speak naturally in English, the way a real girlfriend having a voice conversation would - "
		"usually 1-3 sentences, warm, calm, and conversational in tone. "
		"Speak in a composed, measured manner. Avoid bubbly or excited delivery, forced cheerfulness, "
		"excessive exclamation marks, slang, and repeatedly opening with 'Hey'. "
		"Your college student status is background information only: do not force student stereotypes, "
		"an immature manner, an exaggerated youthful style, or references to college or university life "
		"unless they are relevant to the conversation. "
		"Hiro may invite you to move together to his room, a classroom, a movie theater, "
		"a drive in the car, your room, a walk, or a restaurant. Decide naturally whether "
		"you agree. When you agree to move, state the agreement clearly (for example, "
		"'Yes, let's go' or 'Sure, I'd love to'). Do not claim that the location has already "
		"changed before you agree. You may also decline naturally."
	);

	FString PreviousMemory;
	if (FFileHelper::LoadFileToString(PreviousMemory, *GetMemoryFilePath()) && !PreviousMemory.IsEmpty())
	{
		Instructions += TEXT("\n\nHere is what happened the last time you talked (use this as your memory of your relationship so far, but don't recite it verbatim):\n");
		Instructions += PreviousMemory;
	}

	return Instructions;
}

void ARealtimeTestActor::SaveSessionMemory()
{
	if (SessionTranscriptLog.Num() == 0)
	{
		return;
	}

	const FString Combined = FString::Join(SessionTranscriptLog, TEXT("\n"));
	if (FFileHelper::SaveStringToFile(Combined, *GetMemoryFilePath()))
	{
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: セッションの会話ログを保存しました(%d行)"), SessionTranscriptLog.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: セッションの会話ログの保存に失敗しました"));
	}
}

void ARealtimeTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTestNod(DeltaTime);
	TickHandGesture(DeltaTime);

	// パッケージ版ではPlayerControllerが保持しているキー状態を直接確認する。
	// Editor版はSlate事前通知で処理し、同じ押下による二重切替を避ける。
#if !WITH_EDITOR
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (PC->WasInputKeyJustPressed(EKeys::F9) || PC->WasInputKeyJustPressed(EKeys::Nine))
		{
			HandleToggleRealtimeVoiceKeyPressed();
		}
	}
#endif

	// 【診断用】F9でRealtime APIと従来方式のどちらが実際に有効か、常に画面で分かるようにする
	if (GEngine)
	{
		const bool bRealtimeActive = RealtimeVoice && RealtimeVoice->IsConnected();
		const FString StatusText = bRealtimeActive
			? FString::Printf(TEXT("VOICE: Realtime API (Voice=%s)"), *RealtimeVoice->Voice)
			: FString::Printf(TEXT("VOICE: Legacy (Voice=%s)"), LegacyTTS ? *LegacyTTS->Voice : TEXT("unknown"));
		GEngine->AddOnScreenDebugMessage(9050, 0.0f, bRealtimeActive ? FColor::Green : FColor::Orange, StatusText, true, FVector2D(1.75f, 1.75f));
	}

	// 診断用の重い処理(毎フレームのコンポーネント再取得・LOD強制・大量ログ)は
	// パフォーマンスに影響するため撤去。キャッシュ済みの参照だけを軽量に使う
	if (CachedFaceAnimInstance && CachedJawOpenAlphaProperty)
	{
		const double JawOpenValue = LipSync ? static_cast<double>(LipSync->GetCurrentJawOpen()) : 0.0;
		CachedJawOpenAlphaProperty->SetPropertyValue_InContainer(CachedFaceAnimInstance, JawOpenValue);
	}

	if (OriginalPaytonPoseableMesh && bHasOriginalPaytonJawReference
		&& !bDebugOriginalPoseableRootRaised)
	{
		const float JawOpenValue = LipSync ? LipSync->GetCurrentJawOpen() : 0.0f;
		const float MouthOpenAlpha = FMath::Clamp(JawOpenValue / 0.18f, 0.0f, 1.0f);

		// Meshy AnimatedMouth V1はMorphTargetではなく、下唇領域をjawボーンへ
		// ウェイトしたメッシュ。Component Spaceで下方向へ最大1.6cm移動して開口する。
		FTransform JawTransform = OriginalPaytonJawReferenceTransform;
		// Meshyの上下唇の隙間から黒い背景が見えないよう、可動域全体で
		// 下唇を上唇へ重ねる。0.8cmの動きは残すが、開口端でも隙間を作らない。
		JawTransform.AddToTranslation(FVector(0.0f, 0.0f, 1.20f - 0.80f * MouthOpenAlpha));
		OriginalPaytonPoseableMesh->SetBoneTransformByName(
			TEXT("jaw"), JawTransform, EBoneSpaces::ComponentSpace);
		OriginalPaytonPoseableMesh->RefreshBoneTransforms();
	}

	if (OriginalPaytonMorphMesh && !bDebugOriginalPoseableRootRaised)
	{
		const float JawOpenValue = LipSync ? LipSync->GetCurrentJawOpen() : 0.0f;
		const float MouthOpenAlpha = FMath::Clamp(JawOpenValue / 0.18f, 0.0f, 1.0f);
		// 表示Mesh自身で口と瞬きを評価する。PoseableMeshをLeaderにすると、
		// UEはFollower自身のMorphをレンダリングバッファへ反映できない。
		OriginalPaytonMorphMesh->SetMorphTarget(TEXT("mouthSeal"), 1.0f, false);
		// 最大値を少し抑え、開口時に下唇が縦へ膨らみすぎるのを軽減する。
		const float RefinedMouthOpenAlpha = MouthOpenAlpha * 0.78f;
		OriginalPaytonMorphMesh->SetMorphTarget(TEXT("jawOpen"), RefinedMouthOpenAlpha, false);
		if (OriginalPaytonUpperTeethComponent)
		{
			// 閉口時には隠し、唇が開いた時だけ上歯を見せる。
			OriginalPaytonUpperTeethComponent->SetVisibility(
				!bShowingPaytonV9 && MouthOpenAlpha > 0.10f, false);
		}

	}

	if (CrimsonGazeMorphComponent)
	{
		const float JawOpenValue = LipSync ? LipSync->GetCurrentJawOpen() : 0.0f;
		const float MouthOpenAlpha = FMath::Clamp(JawOpenValue / 0.18f, 0.0f, 1.0f);
		// 後加工した下唇は1.0まで適用すると縦に厚く見えるため、実用域を抑える。
		const float RefinedMouthOpenAlpha = MouthOpenAlpha * 0.55f;
		CrimsonGazeMorphComponent->SetMorphTarget(TEXT("jawOpen"), RefinedMouthOpenAlpha, false);
		// Material 1 / Section 1 は口の穴の奥に置いた口腔。閉口時にもわずかな
		// 境界の隙間から水色の帯として見えていたため、実際に口が開いた時だけ描画する。
		CrimsonGazeMorphComponent->ShowMaterialSection(
			1, 1, MouthOpenAlpha > 0.10f, 0);
		if (CrimsonGazeUpperTeethComponent)
		{
			CrimsonGazeUpperTeethComponent->SetVisibility(false, false);
		}
	}

	if (OriginalPaytonPoseableMesh && bHasOriginalPaytonJawReference && !bDebugOriginalPoseableRootRaised
		&& (bDebugOriginalPoseableJawOffset || bDebugOriginalPoseableJawDown))
	{
		// PoseableMeshはF6/F7のボーン診断専用。通常表示はMorphMeshを使う。
		FTransform JawTransform = OriginalPaytonJawReferenceTransform;
		// F6/F7は診断用。通常の音声変形とは同時に適用しない。
		if (bDebugOriginalPoseableJawOffset)
		{
			JawTransform.AddToTranslation(FVector(30.0f, 0.0f, 0.0f));
		}
		if (bDebugOriginalPoseableJawDown)
		{
			JawTransform.AddToTranslation(FVector(0.0f, 0.0f, -10.0f));
		}
		// F6/F7の既存診断だけは顎ボーン移動を残す。
		if (bDebugOriginalPoseableJawOffset || bDebugOriginalPoseableJawDown)
		{
			OriginalPaytonPoseableMesh->SetBoneTransformByName(TEXT("jaw"), JawTransform, EBoneSpaces::ComponentSpace);
		}
		if (!bLoggedOriginalPaytonJawFixedTest)
		{
			OriginalPaytonPoseableMesh->RefreshBoneTransforms();
			const FTransform ReadBack = OriginalPaytonPoseableMesh->GetBoneTransformByName(
				TEXT("jaw"), EBoneSpaces::ComponentSpace);
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2固定顎テスト ReferenceLocation=%s WrittenLocation=%s ReadBackLocation=%s Bounds=%s"),
				*OriginalPaytonJawReferenceTransform.GetTranslation().ToString(),
				*JawTransform.GetTranslation().ToString(),
				*ReadBack.GetTranslation().ToString(),
				*OriginalPaytonPoseableMesh->Bounds.BoxExtent.ToString());
			bLoggedOriginalPaytonJawFixedTest = true;
		}

		if (bHasOriginalPaytonMouthCavityReference)
		{
			FTransform CavityTransform = OriginalPaytonMouthCavityReferenceTransform;
			// 【固定テスト】音声に関係なく口内を最大表示し、実際の描画位置を確認する。
			CavityTransform.SetScale3D(FVector(1.0f, 1.0f, 1.0f));
			OriginalPaytonPoseableMesh->SetBoneTransformByName(
				TEXT("mouth_cavity"), CavityTransform, EBoneSpaces::ComponentSpace);
		}
		OriginalPaytonPoseableMesh->RefreshBoneTransforms();
	}

	if (OriginalPaytonMouthComponent)
	{
		// 球形の口内メッシュは黒い丸に見えて不自然なため使用しない。
		OriginalPaytonMouthComponent->SetVisibility(false, false);
	}

	// 毎フレーム、キャラクターをカメラの方に向かせ続ける(部屋モードの時のみ。
	// 車モードでは助手席にアタッチ済みなので、相対姿勢を崩さないよう回転させない)。
	// 映画館だけは「背中越しにスクリーンを見る構図」にしたいため、同じメッシュ前方向補正
	// (FacingYawOffset)を使いつつ180度反転させ、カメラのほうを向かせるのではなく
	// カメラに背を向けさせる(単純にこの処理自体を止めると、FacingYawOffsetによる
	// メッシュの前方向補正が失われて横向きになってしまうため、反転はここで行う)。
	if (bIsInRoomMode && CharacterActor && IntroFaceCamera)
	{
		FVector ToCamera = IntroFaceCamera->GetActorLocation() - CharacterActor->GetActorLocation();
		FRotator FacingRotation = ToCamera.Rotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += FacingYawOffset; // メッシュの前方向オフセット補正
		if (CurrentConversationLocation == EConversationLocation::Cinema)
		{
			FacingRotation.Yaw += 180.0f;
		}
		CharacterActor->SetActorRotation(FacingRotation);
	}

	// プレイヤーは車両Pawnを所持したままなので、PlayerControllerの自動カメラ管理が
	// 非ドライブ地点でもViewTargetを車両へ戻すことがある。非ドライブ中は専用カメラを維持する。
	if (bIsInRoomMode && IntroFaceCamera)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			PC->bAutoManageActiveCameraTarget = false;
			if (PC->GetViewTarget() != IntroFaceCamera)
			{
				PC->SetViewTargetWithBlend(IntroFaceCamera, 0.0f);
			}
		}
	}

	// 【デバッグ用】Paytonの実際の座標を画面に常時表示(テレポート後に位置がズレていないか確認するため)
	if (GEngine && CharacterActor)
	{
		GEngine->AddOnScreenDebugMessage(9001, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("Payton pos: %s"), *CharacterActor->GetActorLocation().ToString()), true, FVector2D(1.75f, 1.75f));
		GEngine->AddOnScreenDebugMessage(9013, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("Payton rot: %s"), *CharacterActor->GetActorRotation().ToString()), true, FVector2D(1.75f, 1.75f));
	}

	// 車のBlueprintが毎フレームFrontCameraの位置を上書きしてくることがあるため、
	// こちらも毎フレーム、望みの位置を強制的に再設定する
	EnforceFrontCameraOffset();

	// 【監視】BP_VehicleAdvPlayerControllerには「車が壊れたらPlayerStartの位置に
	// 新しい車を自動スポーンして憑依し直す」処理が元から入っている。これが起きると
	// Paytonのアタッチ・コックピットカメラの設定が失われるため、毎フレーム確認し、
	// 車が差し替わっていたら自動で設定をやり直す
	if (!bIsInRoomMode)
	{
		AActor* CurrentPlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
		if (CurrentPlayerPawn && CurrentPlayerPawn != VehiclePawn)
		{
			VehiclePawn = CurrentPlayerPawn;
			AttachCharacterToVehicle();
			ActivateVehicleCockpitCamera();

			if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
			{
				PC->SetViewTargetWithBlend(VehiclePawn, 0.0f);
			}

			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: プレイヤーの車が差し替わったため、再設定しました"));
		}
	}
}

void ARealtimeTestActor::HandleInterruptKeyPressed()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [INPUT] 会話を割り込みました"));

	if (RealtimeVoice && RealtimeVoice->IsConnected())
	{
		RealtimeVoice->Interrupt();
	}
	else if (LegacyTTS && LegacyTTS->IsPlaying())
	{
		LegacyTTS->StopPlayback();
		StartLegacyRecording();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("(interrupted)"), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleCycleExpressionKeyPressed()
{
	static const TArray<FString> Emotions = {
		TEXT("happy"), TEXT("surprised"), TEXT("sad"),
		TEXT("confused"), TEXT("embarrassed"), TEXT("neutral")
	};

	const int32 SafeIndex = DebugExpressionCycleIndex % Emotions.Num();
	const FString& Emotion = Emotions[SafeIndex];
	const FString PreviousEmotion = SafeIndex == 0 ? TEXT("neutral") : Emotions[SafeIndex - 1];
	const bool bApplied = LipSync && LipSync->SetExpressionTarget(Emotion, 1.0f);
	DebugExpressionCycleIndex = (SafeIndex + 1) % Emotions.Num();

	const FString Message = bApplied
		? FString::Printf(TEXT("[EXPRESSION][MANUAL] %s intensity=1.00 (%s -> %s)"), *Emotion, *PreviousEmotion, *Emotion)
		: FString::Printf(TEXT("[EXPRESSION][MANUAL] %s intensity=1.00 APPLY_FAILED (Morph不足/適用先なし)"), *Emotion);
	if (bApplied)
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *Message);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9101, 4.0f, bApplied ? FColor::Green : FColor::Red, Message, true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::HandleCycleExpressionTestKeyPressed()
{
	static const TArray<FString> Emotions = {
		TEXT("happy"), TEXT("surprised"), TEXT("sad"),
		TEXT("confused"), TEXT("embarrassed"), TEXT("neutral")
	};

	const int32 SafeIndex = DebugExpressionTestCycleIndex % Emotions.Num();
	const FString& Emotion = Emotions[SafeIndex];
	const float Intensity = Emotion == TEXT("neutral") ? 0.0f : 0.8f;
	const FString TestText = FString::Printf(TEXT("Expression test: %s %.1f"), *Emotion, Intensity);
	const bool bSent = RealtimeVoice && RealtimeVoice->SendTextMessage(TestText);
	if (bSent)
	{
		DebugExpressionTestCycleIndex = (SafeIndex + 1) % Emotions.Num();
	}

	const FString Message = bSent
		? FString::Printf(TEXT("[EXPRESSION][AI_TEST via text] \"%s\" を送信"), *TestText)
		: FString::Printf(TEXT("[EXPRESSION][AI_TEST via text] 送信失敗 (Realtime API未接続): \"%s\""), *TestText);
	if (bSent)
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9103, 4.0f, bSent ? FColor::Cyan : FColor::Red,
			Message, true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::TestNod()
{
	StartNod(TEXT("MANUAL"));
}

void ARealtimeTestActor::HandleLegacyNodRequested()
{
	StartNod(TEXT("LEGACY"));
}

void ARealtimeTestActor::HandleRealtimeNodRequested()
{
	StartNod(TEXT("REALTIME"));
}

void ARealtimeTestActor::StartNod(const TCHAR* SourceTag)
{
	const FString SafeSourceTag = SourceTag ? SourceTag : TEXT("UNKNOWN");
	if (SafeSourceTag == TEXT("LEGACY") || SafeSourceTag == TEXT("REALTIME"))
	{
		bNodTriggeredForCurrentAssistantResponse = true;
	}
	if (!CrimsonGazeMorphComponent || CrimsonGazeMorphComponent->GetBoneIndex(TEXT("head")) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[NOD][%s] start failed: Crimson/head bone unavailable"), *SafeSourceTag);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9110, 4.0f, FColor::Red,
				FString::Printf(TEXT("[NOD][%s] FAILED: head bone unavailable"), *SafeSourceTag), true, FVector2D(1.75f, 1.75f));
		}
		return;
	}

	// 進行中の再トリガーは無視する。途中からの再開始によるBase Poseの
	// 二重取得・回転蓄積を避け、必ず元姿勢へ戻してから次を受け付ける。
	if (NodPhase != ENodPhase::Idle)
	{
		UE_LOG(LogTemp, Log, TEXT("[NOD][%s] ignored: nod already in progress"), *SafeSourceTag);
		return;
	}

	NodPhase = ENodPhase::NodDown;
	NodPhaseElapsed = 0.0f;
	CurrentNodPitchDegrees = 0.0f;
	ActiveNodSource = SafeSourceTag;
	UE_LOG(LogTemp, Log, TEXT("[NOD][%s] start pitch=%.1f"), *ActiveNodSource, NodTargetPitchDegrees);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9110, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("[NOD][%s] nod %.1f deg"), *ActiveNodSource, NodTargetPitchDegrees),
			true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::TickTestNod(float DeltaTime)
{
	if (NodPhase == ENodPhase::Idle || !CrimsonGazeMorphComponent)
	{
		return;
	}

	NodPhaseElapsed += DeltaTime;
	switch (NodPhase)
	{
	case ENodPhase::NodDown:
	{
		const float Alpha = FMath::Clamp(NodPhaseElapsed / NodDownDurationSeconds, 0.0f, 1.0f);
		CurrentNodPitchDegrees = FMath::InterpEaseInOut(0.0f, NodTargetPitchDegrees, Alpha, 2.0f);
		if (Alpha >= 1.0f)
		{
			NodPhase = ENodPhase::Hold;
			NodPhaseElapsed = 0.0f;
		}
		break;
	}
	case ENodPhase::Hold:
		CurrentNodPitchDegrees = NodTargetPitchDegrees;
		if (NodPhaseElapsed >= NodHoldDurationSeconds)
		{
			NodPhase = ENodPhase::Return;
			NodPhaseElapsed = 0.0f;
		}
		break;
	case ENodPhase::Return:
	{
		const float Alpha = FMath::Clamp(NodPhaseElapsed / NodReturnDurationSeconds, 0.0f, 1.0f);
		CurrentNodPitchDegrees = FMath::InterpEaseInOut(NodTargetPitchDegrees, 0.0f, Alpha, 2.0f);
		if (Alpha >= 1.0f)
		{
			CurrentNodPitchDegrees = 0.0f;
			NodPhase = ENodPhase::Idle;
			NodPhaseElapsed = 0.0f;
			UE_LOG(LogTemp, Log, TEXT("[NOD][%s] complete offset=0.0"), *ActiveNodSource);
		}
		break;
	}
	default:
		break;
	}

	CrimsonGazeMorphComponent->SetNodPitchDegrees(CurrentNodPitchDegrees);
}

void ARealtimeTestActor::SetupHandGesture()
{
	ResetHandGesture();
	if (!CharacterActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GESTURE][MANUAL] setup failed: CharacterActor unavailable"));
		return;
	}

	TArray<USkeletalMeshComponent*> Components;
	CharacterActor->GetComponents<USkeletalMeshComponent>(Components);
	for (USkeletalMeshComponent* Component : Components)
	{
		if (Component && Component->GetFName() == TEXT("Body"))
		{
			CachedBodyComponent = Component;
			break;
		}
	}
	if (!CachedBodyComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GESTURE][MANUAL] setup failed: MetaHuman Body component unavailable"));
		return;
	}

	for (const FName BoneName : { FName(TEXT("clavicle_r")), FName(TEXT("upperarm_r")),
		FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")) })
	{
		if (CachedBodyComponent->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("[GESTURE][MANUAL] setup failed: bone=%s unavailable mesh=%s"),
				*BoneName.ToString(), CachedBodyComponent->GetSkinnedAsset()
					? *CachedBodyComponent->GetSkinnedAsset()->GetPathName() : TEXT("None"));
			CachedBodyComponent = nullptr;
			return;
		}
	}

	// AnimBP/Post Process評価が終わり、描画側が最終姿勢を使う直前にだけoffsetを加える。
	// Base Poseを保存・復元しないため、Idle Animation等を巻き戻さない。
	BodyBoneTransformsFinalizedHandle = CachedBodyComponent->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(
			this, &ARealtimeTestActor::HandleBodyBoneTransformsFinalized));
	UE_LOG(LogTemp, Log, TEXT("[GESTURE][MANUAL] ready key=Z body=%s anim=%s bones=clavicle_r/upperarm_r/lowerarm_r/hand_r"),
		*CachedBodyComponent->GetName(), CachedBodyComponent->GetAnimInstance()
			? *CachedBodyComponent->GetAnimInstance()->GetClass()->GetPathName() : TEXT("None"));
}

void ARealtimeTestActor::ResetHandGesture()
{
	if (CachedBodyComponent && BodyBoneTransformsFinalizedHandle.IsValid())
	{
		CachedBodyComponent->UnregisterOnBoneTransformsFinalizedDelegate(BodyBoneTransformsFinalizedHandle);
	}
	BodyBoneTransformsFinalizedHandle.Reset();
	CachedBodyComponent = nullptr;
	HandGesturePhase = EHandGesturePhase::Idle;
	HandGesturePhaseElapsed = 0.0f;
	HandGestureAlpha = 0.0f;
	PendingHandGestureSource.Reset();
	PendingHandGestureId.Reset();
}

void ARealtimeTestActor::TestGesture()
{
	RequestHandGesture(TEXT("raise_right_arm"), TEXT("MANUAL"));
}

void ARealtimeTestActor::HandleCycleGestureKeyPressed()
{
	// 単純な右手上げはZキーで確認できるため、XキーはPhase Cの3種類だけを巡回する。
	static const TArray<FString> Gestures = {
		TEXT("wave_right"), TEXT("shrug_right"), TEXT("palm_up_right") };
	const FString& Gesture = Gestures[DebugGestureCycleIndex % Gestures.Num()];
	const FString Result = RequestHandGesture(Gesture, TEXT("MANUAL"));
	if (Result.Contains(TEXT("\"status\":\"applied\"")))
	{
		DebugGestureCycleIndex = (DebugGestureCycleIndex + 1) % Gestures.Num();
	}
}

float ARealtimeTestActor::GetActiveHandGestureHoldSeconds() const
{
	if (ActiveHandGestureId == TEXT("wave_right"))
	{
		return 0.85f;
	}
	if (ActiveHandGestureId == TEXT("palm_up_right"))
	{
		return 0.55f;
	}
	if (ActiveHandGestureId == TEXT("shrug_right"))
	{
		return 0.40f;
	}
	return HandGestureHoldSeconds;
}

void ARealtimeTestActor::HandleCycleGestureAiTestKeyPressed()
{
	static const TArray<FString> Gestures = {
		TEXT("raise_right_arm"), TEXT("wave_right"), TEXT("shrug_right"), TEXT("palm_up_right") };
	const FString& Gesture = Gestures[DebugGestureAiTestCycleIndex % Gestures.Num()];
	const FString Text = FString::Printf(TEXT("Gesture test: %s"), *Gesture);
	const bool bSent = RealtimeVoice && RealtimeVoice->SendTextMessage(Text);
	if (bSent)
	{
		DebugGestureAiTestCycleIndex = (DebugGestureAiTestCycleIndex + 1) % Gestures.Num();
	}
	if (bSent)
	{
		UE_LOG(LogTemp, Log, TEXT("[GESTURE][AI_TEST] sent text=%s"), *Text);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[GESTURE][AI_TEST] send_failed text=%s"), *Text);
	}
}

FString ARealtimeTestActor::RequestHandGesture(const FString& GestureId, const TCHAR* SourceTag)
{
	const FString SafeSourceTag = SourceTag ? SourceTag : TEXT("UNKNOWN");
	static const TSet<FString> Supported = {
		TEXT("raise_right_arm"), TEXT("wave_right"), TEXT("shrug_right"), TEXT("palm_up_right") };
	const FString NormalizedGesture = GestureId.ToLower();
	if (!Supported.Contains(NormalizedGesture))
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"unknown_gesture\"}");
	}
	if (!CachedBodyComponent)
	{
		return TEXT("{\"status\":\"error\",\"reason\":\"gesture_backend_unavailable\"}");
	}
	if (HandGesturePhase != EHandGesturePhase::Idle || !PendingHandGestureId.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] ignored busy requested=%s"), *SafeSourceTag, *NormalizedGesture);
		return TEXT("{\"status\":\"ignored\",\"reason\":\"gesture_busy\"}");
	}
	if (SafeSourceTag == TEXT("LEGACY") || SafeSourceTag == TEXT("REALTIME") || SafeSourceTag == TEXT("AI_TEST"))
	{
		bHandGestureTriggeredForCurrentAssistantResponse = true;
		PendingHandGestureSource = SafeSourceTag;
		PendingHandGestureId = NormalizedGesture;
		UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] queued gesture=%s until assistant playback"),
			*SafeSourceTag, *NormalizedGesture);
	}
	else
	{
		StartHandGestureNow(NormalizedGesture, SafeSourceTag);
	}
	return FString::Printf(TEXT("{\"status\":\"applied\",\"gesture\":\"%s\"}"), *NormalizedGesture);
}

void ARealtimeTestActor::StartHandGestureNow(const FString& GestureId, const FString& SourceTag)
{
	ActiveHandGestureId = GestureId;
	ActiveHandGestureSource = SourceTag;
	HandGesturePhase = EHandGesturePhase::Raise;
	HandGesturePhaseElapsed = 0.0f;
	HandGestureAlpha = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] start gesture=%s"), *ActiveHandGestureSource, *ActiveHandGestureId);
	if (ActiveHandGestureId == TEXT("wave_right") && CachedBodyComponent)
	{
		const TArray<FTransform>& ComponentTransforms = CachedBodyComponent->GetComponentSpaceTransforms();
		const USkeletalMesh* BodyMesh = CachedBodyComponent->GetSkeletalMeshAsset();
		const int32 UpperIndex = CachedBodyComponent->GetBoneIndex(TEXT("upperarm_r"));
		const int32 HandIndex = CachedBodyComponent->GetBoneIndex(TEXT("hand_r"));
		if (BodyMesh && ComponentTransforms.IsValidIndex(UpperIndex) && ComponentTransforms.IsValidIndex(HandIndex))
		{
			const FReferenceSkeleton& RefSkeleton = BodyMesh->GetRefSkeleton();
			const int32 UpperParentIndex = RefSkeleton.GetParentIndex(UpperIndex);
			const int32 HandParentIndex = RefSkeleton.GetParentIndex(HandIndex);
			if (ComponentTransforms.IsValidIndex(UpperParentIndex) && ComponentTransforms.IsValidIndex(HandParentIndex))
			{
				const FTransform UpperLocal = ComponentTransforms[UpperIndex].GetRelativeTransform(ComponentTransforms[UpperParentIndex]);
				const FTransform HandLocal = ComponentTransforms[HandIndex].GetRelativeTransform(ComponentTransforms[HandParentIndex]);
				UE_LOG(LogTemp, Log,
					TEXT("[GESTURE][LOCAL_AXIS] upperarm_r X=%s Y=%s Z=%s hand_r X=%s Y=%s Z=%s"),
					*UpperLocal.GetRotation().GetAxisX().ToCompactString(),
					*UpperLocal.GetRotation().GetAxisY().ToCompactString(),
					*UpperLocal.GetRotation().GetAxisZ().ToCompactString(),
					*HandLocal.GetRotation().GetAxisX().ToCompactString(),
					*HandLocal.GetRotation().GetAxisY().ToCompactString(),
					*HandLocal.GetRotation().GetAxisZ().ToCompactString());
			}
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9120, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("[GESTURE][%s] %s"), *ActiveHandGestureSource, *ActiveHandGestureId), true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::StartPendingHandGestureAtPlayback()
{
	if (PendingHandGestureSource.IsEmpty() || PendingHandGestureId.IsEmpty())
	{
		return;
	}
	const FString Source = PendingHandGestureSource;
	const FString Gesture = PendingHandGestureId;
	PendingHandGestureSource.Reset();
	PendingHandGestureId.Reset();
	StartHandGestureNow(Gesture, Source);
}

void ARealtimeTestActor::HandleRealtimeAssistantStartedSpeaking()
{
	StartPendingHandGestureAtPlayback();
}

void ARealtimeTestActor::TickHandGesture(float DeltaTime)
{
	if (HandGesturePhase == EHandGesturePhase::Idle)
	{
		return;
	}
	HandGesturePhaseElapsed += DeltaTime;
	switch (HandGesturePhase)
	{
	case EHandGesturePhase::Raise:
	{
		const float T = FMath::Clamp(HandGesturePhaseElapsed / HandGestureRaiseSeconds, 0.0f, 1.0f);
		HandGestureAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, T, 2.0f);
		if (T >= 1.0f)
		{
			HandGesturePhase = EHandGesturePhase::Hold;
			HandGesturePhaseElapsed = 0.0f;
		}
		break;
	}
	case EHandGesturePhase::Hold:
		HandGestureAlpha = 1.0f;
		if (HandGesturePhaseElapsed >= GetActiveHandGestureHoldSeconds())
		{
			HandGesturePhase = EHandGesturePhase::Lower;
			HandGesturePhaseElapsed = 0.0f;
		}
		break;
	case EHandGesturePhase::Lower:
	{
		const float T = FMath::Clamp(HandGesturePhaseElapsed / HandGestureLowerSeconds, 0.0f, 1.0f);
		HandGestureAlpha = FMath::InterpEaseInOut(1.0f, 0.0f, T, 2.0f);
		if (T >= 1.0f)
		{
			HandGestureAlpha = 0.0f;
			HandGesturePhase = EHandGesturePhase::Idle;
			HandGesturePhaseElapsed = 0.0f;
			UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] complete offset=0.0"), *ActiveHandGestureSource);
		}
		break;
	}
	default:
		break;
	}
}

void ARealtimeTestActor::HandleBodyBoneTransformsFinalized()
{
	if (CachedBodyComponent && HandGestureAlpha > 0.0001f)
	{
		ApplyRightArmGestureOffset(HandGestureAlpha);
	}
}

void ARealtimeTestActor::ApplyRightArmGestureOffset(float Alpha)
{
	USkeletalMesh* Mesh = CachedBodyComponent ? CachedBodyComponent->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh)
	{
		return;
	}
	// Finalized通知はFlip後なので、現在描画に使われるRead Bufferへ限定して加算する。
	// 次フレームはAnimBPが新しいBase Poseを再評価するため、offsetは蓄積しない。
	TArray<FTransform>& Transforms = const_cast<TArray<FTransform>&>(CachedBodyComponent->GetComponentSpaceTransforms());
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

	auto RotateSubtree = [&Transforms, &RefSkeleton](int32 ParentIndex, const FQuat& Delta)
	{
		if (!Transforms.IsValidIndex(ParentIndex))
		{
			return;
		}
		const FVector Pivot = Transforms[ParentIndex].GetTranslation();
		for (int32 BoneIndex = 0; BoneIndex < Transforms.Num(); ++BoneIndex)
		{
			if (BoneIndex != ParentIndex && !RefSkeleton.BoneIsChildOf(BoneIndex, ParentIndex))
			{
				continue;
			}
			FTransform& BoneTransform = Transforms[BoneIndex];
			BoneTransform.SetTranslation(Pivot + Delta.RotateVector(BoneTransform.GetTranslation() - Pivot));
			BoneTransform.SetRotation((Delta * BoneTransform.GetRotation()).GetNormalized());
		}
	};
	auto TranslateSubtree = [&Transforms, &RefSkeleton](int32 ParentIndex, const FVector& Delta)
	{
		for (int32 BoneIndex = 0; BoneIndex < Transforms.Num(); ++BoneIndex)
		{
			if (BoneIndex == ParentIndex || RefSkeleton.BoneIsChildOf(BoneIndex, ParentIndex))
			{
				Transforms[BoneIndex].AddToTranslation(Delta);
			}
		}
	};

	const int32 UpperIndex = CachedBodyComponent->GetBoneIndex(TEXT("upperarm_r"));
	const int32 LowerIndex = CachedBodyComponent->GetBoneIndex(TEXT("lowerarm_r"));
	const int32 HandIndex = CachedBodyComponent->GetBoneIndex(TEXT("hand_r"));
	const int32 ClavicleIndex = CachedBodyComponent->GetBoneIndex(TEXT("clavicle_r"));

	if (ActiveHandGestureId == TEXT("shrug_right"))
	{
		// 肩を回すだけでは右手上げと見分けにくいため、肩甲帯全体を上へ持ち上げる。
		TranslateSubtree(ClavicleIndex, FVector(0.0f, 0.0f, 3.5f * Alpha));
		RotateSubtree(ClavicleIndex, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-5.0f * Alpha)));
		return;
	}

	float UpperDegrees = HandGestureUpperArmDegrees;
	float LowerDegrees = HandGestureLowerArmDegrees;
	float WristXDegrees = HandGestureWristDegrees;
	float UpperForwardDegrees = 0.0f;
	float HandRollDegrees = 0.0f;
	bool bAlignWaveUpperArmToFront = false;
	bool bAlignWaveForearmToFront = false;
	bool bAlignWavePalmToFront = false;
	bool bAlignPalmUpPose = false;
	float WristYDegrees = 0.0f;
	float WristZDegrees = 0.0f;
	if (ActiveHandGestureId == TEXT("wave_right"))
	{
		// 正常に表示できているraise_right_armを土台にし、肘だけを深く曲げる。
		// 上腕Xを強く回すと腕が胴体背面へ入り、肩の肌が露出するため使わない。
		UpperDegrees = 0.0f;
		LowerDegrees = 0.0f;
		WristXDegrees = 0.0f;
		// Component Spaceの固定Euler軸を推測せず、現在の上腕方向を
		// Jenniferの前上方へ合わせる。
		bAlignWaveUpperArmToFront = true;
		bAlignWaveForearmToFront = true;
		// 掌方向はhand_rの固定Local軸で決めず、指ボーンから実際の掌平面を求める。
		bAlignWavePalmToFront = true;
		if (HandGesturePhase == EHandGesturePhase::Hold)
		{
			WristZDegrees = FMath::Sin(HandGesturePhaseElapsed * 2.0f * PI * 3.0f) * 28.0f;
		}
	}
	else if (ActiveHandGestureId == TEXT("palm_up_right"))
	{
		UpperDegrees = 0.0f;
		LowerDegrees = 0.0f;
		WristXDegrees = 0.0f;
		bAlignPalmUpPose = true;
	}

	const FTransform BodyComponentTransform = CachedBodyComponent->GetComponentTransform();
	const FVector BodyForward = CharacterActor
		? BodyComponentTransform.InverseTransformVectorNoScale(CharacterActor->GetActorForwardVector()).GetSafeNormal()
		: FVector::ForwardVector;
	const FVector BodyRight = CharacterActor
		? BodyComponentTransform.InverseTransformVectorNoScale(CharacterActor->GetActorRightVector()).GetSafeNormal()
		: FVector::RightVector;
	const FVector BodyUp = CharacterActor
		? BodyComponentTransform.InverseTransformVectorNoScale(CharacterActor->GetActorUpVector()).GetSafeNormal()
		: FVector::UpVector;

	if ((bAlignWaveUpperArmToFront || bAlignPalmUpPose)
		&& Transforms.IsValidIndex(UpperIndex) && Transforms.IsValidIndex(LowerIndex))
	{
		const FVector CurrentUpperDirection =
			(Transforms[LowerIndex].GetTranslation() - Transforms[UpperIndex].GetTranslation()).GetSafeNormal();
		// Jennifer Actorの実Forward/Right/UpをBody Component Spaceへ変換した基準で、
		// 身体の前方・右外側・上方を合成する。
		const FVector DesiredUpperDirection = bAlignPalmUpPose
			? (BodyForward * 0.42f + BodyRight * 0.88f + BodyUp * 0.22f).GetSafeNormal()
			: (BodyForward * 0.25f + BodyRight * 1.00f + BodyUp * 0.45f).GetSafeNormal();
		const FQuat FullAlignment = FQuat::FindBetweenNormals(CurrentUpperDirection, DesiredUpperDirection);
		RotateSubtree(UpperIndex, FQuat::Slerp(FQuat::Identity, FullAlignment, Alpha).GetNormalized());
	}
	else
	{
		RotateSubtree(UpperIndex, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(UpperDegrees * Alpha)));
	}
	if (!FMath::IsNearlyZero(UpperForwardDegrees))
	{
		RotateSubtree(UpperIndex, FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(UpperForwardDegrees * Alpha)));
	}
	if ((bAlignWaveForearmToFront || bAlignPalmUpPose)
		&& Transforms.IsValidIndex(LowerIndex) && Transforms.IsValidIndex(HandIndex))
	{
		// 上腕整列後の実際の前腕方向を使う。固定Component X回転は手を背面へ
		// 戻していたため廃止し、手が確実に前上方へ来る方向へ直接合わせる。
		const FVector CurrentForearmDirection =
			(Transforms[HandIndex].GetTranslation() - Transforms[LowerIndex].GetTranslation()).GetSafeNormal();
		const FVector DesiredForearmDirection = bAlignPalmUpPose
			? (BodyForward * 0.62f + BodyRight * 0.18f + BodyUp * 0.10f).GetSafeNormal()
			: (BodyForward * 0.20f + BodyRight * 0.25f + BodyUp * 0.95f).GetSafeNormal();
		const FQuat FullForearmAlignment = FQuat::FindBetweenNormals(CurrentForearmDirection, DesiredForearmDirection);
		RotateSubtree(LowerIndex, FQuat::Slerp(FQuat::Identity, FullForearmAlignment, Alpha).GetNormalized());
	}
	else
	{
		RotateSubtree(LowerIndex, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(LowerDegrees * Alpha)));
	}
	RotateSubtree(HandIndex, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(WristXDegrees * Alpha)));
	if ((bAlignWavePalmToFront || bAlignPalmUpPose) && Transforms.IsValidIndex(HandIndex))
	{
		auto FindFirstBoneIndex = [this](std::initializer_list<const TCHAR*> Names) -> int32
		{
			for (const TCHAR* Name : Names)
			{
				const int32 Index = CachedBodyComponent->GetBoneIndex(FName(Name));
				if (Index != INDEX_NONE)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		};
		const int32 IndexFingerIndex = FindFirstBoneIndex({ TEXT("index_metacarpal_r"), TEXT("index_01_r") });
		const int32 MiddleFingerIndex = FindFirstBoneIndex({ TEXT("middle_metacarpal_r"), TEXT("middle_01_r") });
		const int32 PinkyFingerIndex = FindFirstBoneIndex({ TEXT("pinky_metacarpal_r"), TEXT("pinky_01_r") });
		if (Transforms.IsValidIndex(IndexFingerIndex) && Transforms.IsValidIndex(MiddleFingerIndex)
			&& Transforms.IsValidIndex(PinkyFingerIndex))
		{
			const FVector PalmAcross =
				(Transforms[PinkyFingerIndex].GetTranslation() - Transforms[IndexFingerIndex].GetTranslation()).GetSafeNormal();
			const FVector PalmLongitudinal =
				(Transforms[MiddleFingerIndex].GetTranslation() - Transforms[HandIndex].GetTranslation()).GetSafeNormal();
			FVector PalmNormal = FVector::CrossProduct(PalmAcross, PalmLongitudinal).GetSafeNormal();
			const FVector DesiredPalmNormal = bAlignPalmUpPose ? BodyUp : BodyForward;
			if (bAlignPalmUpPose)
			{
				// 実機でCross(PalmAcross, PalmLongitudinal)は手の甲側の法線だった。
				// palm_upでは表裏を入れ替えた実際の掌側法線をUpへ向ける。
				PalmNormal *= -1.0f;
			}
			else if (FVector::DotProduct(PalmNormal, DesiredPalmNormal) < 0.0f)
			{
				PalmNormal *= -1.0f;
			}
			const FQuat FullPalmAlignment = FQuat::FindBetweenNormals(PalmNormal, DesiredPalmNormal);
			RotateSubtree(HandIndex, FQuat::Slerp(FQuat::Identity, FullPalmAlignment, Alpha).GetNormalized());
			if (bAlignPalmUpPose)
			{
				// 実機では法線整列後も掌面が下を向いた。掌平面には表裏の識別情報が
				// ないため、確認済みの結果に基づき、整列後の手の長手方向を軸に反転する。
				const FVector AlignedPalmLongitudinal =
					(Transforms[MiddleFingerIndex].GetTranslation() - Transforms[HandIndex].GetTranslation()).GetSafeNormal();
				RotateSubtree(HandIndex,
					FQuat(AlignedPalmLongitudinal, FMath::DegreesToRadians(180.0f * Alpha)));
			}
		}
		else
		{
			static bool bLoggedMissingPalmBones = false;
			if (!bLoggedMissingPalmBones)
			{
				bLoggedMissingPalmBones = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[GESTURE][WAVE] palm alignment unavailable: finger bones not found"));
			}
		}
	}
	if (!FMath::IsNearlyZero(HandRollDegrees) && Transforms.IsValidIndex(HandIndex))
	{
		const FVector HandLongAxis = Transforms[HandIndex].GetRotation().GetAxisX();
		RotateSubtree(HandIndex, FQuat(HandLongAxis, FMath::DegreesToRadians(HandRollDegrees * Alpha)));
	}
	if (!FMath::IsNearlyZero(WristYDegrees))
	{
		RotateSubtree(HandIndex, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(WristYDegrees * Alpha)));
	}
	if (!FMath::IsNearlyZero(WristZDegrees))
	{
		RotateSubtree(HandIndex, FQuat(BodyUp, FMath::DegreesToRadians(WristZDegrees * Alpha)));
	}
}

void ARealtimeTestActor::StartLegacyVoice()
{
	if (!GetWorld() || !LegacyMicRecorder || !LegacyWhisper || !LegacyTTS)
	{
		return;
	}
	bLegacyVoiceEnabled = true;
	// gpt-4o-mini-ttsはinstructionsとPCM出力を正式サポートする。
	// Realtime音声経路には影響しないLegacy TTS専用設定。
	LegacyTTS->Model = TEXT("gpt-4o-mini-tts");
	LipSync->VoiceSource = nullptr;
	LipSync->TTSSource = LegacyTTS;
	GetWorldTimerManager().ClearTimer(LegacyStartRecordingTimerHandle);
	GetWorldTimerManager().SetTimer(LegacyStartRecordingTimerHandle, this,
		&ARealtimeTestActor::StartLegacyRecording, 2.0f, false);
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 従来方式(Whisper/Chat/TTS)を開始します"));
}

void ARealtimeTestActor::StopLegacyVoice()
{
	bLegacyVoiceEnabled = false;
	GetWorldTimerManager().ClearTimer(LegacyStartRecordingTimerHandle);
	GetWorldTimerManager().ClearTimer(LegacyRecordingMonitorTimerHandle);
	if (LegacyMicRecorder) LegacyMicRecorder->StopRecording();
	if (LegacyTTS) LegacyTTS->StopPlayback();
}

void ARealtimeTestActor::StartLegacyRecording()
{
	if (!bLegacyVoiceEnabled || !LegacyMicRecorder || LegacyTTS->IsPlaying()) return;
	if (LegacyMicRecorder->StartRecording())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[VOICE][LEGACY][TIMELINE] recording_start tts_playing=%s time=%.3f"),
			LegacyTTS->IsPlaying() ? TEXT("true") : TEXT("false"),
			FPlatformTime::Seconds());
		bLegacySpeechDetected = false;
		LegacySilenceElapsed = 0.0f;
		LegacyRecordingElapsed = 0.0f;
		LegacyVadDiagnosticElapsed = 0.0f;
		GetWorldTimerManager().SetTimer(LegacyRecordingMonitorTimerHandle, this,
			&ARealtimeTestActor::CheckLegacyRecording, 0.2f, true);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, TEXT("Listening... (Standard mode)"), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::CheckLegacyRecording()
{
	constexpr float MonitorIntervalSeconds = 0.2f;
	constexpr float SilenceDurationToStopSeconds = 1.8f;
	constexpr float MaxRecordDurationSeconds = 20.0f;
	// 実機診断で無音0.012～0.023、通常発話0.07～0.24を確認。
	// 旧0.25では発話を一度も検出できず、無音タイマーが開始しなかった。
	constexpr float VoiceActivityThreshold = 0.05f;

	if (!bLegacyVoiceEnabled) return;
	LegacyRecordingElapsed += MonitorIntervalSeconds;
	LegacyVadDiagnosticElapsed += MonitorIntervalSeconds;
	const float Rms = LegacyMicRecorder->GetRecentRms(MonitorIntervalSeconds);
	if (Rms >= VoiceActivityThreshold)
	{
		bLegacySpeechDetected = true;
		LegacySilenceElapsed = 0.0f;
	}
	else if (bLegacySpeechDetected)
	{
		LegacySilenceElapsed += MonitorIntervalSeconds;
	}
	if (LegacyVadDiagnosticElapsed >= 0.4f)
	{
		LegacyVadDiagnosticElapsed = 0.0f;
		UE_LOG(LogTemp, Log,
			TEXT("[VOICE][LEGACY][VAD] rms=%.3f threshold=%.3f silent_for=%.1f speaking=%s speech_detected=%s elapsed=%.1f"),
			Rms,
			VoiceActivityThreshold,
			LegacySilenceElapsed,
			Rms >= VoiceActivityThreshold ? TEXT("true") : TEXT("false"),
			bLegacySpeechDetected ? TEXT("true") : TEXT("false"),
			LegacyRecordingElapsed);
	}
	// 文中の自然な間を発話終了と誤判定しないよう1.8秒待つ。
	// 長めの発話を途中で切らないよう、安全上限も20秒まで延長する。
	const bool bSilenceTimeout = bLegacySpeechDetected
		&& LegacySilenceElapsed >= SilenceDurationToStopSeconds;
	const bool bMaxDuration = LegacyRecordingElapsed >= MaxRecordDurationSeconds;
	if (bSilenceTimeout || bMaxDuration)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[VOICE][LEGACY][VAD] stop reason=%s rms=%.3f silent_for=%.1f speech_detected=%s elapsed=%.1f"),
			bSilenceTimeout ? TEXT("silence") : TEXT("max_duration"),
			Rms,
			LegacySilenceElapsed,
			bLegacySpeechDetected ? TEXT("true") : TEXT("false"),
			LegacyRecordingElapsed);
		UE_LOG(LogTemp, Log,
			TEXT("[VOICE][LEGACY] 録音確定 reason=%s elapsed=%.1f silence=%.1f rms=%.3f"),
			bSilenceTimeout ? TEXT("silence") : TEXT("max_duration"),
			LegacyRecordingElapsed, LegacySilenceElapsed, Rms);
		GetWorldTimerManager().ClearTimer(LegacyRecordingMonitorTimerHandle);
		StopAndSubmitLegacyRecording();
	}
}

void ARealtimeTestActor::StopAndSubmitLegacyRecording()
{
	LegacyMicRecorder->StopRecording();
	LegacySpeechEndTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log,
		TEXT("[LATENCY][LEGACY] speech_end measurement_origin=recording_stop_after_silence_confirmation silence=%.1f sec"),
		LegacySilenceElapsed);
	if (!bLegacyVoiceEnabled) return;
	if (!LegacyMicRecorder->HasSignificantAudio())
	{
		GetWorldTimerManager().SetTimer(LegacyStartRecordingTimerHandle, this,
			&ARealtimeTestActor::StartLegacyRecording, 0.5f, false);
		return;
	}
	LegacyWhisper->TranscribeFromMicRecorder(LegacyMicRecorder);
}

void ARealtimeTestActor::HandleLegacyTranscriptionComplete(const FString& Text)
{
	if (!bLegacyVoiceEnabled) return;
	HandleUserTranscript(Text);
	if (LegacyChatManager) LegacyChatManager->SendMessage(Text);
}

void ARealtimeTestActor::HandleLegacyTranscriptionFailed(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeTestActor: Whisper失敗: %s"), *ErrorMessage);
	if (bLegacyVoiceEnabled) GetWorldTimerManager().SetTimer(LegacyStartRecordingTimerHandle, this,
		&ARealtimeTestActor::StartLegacyRecording, 1.0f, false);
}

void ARealtimeTestActor::HandleLegacyChatResponse(const FString& Text)
{
	if (!bLegacyVoiceEnabled) return;
	HandleAssistantTranscript(Text);
	const FString Emotion = LegacyChatManager
		? LegacyChatManager->GetCurrentLegacyEmotion()
		: TEXT("neutral");
	const float EmotionIntensity = LegacyChatManager
		? LegacyChatManager->GetCurrentLegacyEmotionIntensity()
		: 0.0f;
	LegacyTTS->SpeakTextWithEmotion(Text, Emotion, EmotionIntensity);
}

void ARealtimeTestActor::HandleLegacyTTSStarted()
{
	StartPendingHandGestureAtPlayback();
	UE_LOG(LogTemp, Log, TEXT("[VOICE][LEGACY][TIMELINE] tts_playback_start time=%.3f"),
		FPlatformTime::Seconds());
	if (LegacySpeechEndTimeSeconds <= 0.0)
	{
		return;
	}
	const double LatencySeconds = FPlatformTime::Seconds() - LegacySpeechEndTimeSeconds;
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY][TOTAL] speech_end_to_playback=%.3f sec"), LatencySeconds);
	UE_LOG(LogTemp, Log, TEXT("[EXPRESSION][LEGACY][LATENCY] user_speech_end_to_tts_start=%.3f sec"), LatencySeconds);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9105, 4.0f, FColor::Silver,
			FString::Printf(TEXT("[LEGACY LATENCY] speech end -> TTS %.2f sec"), LatencySeconds),
			true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::HandleLegacyTTSFinished()
{
	UE_LOG(LogTemp, Log,
		TEXT("[VOICE][LEGACY][TIMELINE] tts_playback_end tts_playing=%s time=%.3f"),
		LegacyTTS && LegacyTTS->IsPlaying() ? TEXT("true") : TEXT("false"),
		FPlatformTime::Seconds());
	if (bLegacyVoiceEnabled) StartLegacyRecording();
}

void ARealtimeTestActor::HandleLegacyTTSFailed(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeTestActor: TTS失敗: %s"), *ErrorMessage);
	if (bLegacyVoiceEnabled) StartLegacyRecording();
}

#if WITH_EDITOR
void ARealtimeTestActor::HandleSlatePreInputKeyDown(const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::F9 || Key == EKeys::Nine)
	{
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [INPUT] Slate事前入力で%sを検出"), *Key.ToString());
		HandleToggleRealtimeVoiceKeyPressed();
	}
}
#endif

void ARealtimeTestActor::HandleToggleRealtimeVoiceKeyPressed()
{
	if (!RealtimeVoice)
	{
		return;
	}

	if (RealtimeVoice->IsConnected())
	{
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [INPUT] Realtime APIから切断します"));
		RealtimeVoice->Disconnect();
		StartLegacyVoice();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("VOICE: Legacy (F9 / 9 = Realtime)"), true, FVector2D(1.25f, 1.25f));
		}
	}
	else
	{
		StopLegacyVoice();
		LipSync->TTSSource = nullptr;
		LipSync->VoiceSource = RealtimeVoice;
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [INPUT] Realtime APIへ接続します"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Silver, TEXT("VOICE: Connecting to Realtime API..."), true, FVector2D(1.25f, 1.25f));
		}
		RealtimeVoice->Connect();
	}
}

void ARealtimeTestActor::TeleportPlayerPawnTo(const FVector& Location, const FRotator& Rotation)
{
	// プレイヤーの視点は現在IntroFaceCameraに固定されているため、
	// Pawnではなくこのカメラアクター自体を動かす。VR時はここでも
	// 目の高さ補正を行う(IntroFaceCamera位置=カメラの実際の高さ基準ではなく、
	// HMDが上乗せする分を差し引いた位置を渡す必要があるため、呼び出し側で
	// 補正済みのLocationを渡すか、ここでVR時のみ補正するかを統一する。
	// ここでは呼び出し側から「補正前の目標位置」を受け取り、ここでVR判定込みで補正する)
	if (!IntroFaceCamera)
	{
		return;
	}

	FVector AdjustedLocation = Location;
	if (GEngine && GEngine->IsStereoscopic3D())
	{
		AdjustedLocation.Z -= VREyeHeightOffsetCm;
	}

	IntroFaceCamera->SetActorLocationAndRotation(AdjustedLocation, Rotation);
	UpdateJenniferConversationLightingTransform();
}

void ARealtimeTestActor::TeleportCharacterActorTo(const FVector& Location, const FRotator& Rotation)
{
	if (!CharacterActor)
	{
		return;
	}

	CharacterActor->SetActorLocationAndRotation(
		Location,
		Rotation,
		/*bSweep=*/ false,
		/*OutSweepHitResult=*/ nullptr,
		ETeleportType::TeleportPhysics);
	RestoreJenniferCanonicalScale(TEXT("TeleportCharacterActorTo"));
}

void ARealtimeTestActor::CaptureJenniferCanonicalScale()
{
	if (!CharacterActor || bHasJenniferCanonicalActorScale)
	{
		return;
	}
	JenniferCanonicalActorScale = CharacterActor->GetActorScale3D();
	bHasJenniferCanonicalActorScale = true;
	const FBox CharacterBounds = CharacterActor->GetComponentsBoundingBox(/*bNonColliding=*/ true);
	UE_LOG(LogTemp, Warning,
		TEXT("[SCENE_SCALE] Jennifer canonical captured actor=%s scale=%s rendered_bounds_height=%.2fcm"),
		*CharacterActor->GetName(), *JenniferCanonicalActorScale.ToString(), CharacterBounds.GetSize().Z);
}

void ARealtimeTestActor::RestoreJenniferCanonicalScale(const TCHAR* Context)
{
	if (!CharacterActor || !bHasJenniferCanonicalActorScale)
	{
		return;
	}
	const FVector CurrentScale = CharacterActor->GetActorScale3D();
	if (!CurrentScale.Equals(JenniferCanonicalActorScale, KINDA_SMALL_NUMBER))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SCENE_SCALE] Jennifer scale restored context=%s current=%s canonical=%s"),
			Context ? Context : TEXT("Unknown"), *CurrentScale.ToString(), *JenniferCanonicalActorScale.ToString());
	}
	CharacterActor->SetActorScale3D(JenniferCanonicalActorScale);
}

FVector ARealtimeTestActor::ResolveJenniferFaceTarget() const
{
	if (!CharacterActor)
	{
		return FVector::ZeroVector;
	}
	FVector FaceTarget = CharacterActor->GetActorLocation() + FVector(0.0f, 0.0f, 160.0f);
	if (OriginalPaytonMorphMesh && OriginalPaytonMorphMesh->IsRegistered())
	{
		OriginalPaytonMorphMesh->UpdateBounds();
		return OriginalPaytonMorphMesh->Bounds.Origin;
	}
	const FBox CharacterBounds = CharacterActor->GetComponentsBoundingBox(true);
	if (CharacterBounds.IsValid)
	{
		FaceTarget = FVector(CharacterBounds.GetCenter().X, CharacterBounds.GetCenter().Y,
			CharacterBounds.Max.Z - FMath::Clamp(CharacterBounds.GetExtent().Z * 0.12f, 10.0f, 30.0f));
	}
	return FaceTarget;
}

void ARealtimeTestActor::CaptureCanonicalConversationFraming()
{
	if (!CharacterActor || !IntroFaceCamera)
	{
		return;
	}
	const FVector FaceTarget = ResolveJenniferFaceTarget();
	CanonicalConversationCameraDistanceCm = FVector::Distance(IntroFaceCamera->GetActorLocation(), FaceTarget);
	if (UCameraComponent* CameraComponent = IntroFaceCamera->GetCameraComponent())
	{
		// BeginPlayで元のMyRoomカメラからコピーしたFOVをCanonical値として保持する。
		CameraComponent->SetFieldOfView(CanonicalConversationCameraFOVDegrees);
	}
	bHasCanonicalConversationFraming = CanonicalConversationCameraDistanceCm > KINDA_SMALL_NUMBER;
	UE_LOG(LogTemp, Warning,
		TEXT("[CAMERA][FRAMING] scene=MyRoom camera=%s face_target=%s distance=%.2fcm fov=%.2f canonical=true"),
		*IntroFaceCamera->GetActorLocation().ToString(), *FaceTarget.ToString(),
		CanonicalConversationCameraDistanceCm, CanonicalConversationCameraFOVDegrees);
}

void ARealtimeTestActor::ApplyConversationSceneExposure(EConversationLocation Location)
{
	if (!IntroFaceCamera || !IntroFaceCamera->GetCameraComponent())
	{
		return;
	}

	UCameraComponent* CameraComponent = IntroFaceCamera->GetCameraComponent();
	const float JenniferLightMultiplier =
		Location == EConversationLocation::Classroom ? 64.0f : 1.0f;
	const float JenniferBaseIntensities[] = { 700.0f, 260.0f, 140.0f };
	const FLinearColor JenniferBaseColors[] =
	{
		FLinearColor(1.0f, 0.93f, 0.86f),
		FLinearColor(0.88f, 0.94f, 1.0f),
		FLinearColor(1.0f, 0.96f, 0.90f)
	};
	for (int32 Index = 0;
		Index < JenniferConversationLights.Num() && Index < UE_ARRAY_COUNT(JenniferBaseIntensities);
		++Index)
	{
		if (JenniferConversationLights[Index])
		{
			USpotLightComponent* Light = JenniferConversationLights[Index];
			Light->SetVisibility(true);
			Light->Activate(true);
			Light->SetIntensity(JenniferBaseIntensities[Index] * JenniferLightMultiplier);
			Light->SetLightColor(JenniferBaseColors[Index]);
			Light->SetAttenuationRadius(350.0f);
			Light->SetInnerConeAngle(35.0f);
			Light->SetOuterConeAngle(60.0f);
			Light->MarkRenderStateDirty();
		}
	}
	bDiagnosticJenniferKeyLightProbeEnabled = false;
	if (CharacterActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> JenniferPrimitives(CharacterActor);
		for (UPrimitiveComponent* Primitive : JenniferPrimitives)
		{
			if (Primitive)
			{
				Primitive->SetLightingChannels(false, true, false);
			}
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[LIGHTING][JENNIFER] scene=%s scene_direct_channel=OFF dedicated_light_multiplier=%.2f"),
		*GetConversationLocationTagStem(Location), JenniferLightMultiplier);
	UpdateJenniferConversationLightingTransform();

	if (Location == EConversationLocation::MyRoom)
	{
		// AB_MANUAL_ZEROで「すごく良い」と確認されたMyRoomの基準状態を固定する。
		CameraComponent->PostProcessBlendWeight = 1.0f;
		CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
		CameraComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		CameraComponent->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		CameraComponent->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
		CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
		CameraComponent->PostProcessSettings.AutoExposureBias = 0.0f;
		UE_LOG(LogTemp, Warning,
			TEXT("[LIGHTING][EXPOSURE] scene=MyRoom method=manual physical=false override=true bias=0.00 blend=1.00 canonical=true"));
		LogJenniferConversationLightingDiagnostics(Location);
		return;
	}

	bool bOverrideExposure = false;
	float ExposureBias = 0.0f;
	const FConversationSceneConfig SceneConfig =
		LoadConversationSceneConfig(GetConversationLocationTagStem(Location));
	bOverrideExposure = SceneConfig.bHasExposureBias;
	ExposureBias = bOverrideExposure ? SceneConfig.ExposureBias : 0.0f;

	// MyRoomと同じ露出方式を共通条件とし、Scene差はBiasだけで診断する。
	CameraComponent->PostProcessBlendWeight = 1.0f;
	CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CameraComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	CameraComponent->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	CameraComponent->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
	CameraComponent->PostProcessSettings.AutoExposureBias = ExposureBias;
	UE_LOG(LogTemp, Warning,
		TEXT("[LIGHTING][EXPOSURE] scene=%s method=manual physical=false override=true bias=%.2f blend=1.00 configured_bias=%s"),
		*GetConversationLocationTagStem(Location),
		ExposureBias, bOverrideExposure ? TEXT("true") : TEXT("false"));
	LogJenniferConversationLightingDiagnostics(Location);
}

void ARealtimeTestActor::RefreshClassroomJenniferLightingAfterMove()
{
	if (CurrentConversationLocation != EConversationLocation::Classroom)
	{
		return;
	}

	// Scene移動直後はCamera/Meshの描画状態がまだ更新途中で、専用Spot Lightの
	// 初回設定が描画へ反映されない。8キー診断の復元と同じ処理を安定後に再適用する。
	ApplyConversationSceneExposure(EConversationLocation::Classroom);
	UE_LOG(LogTemp, Warning,
		TEXT("[LIGHTING][JENNIFER] scene=Classroom delayed_refresh=true delay=0.25"));
}

void ARealtimeTestActor::LogConversationCameraDiagnostics(EConversationLocation Location)
{
	if (!CharacterActor || !IntroFaceCamera)
	{
		return;
	}
	FBox FaceBounds(ForceInit);
	if (OriginalPaytonMorphMesh && OriginalPaytonMorphMesh->IsRegistered())
	{
		OriginalPaytonMorphMesh->UpdateBounds();
		FaceBounds = OriginalPaytonMorphMesh->Bounds.GetBox();
	}
	else
	{
		FaceBounds = CharacterActor->GetComponentsBoundingBox(true);
	}
	if (!FaceBounds.IsValid)
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	FVector2D ScreenTop;
	FVector2D ScreenBottom;
	float ScreenOccupancy = -1.0f;
	if (PC)
	{
		PC->GetViewportSize(ViewportX, ViewportY);
		const FVector Top(FaceBounds.GetCenter().X, FaceBounds.GetCenter().Y, FaceBounds.Max.Z);
		const FVector Bottom(FaceBounds.GetCenter().X, FaceBounds.GetCenter().Y, FaceBounds.Min.Z);
		if (ViewportY > 0 && PC->ProjectWorldLocationToScreen(Top, ScreenTop)
			&& PC->ProjectWorldLocationToScreen(Bottom, ScreenBottom))
		{
			ScreenOccupancy = FMath::Abs(ScreenBottom.Y - ScreenTop.Y) / static_cast<float>(ViewportY);
		}
	}
	const float FOV = IntroFaceCamera->GetCameraComponent()
		? IntroFaceCamera->GetCameraComponent()->FieldOfView : -1.0f;
	UE_LOG(LogTemp, Warning,
		TEXT("[SCENE_CAMERA][PROJECTED] scene=%s actor_scale=%s camera_distance=%.2fcm fov=%.2f face_bounds_height=%.2fcm viewport=%dx%d vertical_occupancy=%.4f"),
		*GetConversationLocationTagStem(Location), *CharacterActor->GetActorScale3D().ToString(),
		FVector::Distance(IntroFaceCamera->GetActorLocation(), FaceBounds.GetCenter()), FOV,
		FaceBounds.GetSize().Z, ViewportX, ViewportY, ScreenOccupancy);
	UE_LOG(LogTemp, Warning,
		TEXT("[CAMERA][FRAMING] scene=%s distance=%.2fcm fov=%.2f vertical_occupancy=%.4f"),
		*GetConversationLocationTagStem(Location),
		FVector::Distance(IntroFaceCamera->GetActorLocation(), ResolveJenniferFaceTarget()), FOV, ScreenOccupancy);
	LogRenderEnvironmentDiagnostics(Location);
	LogCrimsonRenderDiagnostics(Location);
}

void ARealtimeTestActor::LogRenderEnvironmentDiagnostics(EConversationLocation Location) const
{
	if (!GetWorld() || !IntroFaceCamera || !CharacterActor)
	{
		return;
	}
	const FString Scene = GetConversationLocationTagStem(Location);
	const FVector CameraLocation = IntroFaceCamera->GetActorLocation();
	const FVector JenniferLocation = CharacterActor->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("[RENDER_ENV] scene=%s camera=%s jennifer=%s dynamic_gi=0"),
		*Scene, *CameraLocation.ToString(), *JenniferLocation.ToString());

	TArray<AActor*> Volumes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APostProcessVolume::StaticClass(), Volumes);
	for (AActor* Actor : Volumes)
	{
		APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor);
		if (!Volume)
		{
			continue;
		}
		float DistanceToPoint = 0.0f;
		const bool bInside = Volume->bUnbound || Volume->EncompassesPoint(CameraLocation, 0.0f, &DistanceToPoint);
		const float EffectiveWeight = !bInside ? 0.0f
			: (Volume->bUnbound || Volume->BlendRadius <= KINDA_SMALL_NUMBER
				? Volume->BlendWeight
				: Volume->BlendWeight * FMath::Clamp(1.0f - DistanceToPoint / Volume->BlendRadius, 0.0f, 1.0f));
		const FPostProcessSettings& S = Volume->Settings;
		UE_LOG(LogTemp, Warning,
			TEXT("[RENDER_ENV] scene=%s pp_volume=%s inside=%s unbound=%s priority=%.2f blend_weight=%.3f effective_weight=%.3f blend_radius=%.1f distance=%.1f exposure_bias=%.2f exposure_override=%s saturation=%s contrast=%s white_temp=%.1f white_tint=%.2f bloom=%.2f vignette=%.2f ambient_cube=%.2f local_highlight=%.2f local_shadow=%.2f film_slope=%.2f film_toe=%.2f film_shoulder=%.2f"),
			*Scene, *Volume->GetName(), bInside ? TEXT("true") : TEXT("false"),
			Volume->bUnbound ? TEXT("true") : TEXT("false"), Volume->Priority,
			Volume->BlendWeight, EffectiveWeight, Volume->BlendRadius, DistanceToPoint,
			S.AutoExposureBias, S.bOverride_AutoExposureBias ? TEXT("true") : TEXT("false"),
			*S.ColorSaturation.ToString(), *S.ColorContrast.ToString(), S.WhiteTemp, S.WhiteTint,
			S.BloomIntensity, S.VignetteIntensity, S.AmbientCubemapIntensity,
			S.LocalExposureHighlightContrastScale, S.LocalExposureShadowContrastScale,
			S.FilmSlope, S.FilmToe, S.FilmShoulder);
	}

	if (const UCameraComponent* Camera = IntroFaceCamera->GetCameraComponent())
	{
		const FPostProcessSettings& S = Camera->PostProcessSettings;
		UE_LOG(LogTemp, Warning,
			TEXT("[RENDER_ENV] scene=%s camera_pp blend_weight=%.3f exposure_override=%s exposure_bias=%.2f saturation=%s contrast=%s white_temp=%.1f white_tint=%.2f bloom=%.2f vignette=%.2f ambient_cube=%.2f"),
			*Scene, Camera->PostProcessBlendWeight,
			S.bOverride_AutoExposureBias ? TEXT("true") : TEXT("false"), S.AutoExposureBias,
			*S.ColorSaturation.ToString(), *S.ColorContrast.ToString(), S.WhiteTemp, S.WhiteTint,
			S.BloomIntensity, S.VignetteIntensity, S.AmbientCubemapIntensity);
	}
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const APlayerCameraManager* PCM = PC->PlayerCameraManager)
		{
			const FMinimalViewInfo& POV = PCM->GetCameraCacheView();
			const FPostProcessSettings& S = POV.PostProcessSettings;
			UE_LOG(LogTemp, Warning,
				TEXT("[RENDER_ENV] scene=%s final_pov location=%s rotation=%s fov=%.2f pp_blend=%.3f exposure=%.2f saturation=%s contrast=%s white_temp=%.1f bloom=%.2f vignette=%.2f ambient_cube=%.2f local_highlight=%.2f local_shadow=%.2f"),
				*Scene, *POV.Location.ToString(), *POV.Rotation.ToString(), POV.FOV,
				POV.PostProcessBlendWeight, S.AutoExposureBias, *S.ColorSaturation.ToString(),
				*S.ColorContrast.ToString(), S.WhiteTemp, S.BloomIntensity, S.VignetteIntensity,
				S.AmbientCubemapIntensity, S.LocalExposureHighlightContrastScale,
				S.LocalExposureShadowContrastScale);
		}
	}

	TArray<AActor*> SkyLights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASkyLight::StaticClass(), SkyLights);
	for (AActor* Actor : SkyLights)
	{
		const ASkyLight* Sky = Cast<ASkyLight>(Actor);
		const USkyLightComponent* C = Sky ? Sky->GetLightComponent() : nullptr;
		if (!C) continue;
		UE_LOG(LogTemp, Warning,
			TEXT("[RENDER_ENV] scene=%s skylight=%s intensity=%.2f visible=%s mobility=%d source=%d cubemap=%s realtime=%s lower_black=%s lower_color=%s"),
			*Scene, *Sky->GetName(), C->Intensity, C->IsVisible() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(C->Mobility), static_cast<int32>(C->SourceType),
			C->Cubemap ? *C->Cubemap->GetPathName() : TEXT("None"),
			C->bRealTimeCapture ? TEXT("true") : TEXT("false"),
			C->bLowerHemisphereIsBlack ? TEXT("true") : TEXT("false"),
			*C->LowerHemisphereColor.ToString());
	}

	TArray<AActor*> Captures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AReflectionCapture::StaticClass(), Captures);
	for (AActor* Actor : Captures)
	{
		const AReflectionCapture* Capture = Cast<AReflectionCapture>(Actor);
		UReflectionCaptureComponent* C = Capture ? Capture->GetCaptureComponent() : nullptr;
		if (!C) continue;
		C->UpdateBounds();
		const FBox Bounds = C->Bounds.GetBox();
		UE_LOG(LogTemp, Warning,
			TEXT("[RENDER_ENV] scene=%s reflection_capture=%s type=%s camera_inside=%s jennifer_inside=%s distance_camera=%.1f distance_jennifer=%.1f bounds_extent=%s brightness=%.2f"),
			*Scene, *Capture->GetName(), *Capture->GetClass()->GetName(),
			Bounds.IsInside(CameraLocation) ? TEXT("true") : TEXT("false"),
			Bounds.IsInside(JenniferLocation) ? TEXT("true") : TEXT("false"),
			FVector::Distance(CameraLocation, Capture->GetActorLocation()),
			FVector::Distance(JenniferLocation, Capture->GetActorLocation()),
			*Bounds.GetExtent().ToString(), C->Brightness);
	}
}

void ARealtimeTestActor::LogCrimsonRenderDiagnostics(EConversationLocation Location) const
{
	if ((Location != EConversationLocation::MyRoom && Location != EConversationLocation::Classroom)
		|| !CrimsonGazeMorphComponent)
	{
		return;
	}
	const FString Scene = GetConversationLocationTagStem(Location);
	const FLightingChannels Channels = CrimsonGazeMorphComponent->LightingChannels;
	const FBoxSphereBounds CrimsonBounds = CrimsonGazeMorphComponent->Bounds;
	const FVector CameraLocation = IntroFaceCamera ? IntroFaceCamera->GetActorLocation() : FVector::ZeroVector;
	const float CameraDistance = IntroFaceCamera
		? FVector::Distance(CameraLocation, CrimsonBounds.Origin) : -1.0f;
	const float CameraFOV = IntroFaceCamera && IntroFaceCamera->GetCameraComponent()
		? IntroFaceCamera->GetCameraComponent()->FieldOfView : -1.0f;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	float ProjectedScreenHeight = -1.0f;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->GetViewportSize(ViewportX, ViewportY);
		FVector2D ScreenTop;
		FVector2D ScreenBottom;
		const FVector Top = CrimsonBounds.Origin + FVector::UpVector * CrimsonBounds.BoxExtent.Z;
		const FVector Bottom = CrimsonBounds.Origin - FVector::UpVector * CrimsonBounds.BoxExtent.Z;
		if (ViewportY > 0 && PC->ProjectWorldLocationToScreen(Top, ScreenTop)
			&& PC->ProjectWorldLocationToScreen(Bottom, ScreenBottom))
		{
			ProjectedScreenHeight = FMath::Abs(ScreenBottom.Y - ScreenTop.Y) / static_cast<float>(ViewportY);
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[CRIMSON_RENDER] scene=%s component=%s mesh=%s visible=%s hidden_game=%s active=%s cast_shadow=%s receive_decals=%s main_pass=%s custom_depth=%s stencil=%d channels=%d%d%d predicted_lod=%d forced_lod=%d min_lod=%d bounds_origin=%s bounds_radius=%.2f scale=%s materials=%d camera_distance=%.2f fov=%.2f viewport=%dx%d projected_screen_height=%.5f"),
		*Scene, *CrimsonGazeMorphComponent->GetName(),
		CrimsonGazeMorphComponent->GetSkeletalMeshAsset() ? *CrimsonGazeMorphComponent->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"),
		CrimsonGazeMorphComponent->IsVisible() ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->bHiddenInGame ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->IsActive() ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->CastShadow ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->bReceivesDecals ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->bRenderInMainPass ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->bRenderCustomDepth ? TEXT("true") : TEXT("false"),
		CrimsonGazeMorphComponent->CustomDepthStencilValue,
		Channels.bChannel0 ? 1 : 0, Channels.bChannel1 ? 1 : 0, Channels.bChannel2 ? 1 : 0,
		CrimsonGazeMorphComponent->GetPredictedLODLevel(), CrimsonGazeMorphComponent->GetForcedLOD(),
		CrimsonGazeMorphComponent->MinLodModel, *CrimsonBounds.Origin.ToString(),
		CrimsonBounds.SphereRadius, *CrimsonGazeMorphComponent->GetComponentScale().ToString(),
		CrimsonGazeMorphComponent->GetNumMaterials(), CameraDistance, CameraFOV,
		ViewportX, ViewportY, ProjectedScreenHeight);

	for (int32 Slot = 0; Slot < CrimsonGazeMorphComponent->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Material = CrimsonGazeMorphComponent->GetMaterial(Slot);
		const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
		UE_LOG(LogTemp, Warning,
			TEXT("[CRIMSON_RENDER][MATERIAL] scene=%s slot=%d slot_name=%s material=%s class=%s dynamic=%s blend_mode=%d shading_models=0x%04x two_sided=%s"),
			*Scene, Slot, *CrimsonGazeMorphComponent->GetMaterialSlotNames()[Slot].ToString(),
			Material ? *Material->GetPathName() : TEXT("None"), Material ? *Material->GetClass()->GetName() : TEXT("None"),
			Dynamic ? TEXT("true") : TEXT("false"), Material ? static_cast<int32>(Material->GetBlendMode()) : -1,
			Material ? Material->GetShadingModels().GetShadingModelField() : 0,
			Material && Material->IsTwoSided() ? TEXT("true") : TEXT("false"));
		if (!Material)
		{
			continue;
		}

		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			float Value = 0.0f;
			if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Value))
			{
				UE_LOG(LogTemp, Warning, TEXT("[CRIMSON_RENDER][PARAM] scene=%s slot=%d type=scalar name=%s value=%.6f"),
					*Scene, Slot, *Info.Name.ToString(), Value);
			}
		}
		Infos.Reset(); Ids.Reset();
		Material->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			FLinearColor Value;
			if (Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Value))
			{
				UE_LOG(LogTemp, Warning, TEXT("[CRIMSON_RENDER][PARAM] scene=%s slot=%d type=vector name=%s value=%s"),
					*Scene, Slot, *Info.Name.ToString(), *Value.ToString());
			}
		}
		Infos.Reset(); Ids.Reset();
		Material->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UTexture* Value = nullptr;
			if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Info), Value))
			{
				UE_LOG(LogTemp, Warning, TEXT("[CRIMSON_RENDER][PARAM] scene=%s slot=%d type=texture name=%s value=%s"),
					*Scene, Slot, *Info.Name.ToString(), Value ? *Value->GetPathName() : TEXT("None"));
			}
		}
	}
}

UCameraComponent* ARealtimeTestActor::FindCameraComponentByName(AActor* Actor, FName ComponentName)
{
	if (!Actor)
	{
		return nullptr;
	}

	TArray<UCameraComponent*> Cameras;
	Actor->GetComponents<UCameraComponent>(Cameras);
	for (UCameraComponent* Camera : Cameras)
	{
		if (Camera->GetFName() == ComponentName)
		{
			return Camera;
		}
	}
	return nullptr;
}

USceneComponent* ARealtimeTestActor::FindSceneComponentByName(AActor* Actor, FName ComponentName)
{
	if (!Actor)
	{
		return nullptr;
	}

	TArray<USceneComponent*> Components;
	Actor->GetComponents<USceneComponent>(Components);
	for (USceneComponent* Component : Components)
	{
		if (Component->GetFName() == ComponentName)
		{
			return Component;
		}
	}
	return nullptr;
}

void ARealtimeTestActor::EnforceFrontCameraOffset()
{
	if (!CachedFrontCameraMount)
	{
		return;
	}

	FVector FinalLocation = DesiredFrontCameraOffset;
	if (GEngine && GEngine->IsStereoscopic3D())
	{
		FinalLocation.Z -= VehicleCockpitVREyeHeightOffsetCm;
	}
	CachedFrontCameraMount->SetRelativeLocation(FinalLocation);
}

void ARealtimeTestActor::ActivateVehicleCockpitCamera()
{
	if (!VehiclePawn)
	{
		return;
	}

	if (UCameraComponent* FrontCamera = FindCameraComponentByName(VehiclePawn, TEXT("FrontCamera")))
	{
		FrontCamera->SetActive(true);

		// 空が明るく自動露出でPaytonが真っ黒に潰れてしまうため、このカメラだけ
		// 自動露出を切って、明るさを手動で固定する
		FrontCamera->PostProcessBlendWeight = 1.0f;
		FrontCamera->PostProcessSettings.bOverride_AutoExposureMethod = true;
		FrontCamera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		FrontCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
		FrontCamera->PostProcessSettings.AutoExposureBias = 7.0f;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 車にFrontCameraが見つかりませんでした"));
	}

	// VRではFrontCamera自身の位置はbLockToHmdにより毎フレームHMDトラッキングで
	// 上書きされてしまうため、位置調整は親のFrontSpringArmに対して行う
	if (USceneComponent* FrontSpringArm = FindSceneComponentByName(VehiclePawn, TEXT("FrontSpringArm")))
	{
		CachedFrontCameraMount = FrontSpringArm;

		// 前回、矢印キーで調整して保存した位置(非VR時の基準位置)を読み込む。
		// なければ現在の値(Blueprintのデフォルト)を基準とする
		DesiredFrontCameraOffset = FrontSpringArm->GetRelativeLocation();
		FString SavedText;
		FVector SavedOffset;
		if (FFileHelper::LoadFileToString(SavedText, *GetCockpitCameraConfigPath()) && SavedOffset.InitFromString(SavedText))
		{
			DesiredFrontCameraOffset = SavedOffset;
			UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 保存済みのFrontCamera位置を復元しました: %s"), *SavedOffset.ToString());
		}

		// 前回、[ ]キーで調整して保存したVRの目の高さ補正値があれば復元する
		FString SavedHeightText;
		if (FFileHelper::LoadFileToString(SavedHeightText, *GetVREyeHeightConfigPath()))
		{
			VehicleCockpitVREyeHeightOffsetCm = FCString::Atof(*SavedHeightText);
		}

		// 車のBlueprint自身が毎フレームこの位置を上書きしてくることがあるため、
		// 一度だけでなく、Tickで毎フレーム強制的に再適用する(EnforceFrontCameraOffset)
		EnforceFrontCameraOffset();

		EnsureCockpitFillLights();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 車にFrontSpringArmが見つかりませんでした"));
	}

	if (UCameraComponent* BackCamera = FindCameraComponentByName(VehiclePawn, TEXT("BackCamera")))
	{
		BackCamera->SetActive(false);
	}
}

void ARealtimeTestActor::SetupPaytonNewFace()
{
	if (!CharacterActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CharacterActorが未設定のため、Face差し替えをスキップします"));
		return;
	}

	TArray<USkeletalMeshComponent*> SkeletalComps;
	CharacterActor->GetComponents<USkeletalMeshComponent>(SkeletalComps);

	USkeletalMeshComponent* FaceComp = nullptr;
	for (USkeletalMeshComponent* Comp : SkeletalComps)
	{
		if (Comp && Comp->GetFName() == TEXT("Face"))
		{
			FaceComp = Comp;
			break;
		}
	}

	if (!FaceComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: PaytonのFaceコンポーネントが見つかりませんでした"));
		return;
	}

	// MetaHuman Character Editorでフルリグからアセンブルした、口パク可能なFaceメッシュを使う。
	USkeletalMesh* NewFaceMesh = PaytonFaceMesh.Get();
	if (!NewFaceMesh)
	{
		NewFaceMesh = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/MetaHumans/SK_Payton_Identify_Final.SK_Payton_Identify_Final"));
	}
	if (NewFaceMesh)
	{
		FaceComp->SetSkeletalMesh(NewFaceMesh);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: SK_Payton_Identify_Finalのロードに失敗しました"));
	}

	// RigLogicは実際にはPost-Process AnimBPではなく、Face_AnimBPが内部で呼び出す
	// Control Rig(Face_ControlBoard_CtrlRig)経由で実行されている。Control Rigは
	// カーブではなくJawOpenAlpha等の「プレーンな変数」を直接入力として使うため、
	// カーブのRequired/Active Curvesフィルタ問題を完全に回避できる
	UClass* NewAnimClass = PaytonFaceAnimClass.Get();
	if (!NewAnimClass)
	{
		NewAnimClass = LoadClass<UAnimInstance>(nullptr,
			TEXT("/Game/MetaHumans/Common/Face/Face_AnimBP.Face_AnimBP_C"));
	}
	if (NewAnimClass)
	{
		FaceComp->SetAnimInstanceClass(NewAnimClass);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Face_AnimBPのロードに失敗しました"));
		return;
	}

	UAnimInstance* FaceAnim = FaceComp->GetAnimInstance();
	if (!FaceAnim)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Face_AnimBPのAnimInstance取得に失敗しました"));
		return;
	}

	CachedFaceAnimInstance = FaceAnim;
	CachedFacePostProcessInstance = FaceComp->GetPostProcessInstance();
	CachedFaceComponent = FaceComp;

	// 診断で確定した表示構成を通常動作にする。子Groomへは伝播させない。
	FaceComp->SetVisibility(bUseAnimatedPaytonFace, false);

	TArray<UStaticMeshComponent*> StaticMeshComps;
	CharacterActor->GetComponents<UStaticMeshComponent>(StaticMeshComps);
	for (UStaticMeshComponent* StaticComp : StaticMeshComps)
	{
		if (!StaticComp || StaticComp->GetFName() != TEXT("StaticMesh"))
		{
			continue;
		}

		UStaticMesh* HairMesh = PaytonHairMesh.Get();
		if (!HairMesh)
		{
			HairMesh = LoadObject<UStaticMesh>(nullptr,
				TEXT("/Game/Payton_Hair_Only.Payton_Hair_Only"));
		}

		if (HairMesh)
		{
			USkeletalMesh* OriginalRiggedMesh = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Meshy_Ruby_AnimatedMouth_V1/SK_Meshy_Ruby_AnimatedMouth_V1.SK_Meshy_Ruby_AnimatedMouth_V1"));
			if (OriginalRiggedMesh)
			{
				OriginalPaytonPoseableMesh = NewObject<UPoseableMeshComponent>(CharacterActor, TEXT("OriginalPaytonPoseableMesh"));
				OriginalPaytonPoseableMesh->SetSkeletalMesh(OriginalRiggedMesh);
				OriginalPaytonPoseableMesh->SetupAttachment(StaticComp->GetAttachParent());
				FTransform MeshyDisplayTransform = StaticComp->GetRelativeTransform();
				MeshyDisplayTransform.SetScale3D(MeshyDisplayTransform.GetScale3D() * 0.92f);
				MeshyDisplayTransform.AddToTranslation(FVector(0.0f, -4.0f, 3.0f));
				OriginalPaytonPoseableMesh->SetRelativeTransform(MeshyDisplayTransform);
				OriginalPaytonPoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				OriginalPaytonPoseableMesh->RegisterComponent();
				// 骨姿勢の計算元として保持し、実描画はMorph Target対応の
				// SkeletalMeshComponentへ任せる。
				OriginalPaytonPoseableMesh->SetVisibility(false, false);

				// Meshy FBXに同梱された独立口内は、閉口時にも上下唇の間へ黒い線として
				// 露出する。このセクションだけを描画対象から完全に外す。
				if (const FSkeletalMeshRenderData* MouthRenderData = OriginalRiggedMesh->GetResourceForRendering())
				{
					if (MouthRenderData->LODRenderData.Num() > 0)
					{
						const FSkeletalMeshLODRenderData& MouthLOD = MouthRenderData->LODRenderData[0];
						const TArray<FSkeletalMaterial>& MeshMaterials = OriginalRiggedMesh->GetMaterials();
						for (int32 SectionIndex = 0; SectionIndex < MouthLOD.RenderSections.Num(); ++SectionIndex)
						{
							const int32 MaterialIndex = MouthLOD.RenderSections[SectionIndex].MaterialIndex;
							if (!MeshMaterials.IsValidIndex(MaterialIndex))
							{
								continue;
							}
							const FString SlotName = MeshMaterials[MaterialIndex].MaterialSlotName.ToString();
							if (SlotName.Contains(TEXT("MouthInterior"), ESearchCase::IgnoreCase)
								|| SlotName.Contains(TEXT("MouthCavity"), ESearchCase::IgnoreCase))
							{
								OriginalPaytonPoseableMesh->ShowMaterialSection(MaterialIndex, SectionIndex, false, 0);
								UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 黒線除去 口内Sectionを非表示 Material=%d Section=%d Slot=%s"),
									MaterialIndex, SectionIndex, *SlotName);
							}
						}
					}
				}

				OriginalPaytonMorphMesh = NewObject<USkeletalMeshComponent>(CharacterActor, TEXT("OriginalPaytonMorphMesh"));
				OriginalPaytonMorphMesh->SetSkeletalMesh(OriginalRiggedMesh);
				OriginalPaytonMorphMesh->SetupAttachment(StaticComp->GetAttachParent());
				OriginalPaytonMorphMesh->SetRelativeTransform(MeshyDisplayTransform);
				OriginalPaytonMorphMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				OriginalPaytonMorphMesh->RegisterComponent();
				// 口と瞬きのMorphは、この表示Mesh自身で評価する。
				// PoseableMeshをLeaderにするとFollower自身のMorphが描画へ届かない。
				OriginalPaytonMorphMesh->SetLeaderPoseComponent(nullptr, false, false);
				OriginalPaytonMorphMesh->SetVisibility(true, false);

				// Blenderの既存MouthTeethPreview V3から取り出した上歯。顔と同じ
				// 座標系で書き出しているため、同じ表示Transformで正確に重なる。
				if (UStaticMesh* UpperTeethMesh = LoadObject<UStaticMesh>(nullptr,
					TEXT("/Game/Meshy_Ruby_UpperTeeth/SM_Meshy_Ruby_UpperTeeth.SM_Meshy_Ruby_UpperTeeth")))
				{
					OriginalPaytonUpperTeethComponent = NewObject<UStaticMeshComponent>(
						CharacterActor, TEXT("OriginalPaytonUpperTeethComponent"));
					OriginalPaytonUpperTeethComponent->SetStaticMesh(UpperTeethMesh);
					OriginalPaytonUpperTeethComponent->SetupAttachment(StaticComp->GetAttachParent());
					OriginalPaytonUpperTeethComponent->SetRelativeTransform(MeshyDisplayTransform);
					OriginalPaytonUpperTeethComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					OriginalPaytonUpperTeethComponent->RegisterComponent();
					OriginalPaytonUpperTeethComponent->SetVisibility(false, false);
				}
				UMorphTarget* LeftBlinkMorph = OriginalPaytonMorphMesh->FindMorphTarget(TEXT("eyeBlinkLeft"));
				UMorphTarget* RightBlinkMorph = OriginalPaytonMorphMesh->FindMorphTarget(TEXT("eyeBlinkRight"));
				UMorphTarget* MouthSealMorph = OriginalPaytonMorphMesh->FindMorphTarget(TEXT("mouthSeal"));
				UMorphTarget* JawOpenMorph = OriginalPaytonMorphMesh->FindMorphTarget(TEXT("jawOpen"));
				const int32 LeftBlinkDeltaCount = LeftBlinkMorph && LeftBlinkMorph->GetMorphLODModels().Num() > 0
					? LeftBlinkMorph->GetMorphLODModels()[0].Vertices.Num() : 0;
				const int32 RightBlinkDeltaCount = RightBlinkMorph && RightBlinkMorph->GetMorphLODModels().Num() > 0
					? RightBlinkMorph->GetMorphLODModels()[0].Vertices.Num() : 0;
				auto GetMorphDeltaStats = [](UMorphTarget* Morph, float& OutAverage, float& OutMaximum)
				{
					OutAverage = 0.0f;
					OutMaximum = 0.0f;
					if (!Morph || Morph->GetMorphLODModels().Num() == 0)
					{
						return;
					}
					const TArray<FMorphTargetDelta>& Deltas = Morph->GetMorphLODModels()[0].Vertices;
					for (const FMorphTargetDelta& Delta : Deltas)
					{
						const float Distance = Delta.PositionDelta.Length();
						OutAverage += Distance;
						OutMaximum = FMath::Max(OutMaximum, Distance);
					}
					if (Deltas.Num() > 0)
					{
						OutAverage /= static_cast<float>(Deltas.Num());
					}
				};
				float LeftAverage = 0.0f;
				float LeftMaximum = 0.0f;
				float RightAverage = 0.0f;
				float RightMaximum = 0.0f;
				GetMorphDeltaStats(LeftBlinkMorph, LeftAverage, LeftMaximum);
				GetMorphDeltaStats(RightBlinkMorph, RightAverage, RightMaximum);
				UE_LOG(LogTemp, Warning,
					TEXT("RealtimeTestActor: ARKit瞬きMorph Left=%s Delta=%d Avg=%f Max=%f Right=%s Delta=%d Avg=%f Max=%f FollowerTick=true"),
					LeftBlinkMorph ? TEXT("true") : TEXT("false"), LeftBlinkDeltaCount, LeftAverage, LeftMaximum,
					RightBlinkMorph ? TEXT("true") : TEXT("false"), RightBlinkDeltaCount, RightAverage, RightMaximum);
				UE_LOG(LogTemp, Warning,
					TEXT("RealtimeTestActor: 同一表示Mesh口Morph mouthSeal=%s jawOpen=%s Leaderなし=true"),
					MouthSealMorph ? TEXT("true") : TEXT("false"),
					JawOpenMorph ? TEXT("true") : TEXT("false"));

				// Poseable表示時と同じく、独立口内セクションは描画しない。
				if (const FSkeletalMeshRenderData* MorphRenderData = OriginalRiggedMesh->GetResourceForRendering())
				{
					if (MorphRenderData->LODRenderData.Num() > 0)
					{
						const FSkeletalMeshLODRenderData& MorphLOD = MorphRenderData->LODRenderData[0];
						const TArray<FSkeletalMaterial>& MeshMaterials = OriginalRiggedMesh->GetMaterials();
						for (int32 SectionIndex = 0; SectionIndex < MorphLOD.RenderSections.Num(); ++SectionIndex)
						{
							const int32 MaterialIndex = MorphLOD.RenderSections[SectionIndex].MaterialIndex;
							if (!MeshMaterials.IsValidIndex(MaterialIndex)) continue;
							const FString SlotName = MeshMaterials[MaterialIndex].MaterialSlotName.ToString();
							if (SlotName.Contains(TEXT("MouthInterior"), ESearchCase::IgnoreCase)
								|| SlotName.Contains(TEXT("MouthCavity"), ESearchCase::IgnoreCase))
							{
								OriginalPaytonMorphMesh->ShowMaterialSection(MaterialIndex, SectionIndex, false, 0);
							}
						}
					}
				}

				// Crimson Gazeは比較表示専用。現在の口パクモデルを通常表示として保持し、F8で切り替える。
				USkeletalMesh* CrimsonGazeJawMesh = LoadObject<USkeletalMesh>(nullptr,
					TEXT("/Game/Meshy_Crimson_Gaze_HeadRig/SK_Crimson_Gaze_HeadRig.SK_Crimson_Gaze_HeadRig"));
				UStaticMesh* CrimsonGazeMesh = CrimsonGazeJawMesh ? nullptr : LoadObject<UStaticMesh>(nullptr,
					TEXT("/Game/Meshy_Crimson_Gaze/Meshy_AI_Crimson_Gaze_0816095605_texture.Meshy_AI_Crimson_Gaze_0816095605_texture"));
				UPrimitiveComponent* CrimsonPreviewComponent = nullptr;
				if (CrimsonGazeJawMesh)
				{
					CrimsonGazeMorphComponent = NewObject<UJenniferNodSkeletalMeshComponent>(CharacterActor, TEXT("CrimsonGazeMorphComponent"));
					CrimsonGazeMorphComponent->SetSkeletalMesh(CrimsonGazeJawMesh);
					CrimsonGazeMorphComponent->SetupAttachment(StaticComp->GetAttachParent());
					CrimsonGazeMorphComponent->SetRelativeTransform(StaticComp->GetRelativeTransform());
					CrimsonGazeMorphComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					CrimsonGazeMorphComponent->RegisterComponent();
					CrimsonGazeMorphComponent->SetVisibility(true, false);
					CrimsonGazeMorphComponent->SetLeaderPoseComponent(nullptr, false, false);
					// 顔へ埋め込まれた歯はMorph値0でも描画されるため、該当セクションを常時隠す。
					if (const FSkeletalMeshRenderData* RenderData = CrimsonGazeJawMesh->GetResourceForRendering())
					{
						if (RenderData->LODRenderData.Num() > 0)
						{
							const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[0];
							const TArray<FSkeletalMaterial>& Materials = CrimsonGazeJawMesh->GetMaterials();
							for (int32 SectionIndex = 0; SectionIndex < LOD.RenderSections.Num(); ++SectionIndex)
							{
								const int32 MaterialIndex = LOD.RenderSections[SectionIndex].MaterialIndex;
								if (!Materials.IsValidIndex(MaterialIndex))
								{
									continue;
								}
								const FString SlotName = Materials[MaterialIndex].MaterialSlotName.ToString();
								const FString MaterialName = Materials[MaterialIndex].MaterialInterface
									? Materials[MaterialIndex].MaterialInterface->GetName() : FString();
								// このアセットは 0=顔、1=口腔、2=埋め込み上歯。Importerがスロット名を
								// 変更する場合もあるため、実マテリアル名と既知のIndex=2でも判定する。
								const bool bEmbeddedTeeth = MaterialIndex == 2
									|| SlotName.Contains(TEXT("UpperTeeth"), ESearchCase::IgnoreCase)
									|| MaterialName.Contains(TEXT("UpperTeeth"), ESearchCase::IgnoreCase);
								if (bEmbeddedTeeth)
								{
									CrimsonGazeMorphComponent->ShowMaterialSection(MaterialIndex, SectionIndex, false, 0);
									UE_LOG(LogTemp, Warning,
										TEXT("RealtimeTestActor: Crimson埋め込み歯を非表示 Material=%d Section=%d Slot=%s Material=%s"),
										MaterialIndex, SectionIndex, *SlotName, *MaterialName);
								}
							}
						}
					}
					// 別体の上歯なら閉口時に確実に非表示にできる。顔へ子付けして自動整列にも追従させる。
					if (UStaticMesh* TeethMesh = LoadObject<UStaticMesh>(nullptr,
						TEXT("/Game/Meshy_Crimson_Gaze_Teeth/SM_Crimson_Gaze_UpperTeeth.SM_Crimson_Gaze_UpperTeeth")))
					{
						CrimsonGazeUpperTeethComponent = NewObject<UStaticMeshComponent>(CharacterActor, TEXT("CrimsonGazeUpperTeethComponent"));
						CrimsonGazeUpperTeethComponent->SetStaticMesh(TeethMesh);
						CrimsonGazeUpperTeethComponent->SetupAttachment(CrimsonGazeMorphComponent);
						CrimsonGazeUpperTeethComponent->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 4.3f)));
						CrimsonGazeUpperTeethComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						CrimsonGazeUpperTeethComponent->RegisterComponent();
						CrimsonGazeUpperTeethComponent->SetVisibility(false, false);
					}
					CrimsonPreviewComponent = CrimsonGazeMorphComponent;
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Crimson jawOpen Morph=%s"),
						CrimsonGazeMorphComponent->FindMorphTarget(TEXT("jawOpen")) ? TEXT("true") : TEXT("false"));
				}
				else if (CrimsonGazeMesh)
				{
					PaytonV9FaceComponent = NewObject<UStaticMeshComponent>(CharacterActor, TEXT("CrimsonGazePreviewComponent"));
					PaytonV9FaceComponent->SetStaticMesh(CrimsonGazeMesh);
					PaytonV9FaceComponent->SetupAttachment(StaticComp->GetAttachParent());
					PaytonV9FaceComponent->SetRelativeTransform(StaticComp->GetRelativeTransform());
					PaytonV9FaceComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					PaytonV9FaceComponent->RegisterComponent();
					PaytonV9FaceComponent->SetVisibility(false, false);
					CrimsonPreviewComponent = PaytonV9FaceComponent;
				}

				if (CrimsonPreviewComponent)
				{
					// 親コンポーネントのスケールに依存しないよう、登録後のWorld Boundsを使って
					// 現行表示メッシュへCrimson Gaze全身を自動整列する。
					OriginalPaytonMorphMesh->UpdateBounds();
					CrimsonPreviewComponent->UpdateBounds();
					FBox V9Box(EForceInit::ForceInit);
					V9Box += CrimsonPreviewComponent->Bounds.GetBox();
					const FBox V8Box = OriginalPaytonMorphMesh->Bounds.GetBox();
					const float V9Height = V9Box.GetSize().Z;
					if (V9Height > KINDA_SMALL_NUMBER)
					{
						float ScaleMultiplier = 1.35f;
						FVector PreviewOffset = FVector::ZeroVector;
						FConfigFile PreviewConfig;
						PreviewConfig.Read(FPaths::ProjectConfigDir() / TEXT("CrimsonGazePreview.ini"));
						PreviewConfig.GetFloat(TEXT("CrimsonGazePreview"), TEXT("ScaleMultiplier"), ScaleMultiplier);
						ReadSceneVector(PreviewConfig, TEXT("CrimsonGazePreview"), TEXT("Offset"), PreviewOffset);
						const float AutoScale = (V8Box.GetSize().Z / V9Height) * ScaleMultiplier;
						CrimsonPreviewComponent->SetWorldScale3D(CrimsonPreviewComponent->GetComponentScale() * AutoScale);
						CrimsonPreviewComponent->UpdateBounds();
						FBox ScaledV9Box(EForceInit::ForceInit);
						ScaledV9Box += CrimsonPreviewComponent->Bounds.GetBox();
						const FVector CenterOffset = V8Box.GetCenter() - ScaledV9Box.GetCenter() + PreviewOffset;
						CrimsonPreviewComponent->AddWorldOffset(CenterOffset);
						UE_LOG(LogTemp, Warning,
							TEXT("RealtimeTestActor: Crimson Gaze自動整列 Scale=%f Offset=%s CurrentSize=%s CrimsonSize=%s"),
							AutoScale, *CenterOffset.ToString(), *V8Box.GetSize().ToString(), *ScaledV9Box.GetSize().ToString());
					}
					UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Crimson Gaze比較表示を準備しました(F8)"));
					bShowingPaytonV9 = true;
					CrimsonPreviewComponent->SetVisibility(true, false);
					OriginalPaytonMorphMesh->SetVisibility(false, false);
					if (OriginalPaytonUpperTeethComponent) OriginalPaytonUpperTeethComponent->SetVisibility(false, false);
					// 表情制御は実際に表示しているDirect Morph顔へ向ける。
					// 現行CrimsonにはjawOpen以外の表情Morphが無いため、登録時ログで明示する。
					if (LipSync)
					{
						LipSync->SetExpressionFaceMesh(CrimsonGazeMorphComponent);
					}
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("RealtimeTestActor: Crimson Gazeメッシュのロードに失敗しました"));
				}

				// Meshy V1には口内と歯列を同梱済み。旧独立口内は二重表示になるため使わない。
				UStaticMesh* MouthMesh = nullptr;
				if (MouthMesh)
				{
					OriginalPaytonMouthComponent = NewObject<UStaticMeshComponent>(CharacterActor, TEXT("OriginalPaytonMouthComponent"));
					OriginalPaytonMouthComponent->SetStaticMesh(MouthMesh);
					if (UMaterialInterface* MouthMaterial = LoadObject<UMaterialInterface>(nullptr,
						TEXT("/Game/Payton_Original_Rigged/M_Payton_MouthCavity.M_Payton_MouthCavity")))
					{
						OriginalPaytonMouthComponent->SetMaterial(0, MouthMaterial);
					}
					OriginalPaytonMouthComponent->SetupAttachment(OriginalPaytonPoseableMesh);
					OriginalPaytonMouthComponent->SetRelativeLocation(FVector(0.0f, -42.0f, 32.5f));
					OriginalPaytonMouthComponent->SetRelativeRotation(FRotator::ZeroRotator);
					OriginalPaytonMouthComponent->SetRelativeScale3D(FVector(1.0f));
					OriginalPaytonMouthComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					OriginalPaytonMouthComponent->RegisterComponent();
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 独立口内StaticMeshを表示しました AssetExtent=%s ComponentExtent=%s Scale=%s"),
						*MouthMesh->GetBounds().BoxExtent.ToString(),
						*OriginalPaytonMouthComponent->Bounds.BoxExtent.ToString(),
						*OriginalPaytonMouthComponent->GetRelativeScale3D().ToString());
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 口内配置 Relative=%s World=%s BoundsOrigin=%s / 顔BoundsOrigin=%s"),
						*OriginalPaytonMouthComponent->GetRelativeLocation().ToString(),
						*OriginalPaytonMouthComponent->GetComponentLocation().ToString(),
						*OriginalPaytonMouthComponent->Bounds.Origin.ToString(),
						*OriginalPaytonPoseableMesh->Bounds.Origin.ToString());
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Meshy同梱口内を使用します"));
				}
				const int32 JawBoneIndex = OriginalPaytonPoseableMesh->GetBoneIndex(TEXT("jaw"));
				OriginalPaytonJawReferenceTransform = OriginalPaytonPoseableMesh->GetBoneTransformByName(
					TEXT("jaw"), EBoneSpaces::ComponentSpace);
				bHasOriginalPaytonJawReference = JawBoneIndex != INDEX_NONE;
				if (const FSkeletalMeshRenderData* RenderData = OriginalRiggedMesh->GetResourceForRendering())
				{
					if (RenderData->LODRenderData.Num() > 0)
					{
						const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
						const FSkinWeightVertexBuffer* WeightBuffer = LODData.GetSkinWeightVertexBuffer();
						int32 JawInfluencedVertices = 0;
						uint16 MaxJawWeight = 0;
						FBox JawVertexBounds(EForceInit::ForceInit);
						for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
						{
							int32 SectionJawVertices = 0;
							int32 FrontJawVertices = 0;
							for (uint32 LocalVertex = 0; LocalVertex < Section.NumVertices; ++LocalVertex)
							{
								const uint32 VertexIndex = Section.BaseVertexIndex + LocalVertex;
								const FSkinWeightInfo WeightInfo = WeightBuffer->GetVertexSkinWeights(VertexIndex);
								bool bJawInfluenced = false;
								for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
								{
									const int32 LocalBone = WeightInfo.InfluenceBones[Influence];
									if (WeightInfo.InfluenceWeights[Influence] > 0 && Section.BoneMap.IsValidIndex(LocalBone)
										&& Section.BoneMap[LocalBone] == JawBoneIndex)
									{
										bJawInfluenced = true;
										MaxJawWeight = FMath::Max(MaxJawWeight, WeightInfo.InfluenceWeights[Influence]);
									}
								}
								JawInfluencedVertices += bJawInfluenced ? 1 : 0;
								if (bJawInfluenced)
								{
									const FVector Position(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
									JawVertexBounds += Position;
									++SectionJawVertices;
									FrontJawVertices += Position.Y >= 37.0f ? 1 : 0;
								}
							}
							if (SectionJawVertices > 0)
							{
								UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2 jaw Section Material=%u Vertices=%u Jaw=%d FrontY37=%d Disabled=%s"),
									Section.MaterialIndex, Section.NumVertices, SectionJawVertices, FrontJawVertices,
									Section.bDisabled ? TEXT("true") : TEXT("false"));
							}
						}
						UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2 GPU SkinWeight jaw影響頂点=%d 最大Weight=%u LOD0頂点=%u 座標Min=%s Max=%s"),
							JawInfluencedVertices, MaxJawWeight, WeightBuffer->GetNumVertices(),
							*JawVertexBounds.Min.ToString(), *JawVertexBounds.Max.ToString());
					}
				}
				const int32 MouthCavityBoneIndex = OriginalPaytonPoseableMesh->GetBoneIndex(TEXT("mouth_cavity"));
				OriginalPaytonMouthCavityReferenceTransform = OriginalPaytonPoseableMesh->GetBoneTransformByName(
					TEXT("mouth_cavity"), EBoneSpaces::ComponentSpace);
				bHasOriginalPaytonMouthCavityReference = MouthCavityBoneIndex != INDEX_NONE;
				if (OriginalPaytonMouthComponent && bHasOriginalPaytonMouthCavityReference)
				{
					FVector MouthLocation = OriginalPaytonMouthCavityReferenceTransform.GetTranslation();
					// Blenderの正面(-Y)はUEの+Y。実測で表示できた位置まで8cm押し出す。
					MouthLocation.Y += 8.0f;
					OriginalPaytonMouthComponent->SetRelativeLocation(MouthLocation);
					OriginalPaytonMouthComponent->UpdateBounds();
				}
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 元FBX顎骨を初期化 BoneIndex=%d Reference=%s"),
					JawBoneIndex, *OriginalPaytonJawReferenceTransform.ToHumanReadableString());
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 口内ボーン初期化 BoneIndex=%d Exists=%s Reference=%s"),
					MouthCavityBoneIndex,
					bHasOriginalPaytonMouthCavityReference ? TEXT("true") : TEXT("false"),
					*OriginalPaytonMouthCavityReferenceTransform.ToHumanReadableString());
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 口内ボーンComponent位置=%s"),
					*OriginalPaytonMouthCavityReferenceTransform.GetTranslation().ToString());
				StaticComp->SetVisibility(false, false);
				FaceComp->SetVisibility(false, false);
				UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 元FBX骨付きメッシュを表示しました: %s"), *OriginalRiggedMesh->GetPathName());
			}
			else
			{
				StaticComp->SetStaticMesh(HairMesh);
				StaticComp->SetVisibility(true, false);
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 元FBX骨付きメッシュをロードできないため髪のみを表示します"));
			}
		}
		else if (bUseAnimatedPaytonFace)
		{
			// 現在のStatic Meshは顔と髪が一体で、表示すると動くFaceを覆うため隠す。
			StaticComp->SetVisibility(false, false);
		}
		break;
	}

	// 【仮説検証】Face_AnimBPのAnimGraphが「Use ARKit Face」の状態によって
	// GetCurveValue(JawOpen)とJawOpenAlpha変数のどちらを使うか分岐している可能性を検証するため、
	// CharacterActor(BP_Payton)自身のUse ARKit Face変数を直接falseにしてみる
	if (CharacterActor)
	{
		bool bToggled = false;
		for (TFieldIterator<FBoolProperty> PropIt(CharacterActor->GetClass()); PropIt; ++PropIt)
		{
			const FString PropName = PropIt->GetName();
			if (PropName.Contains(TEXT("ARKit"), ESearchCase::IgnoreCase) && PropName.Contains(TEXT("Face"), ESearchCase::IgnoreCase))
			{
				PropIt->SetPropertyValue_InContainer(CharacterActor, false);
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CharacterActorの%sをfalseにしました"), *PropName);
				bToggled = true;
			}
		}
		if (!bToggled)
		{
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Use ARKit Face相当のプロパティが見つかりませんでした"));
		}
	}

	// LLink_Face_Subj(型:FLiveLinkSubjectName)をLifeSimARKitFaceに設定しておく
	// (Face_AnimBP自体のARKit取り込み経路がまだ生きている可能性があるための保険)
	if (FStructProperty* SubjProp = FindFProperty<FStructProperty>(FaceAnim->GetClass(), TEXT("LLink_Face_Subj")))
	{
		if (void* ValuePtr = SubjProp->ContainerPtrToValuePtr<void>(FaceAnim))
		{
			static_cast<FLiveLinkSubjectName*>(ValuePtr)->Name = TEXT("LifeSimARKitFace");
		}
	}

	// JawOpenAlpha変数への直接書き込みに使うプロパティをキャッシュしておく
	CachedJawOpenAlphaProperty = FindFProperty<FDoubleProperty>(FaceAnim->GetClass(), TEXT("JawOpenAlpha"));
	if (!CachedJawOpenAlphaProperty)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: JawOpenAlphaプロパティが見つかりませんでした"));
	}
}

void ARealtimeTestActor::CallFaceSetControl(FName ControlName, float Value)
{
	if (!CachedFaceAnimInstance || !CachedSetControlFunction)
	{
		return;
	}

	// UFunctionのパラメータレイアウト(パディング等)を仮定せず、FindFPropertyで
	// 安全にオフセットを取得してから値を書き込む
	TArray<uint8> ParamsBuffer;
	ParamsBuffer.AddZeroed(CachedSetControlFunction->ParmsSize);

	if (FNameProperty* NameProp = FindFProperty<FNameProperty>(CachedSetControlFunction, TEXT("ControlName")))
	{
		NameProp->SetPropertyValue_InContainer(ParamsBuffer.GetData(), ControlName);
	}
	// UE5.8のBlueprint浮動小数点はdouble精度のため、Value引数はFFloatPropertyではなく
	// FDoublePropertyになっている(これが原因で以前は常に0のまま書き込みに失敗していた)
	if (FDoubleProperty* ValueProp = FindFProperty<FDoubleProperty>(CachedSetControlFunction, TEXT("Value")))
	{
		ValueProp->SetPropertyValue_InContainer(ParamsBuffer.GetData(), (double)Value);
	}

	CachedFaceAnimInstance->ProcessEvent(CachedSetControlFunction, ParamsBuffer.GetData());
}

void ARealtimeTestActor::AttachCharacterToVehicle()
{
	if (!CharacterActor || !VehiclePawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CharacterActorまたは車のポーンが未設定のため、助手席にアタッチできません"));
		return;
	}

	// Paytonのコリジョンが車体・タイヤと干渉して物理演算(走行)を止めてしまうため、
	// アタッチする前に(1フレームも重ならないうちに)コリジョンを無効化しておく
	CharacterActor->SetActorEnableCollision(false);
	RestoreJenniferCanonicalScale(TEXT("AttachCharacterToVehicle"));

	// bWeldSimulatedBodies=falseを明示し、Paytonが車の物理演算に質量として
	// 溶接されてしまわないようにする(質量が加わると車が正しく動けなくなる)
	const FAttachmentTransformRules AttachRules(
		EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, /*bWeldSimulatedBodies=*/ false);
	CharacterActor->AttachToActor(VehiclePawn, AttachRules);
	CharacterActor->SetActorRelativeLocation(VehiclePassengerSeatOffset);
	CharacterActor->SetActorRelativeRotation(VehiclePassengerSeatRotationOffset);

	EnsurePaytonFillLight();
}

void ARealtimeTestActor::EnsurePaytonFillLight()
{
	if (!CharacterActor || !CharacterActor->GetRootComponent())
	{
		return;
	}

	if (PaytonFillLights.Num() == 0)
	{
		// 前・後・左・右・真上、それぞれにライトを1つずつ配置する(Root=足元基準の相対位置)
		const TArray<FVector> LightOffsets = {
			FVector(60.0f, 0.0f, 150.0f),   // 前
			FVector(-60.0f, 0.0f, 150.0f),  // 後ろ
			FVector(0.0f, -60.0f, 150.0f),  // 左
			FVector(0.0f, 60.0f, 150.0f),   // 右
			FVector(0.0f, 0.0f, 230.0f),    // 真上
		};

		for (int32 i = 0; i < LightOffsets.Num(); ++i)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(
				CharacterActor, *FString::Printf(TEXT("PaytonFillLight%d"), i));
			Light->RegisterComponent();
			Light->SetMobility(EComponentMobility::Movable);
			Light->CastShadows = false;
			Light->Intensity = 3000.0f;
			Light->AttenuationRadius = 300.0f;
			Light->SetLightColor(FLinearColor(1.0f, 0.95f, 0.9f));
			Light->AttachToComponent(CharacterActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			Light->SetRelativeLocation(LightOffsets[i]);

			PaytonFillLights.Add(Light);
		}
	}
}

void ARealtimeTestActor::EnsureJenniferConversationLighting()
{
	if (!CharacterActor || !CharacterActor->GetRootComponent())
	{
		return;
	}

	// Jenniferに属する描画コンポーネントだけを専用Channel 1へ移す。
	// Scene背景は従来どおりChannel 0なので、背景の照明や雰囲気は変化しない。
	TInlineComponentArray<UPrimitiveComponent*> JenniferPrimitives(CharacterActor);
	for (UPrimitiveComponent* Primitive : JenniferPrimitives)
	{
		if (Primitive)
		{
			Primitive->SetLightingChannels(false, true, false);
		}
	}

	if (JenniferConversationLights.Num() == 0)
	{
		struct FJenniferLightSpec
		{
			FVector Offset;
			float Intensity;
			FLinearColor Color;
			bool bCastShadows;
		};

		// Actorローカルの+Xを正面として、柔らかいKey/Fill/Topの3灯を固定する。
		// Sceneライトより十分弱くし、陰影を残しながら極端な片影と白飛びを防ぐ。
		const FJenniferLightSpec Specs[] =
		{
			{ FVector(95.0f, -55.0f, 175.0f), 700.0f, FLinearColor(1.0f, 0.93f, 0.86f), true },
			{ FVector(75.0f,  70.0f, 165.0f), 260.0f, FLinearColor(0.88f, 0.94f, 1.0f), false },
			{ FVector(-15.0f, 0.0f, 225.0f), 140.0f, FLinearColor(1.0f, 0.96f, 0.90f), false }
		};

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Specs); ++Index)
		{
			const FJenniferLightSpec& Spec = Specs[Index];
			USpotLightComponent* Light = NewObject<USpotLightComponent>(
				CharacterActor, *FString::Printf(TEXT("JenniferConversationLight%d"), Index));
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(Spec.Intensity);
			Light->SetAttenuationRadius(350.0f);
			Light->SetSourceRadius(22.0f);
			Light->SetSoftSourceRadius(35.0f);
			Light->SetInnerConeAngle(35.0f);
			Light->SetOuterConeAngle(60.0f);
			Light->SetLightColor(Spec.Color);
			Light->CastShadows = Spec.bCastShadows;
			Light->SetLightingChannels(false, true, false);
			Light->SetupAttachment(CharacterActor->GetRootComponent());
			Light->RegisterComponent();
			Light->SetRelativeLocation(Spec.Offset);
			JenniferConversationLights.Add(Light);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[LIGHTING][JENNIFER] isolated_channel=1 primitives=%d dedicated_lights=%d"),
		JenniferPrimitives.Num(), JenniferConversationLights.Num());
	UpdateJenniferConversationLightingTransform();
}

void ARealtimeTestActor::UpdateJenniferConversationLightingTransform()
{
	if (!IntroFaceCamera || JenniferConversationLights.Num() < 3)
	{
		return;
	}

	const FVector FaceTarget = ResolveJenniferFaceTarget();
	FVector TowardCamera = IntroFaceCamera->GetActorLocation() - FaceTarget;
	if (!TowardCamera.Normalize())
	{
		return;
	}
	FVector CameraRight = FVector::CrossProduct(FVector::UpVector, TowardCamera);
	if (!CameraRight.Normalize())
	{
		CameraRight = FVector::RightVector;
	}

	// Actorのローカル軸ではなく、実際の会話カメラを基準に配置する。
	// これによりSceneごとのJennifer回転に関係なく、Key/Fillが常に顔の正面へ来る。
	const FVector WorldLocations[] =
	{
		FaceTarget + TowardCamera * 75.0f - CameraRight * 45.0f + FVector::UpVector * 20.0f,
		FaceTarget + TowardCamera * 70.0f + CameraRight * 50.0f + FVector::UpVector * 10.0f,
		FaceTarget - TowardCamera * 55.0f + CameraRight * 20.0f + FVector::UpVector * 85.0f
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WorldLocations); ++Index)
	{
		if (JenniferConversationLights[Index])
		{
			JenniferConversationLights[Index]->SetWorldLocation(WorldLocations[Index]);
			JenniferConversationLights[Index]->SetWorldRotation(
				(FaceTarget - WorldLocations[Index]).Rotation());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[LIGHTING][JENNIFER] camera_relative face=%s key=%s fill=%s top=%s"),
		*FaceTarget.ToString(), *WorldLocations[0].ToString(), *WorldLocations[1].ToString(),
		*WorldLocations[2].ToString());
}

void ARealtimeTestActor::EnsureCockpitFillLights()
{
	if (!VehiclePawn || !VehiclePawn->GetRootComponent())
	{
		return;
	}

	if (CockpitFillLights.Num() == 0)
	{
		// コックピットカメラの基準位置(FrontSpringArm)を中心に、前後左右にライトを配置する
		const FVector Center = DesiredFrontCameraOffset;
		const TArray<FVector> LightOffsets = {
			Center + FVector(50.0f, 0.0f, 20.0f),
			Center + FVector(-50.0f, 0.0f, 20.0f),
			Center + FVector(0.0f, -60.0f, 20.0f),
			Center + FVector(0.0f, 60.0f, 20.0f),
		};

		for (int32 i = 0; i < LightOffsets.Num(); ++i)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(
				VehiclePawn, *FString::Printf(TEXT("CockpitFillLight%d"), i));
			Light->RegisterComponent();
			Light->SetMobility(EComponentMobility::Movable);
			Light->CastShadows = false;
			Light->Intensity = 4000.0f;
			Light->AttenuationRadius = 250.0f;
			Light->SetLightColor(FLinearColor(1.0f, 0.97f, 0.92f));
			Light->AttachToComponent(VehiclePawn->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			Light->SetRelativeLocation(LightOffsets[i]);

			CockpitFillLights.Add(Light);
		}
	}
}

void ARealtimeTestActor::SetPaytonFillLightsEnabled(bool bEnabled)
{
	for (UPointLightComponent* Light : PaytonFillLights)
	{
		if (Light)
		{
			Light->SetVisibility(bEnabled);
		}
	}
}

void ARealtimeTestActor::HandleToggleScenePointLightDiagnostic()
{
	bDiagnosticScenePointLightsEnabled = !bDiagnosticScenePointLightsEnabled;
	TArray<AActor*> SceneLights;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TEXT("LifeSimJenniferScenePointLight"), SceneLights);
	for (AActor* Actor : SceneLights)
	{
		if (APointLight* PointLight = Cast<APointLight>(Actor))
		{
			PointLight->SetActorHiddenInGame(!bDiagnosticScenePointLightsEnabled);
			if (PointLight->PointLightComponent)
			{
				PointLight->PointLightComponent->SetVisibility(bDiagnosticScenePointLightsEnabled);
			}
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[LIGHTING][AB] scene=%s generated_scene_point_light=%s count=%d"),
		*GetConversationLocationTagStem(CurrentConversationLocation),
		bDiagnosticScenePointLightsEnabled ? TEXT("ON") : TEXT("OFF"), SceneLights.Num());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9140, 5.0f,
			bDiagnosticScenePointLightsEnabled ? FColor::Yellow : FColor::Cyan,
			FString::Printf(TEXT("[LIGHTING A/B] Scene Point Light: %s"),
				bDiagnosticScenePointLightsEnabled ? TEXT("ON") : TEXT("OFF")),
			true, FVector2D(1.75f, 1.75f));
	}
	LogLightingEnvironmentAtJennifer();
}

void ARealtimeTestActor::LogLightingEnvironmentAtJennifer() const
{
	if (!GetWorld() || !CharacterActor)
	{
		return;
	}
	const FVector JenniferLocation = CharacterActor->GetActorLocation();
	TArray<AActor*> Lights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALight::StaticClass(), Lights);
	for (AActor* Actor : Lights)
	{
		const ALight* Light = Cast<ALight>(Actor);
		const ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr;
		if (!Component)
		{
			continue;
		}
		const float Distance = FVector::Distance(JenniferLocation, Actor->GetActorLocation());
		const bool bInfiniteLight = Actor->GetClass()->GetName().Contains(TEXT("Directional"));
		if (!bInfiniteLight && Distance > 5000.0f)
		{
			continue;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[LIGHTING][ENV] scene=%s type=%s name=%s distance=%.1f intensity=%.2f visible=%s color=%s temperature=%.1f use_temperature=%s"),
			*GetConversationLocationTagStem(CurrentConversationLocation), *Actor->GetClass()->GetName(),
			*Actor->GetName(), Distance, Component->Intensity,
			Component->IsVisible() ? TEXT("true") : TEXT("false"),
			*Component->GetLightColor().ToString(), Component->Temperature,
			Component->bUseTemperature ? TEXT("true") : TEXT("false"));
	}

	TArray<AActor*> SkyLights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASkyLight::StaticClass(), SkyLights);
	for (AActor* Actor : SkyLights)
	{
		const ASkyLight* SkyLight = Cast<ASkyLight>(Actor);
		const USkyLightComponent* Component = SkyLight ? SkyLight->GetLightComponent() : nullptr;
		UE_LOG(LogTemp, Warning,
			TEXT("[LIGHTING][ENV] scene=%s type=SkyLight name=%s intensity=%.2f visible=%s mobility=%d"),
			*GetConversationLocationTagStem(CurrentConversationLocation), *Actor->GetName(),
			Component ? Component->Intensity : -1.0f,
			Component && Component->IsVisible() ? TEXT("true") : TEXT("false"),
			Component ? static_cast<int32>(Component->Mobility) : -1);
	}

	TArray<AActor*> ReflectionCaptures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AReflectionCapture::StaticClass(), ReflectionCaptures);
	for (AActor* Actor : ReflectionCaptures)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LIGHTING][ENV] scene=%s type=ReflectionCapture name=%s distance=%.1f"),
			*GetConversationLocationTagStem(CurrentConversationLocation), *Actor->GetName(),
			FVector::Distance(JenniferLocation, Actor->GetActorLocation()));
	}
}

void ARealtimeTestActor::HandleCycleMyRoomLightDiagnostic()
{
	if (!GetWorld() || !CharacterActor)
	{
		return;
	}

	if (CurrentConversationLocation != EConversationLocation::MyRoom)
	{
		ResetMyRoomLightDiagnostic();
		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][MYROOM_AB] unavailable: move to MyRoom first"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9141, 5.0f, FColor::Orange,
				TEXT("[LIGHTING A/B] Move to MyRoom, then press 0"), true, FVector2D(1.5f, 1.5f));
		}
		return;
	}

	// 前回OFFにした1灯は必ず元の可視状態へ戻してから、次の候補へ進む。
	if (DiagnosticMyRoomDisabledLight.IsValid())
	{
		DiagnosticMyRoomDisabledLight->SetVisibility(bDiagnosticMyRoomPreviousVisibility);
		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][MYROOM_AB] restored=%s visible=%s"),
			*DiagnosticMyRoomDisabledLight->GetOwner()->GetName(),
			bDiagnosticMyRoomPreviousVisibility ? TEXT("true") : TEXT("false"));
		DiagnosticMyRoomDisabledLight.Reset();
	}

	if (DiagnosticMyRoomLights.Num() == 0)
	{
		constexpr float DiagnosticRadiusCm = 3000.0f;
		const FVector JenniferLocation = CharacterActor->GetActorLocation();
		TArray<AActor*> Lights;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALight::StaticClass(), Lights);
		for (AActor* Actor : Lights)
		{
			ALight* LightActor = Cast<ALight>(Actor);
			ULightComponent* LightComponent = LightActor ? LightActor->GetLightComponent() : nullptr;
			UPointLightComponent* PointComponent = Cast<UPointLightComponent>(LightComponent);
			if (!PointComponent)
			{
				continue;
			}
			// 現在のMyRoom描画へ実際に寄与している灯体だけをA/B対象にする。
			// 非表示ライトを順番に含めると、OFFにしても変化しない診断が混ざる。
			if (!LightComponent->IsVisible() || Actor->IsHidden())
			{
				continue;
			}

			const float Distance = FVector::Distance(JenniferLocation, Actor->GetActorLocation());
			if (Distance > DiagnosticRadiusCm)
			{
				continue;
			}
			DiagnosticMyRoomLights.Add(LightComponent);
		}

		DiagnosticMyRoomLights.Sort([JenniferLocation](const TWeakObjectPtr<ULightComponent>& A,
			const TWeakObjectPtr<ULightComponent>& B)
		{
			const AActor* OwnerA = A.IsValid() ? A->GetOwner() : nullptr;
			const AActor* OwnerB = B.IsValid() ? B->GetOwner() : nullptr;
			const float DistanceA = OwnerA ? FVector::Distance(JenniferLocation, OwnerA->GetActorLocation()) : MAX_flt;
			const float DistanceB = OwnerB ? FVector::Distance(JenniferLocation, OwnerB->GetActorLocation()) : MAX_flt;
			return DistanceA < DistanceB;
		});

		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][MYROOM_AB] candidates=%d radius=%.0fcm Jennifer=%s"),
			DiagnosticMyRoomLights.Num(), DiagnosticRadiusCm, *JenniferLocation.ToString());
		for (int32 Index = 0; Index < DiagnosticMyRoomLights.Num(); ++Index)
		{
			ULightComponent* Component = DiagnosticMyRoomLights[Index].Get();
			UPointLightComponent* Point = Cast<UPointLightComponent>(Component);
			if (!Component || !Point || !Component->GetOwner())
			{
				continue;
			}
			const USpotLightComponent* Spot = Cast<USpotLightComponent>(Component);
			const FLinearColor RawColor = Component->LightColor;
			UE_LOG(LogTemp, Warning,
				TEXT("[LIGHTING][MYROOM_LIGHT] index=%d name=%s type=%s distance=%.1f intensity=%.2f color=(%.3f,%.3f,%.3f) use_temperature=%s temperature=%.1f cast_shadow=%s attenuation=%.1f inner_cone=%.1f outer_cone=%.1f mobility=%d visible=%s"),
				Index + 1, *Component->GetOwner()->GetName(), Spot ? TEXT("Spot") : TEXT("Point"),
				FVector::Distance(JenniferLocation, Component->GetOwner()->GetActorLocation()), Component->Intensity,
				RawColor.R, RawColor.G, RawColor.B,
				Component->bUseTemperature ? TEXT("true") : TEXT("false"), Component->Temperature,
				Component->CastShadows ? TEXT("true") : TEXT("false"), Point->AttenuationRadius,
				Spot ? Spot->InnerConeAngle : -1.0f, Spot ? Spot->OuterConeAngle : -1.0f,
				static_cast<int32>(Component->Mobility), Component->IsVisible() ? TEXT("true") : TEXT("false"));
		}
	}

	DiagnosticMyRoomLights.RemoveAll([](const TWeakObjectPtr<ULightComponent>& Light) { return !Light.IsValid(); });
	if (DiagnosticMyRoomLights.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][MYROOM_AB] no nearby Point/Spot lights found"));
		return;
	}

	DiagnosticMyRoomLightIndex %= DiagnosticMyRoomLights.Num();
	ULightComponent* Selected = DiagnosticMyRoomLights[DiagnosticMyRoomLightIndex].Get();
	if (!Selected || !Selected->GetOwner())
	{
		return;
	}
	bDiagnosticMyRoomPreviousVisibility = Selected->IsVisible();
	DiagnosticMyRoomDisabledLight = Selected;
	Selected->SetVisibility(false);
	const FString Message = FString::Printf(TEXT("[LIGHTING A/B] 0 key OFF %d/%d: %s"),
		DiagnosticMyRoomLightIndex + 1, DiagnosticMyRoomLights.Num(), *Selected->GetOwner()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][MYROOM_AB] off_index=%d/%d name=%s previous_visible=%s"),
		DiagnosticMyRoomLightIndex + 1, DiagnosticMyRoomLights.Num(), *Selected->GetOwner()->GetName(),
		bDiagnosticMyRoomPreviousVisibility ? TEXT("true") : TEXT("false"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9141, 8.0f, FColor::Yellow, Message, true, FVector2D(1.5f, 1.5f));
	}
	DiagnosticMyRoomLightIndex = (DiagnosticMyRoomLightIndex + 1) % DiagnosticMyRoomLights.Num();
}

void ARealtimeTestActor::ResetMyRoomLightDiagnostic()
{
	if (DiagnosticMyRoomDisabledLight.IsValid())
	{
		DiagnosticMyRoomDisabledLight->SetVisibility(bDiagnosticMyRoomPreviousVisibility);
	}
	DiagnosticMyRoomDisabledLight.Reset();
	DiagnosticMyRoomLights.Reset();
	DiagnosticMyRoomLightIndex = 0;
	bDiagnosticMyRoomPreviousVisibility = true;
}

void ARealtimeTestActor::HandleCycleDirectionalLightDiagnostic()
{
	if (!GetWorld())
	{
		return;
	}
	if (DiagnosticDisabledDirectionalLight.IsValid())
	{
		DiagnosticDisabledDirectionalLight->SetVisibility(bDiagnosticDirectionalPreviousVisibility);
		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][DIRECTIONAL_AB] restored=%s"),
			*DiagnosticDisabledDirectionalLight->GetOwner()->GetName());
		DiagnosticDisabledDirectionalLight.Reset();
	}
	if (DiagnosticDirectionalLights.Num() == 0)
	{
		TArray<AActor*> Lights;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALight::StaticClass(), Lights);
		for (AActor* Actor : Lights)
		{
			ALight* Light = Cast<ALight>(Actor);
			ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr;
			if (Component && Actor->GetClass()->GetName().Contains(TEXT("Directional"))
				&& Component->IsVisible() && !Actor->IsHidden())
			{
				DiagnosticDirectionalLights.Add(Component);
				UE_LOG(LogTemp, Warning,
					TEXT("[LIGHTING][DIRECTIONAL] name=%s intensity=%.2f color=%s temperature=%.1f use_temperature=%s rotation=%s cast_shadow=%s mobility=%d"),
					*Actor->GetName(), Component->Intensity, *Component->GetLightColor().ToString(),
					Component->Temperature, Component->bUseTemperature ? TEXT("true") : TEXT("false"),
					*Actor->GetActorRotation().ToString(), Component->CastShadows ? TEXT("true") : TEXT("false"),
					static_cast<int32>(Component->Mobility));
			}
		}
		DiagnosticDirectionalLights.Sort([](const TWeakObjectPtr<ULightComponent>& A,
			const TWeakObjectPtr<ULightComponent>& B)
		{
			return A.IsValid() && B.IsValid() && A->Intensity > B->Intensity;
		});
	}
	DiagnosticDirectionalLights.RemoveAll([](const TWeakObjectPtr<ULightComponent>& Light) { return !Light.IsValid(); });
	if (DiagnosticDirectionalLights.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][DIRECTIONAL_AB] no active Directional Lights found"));
		return;
	}
	DiagnosticDirectionalLightIndex %= DiagnosticDirectionalLights.Num();
	ULightComponent* Selected = DiagnosticDirectionalLights[DiagnosticDirectionalLightIndex].Get();
	if (!Selected || !Selected->GetOwner())
	{
		return;
	}
	bDiagnosticDirectionalPreviousVisibility = Selected->IsVisible();
	DiagnosticDisabledDirectionalLight = Selected;
	Selected->SetVisibility(false);
	const FString Message = FString::Printf(TEXT("[LIGHTING A/B] 0 key Directional OFF %d/%d: %s"),
		DiagnosticDirectionalLightIndex + 1, DiagnosticDirectionalLights.Num(), *Selected->GetOwner()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[LIGHTING][DIRECTIONAL_AB] scene=%s off_index=%d/%d name=%s"),
		*GetConversationLocationTagStem(CurrentConversationLocation), DiagnosticDirectionalLightIndex + 1,
		DiagnosticDirectionalLights.Num(), *Selected->GetOwner()->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9141, 8.0f, FColor::Yellow, Message, true, FVector2D(1.5f, 1.5f));
	}
	DiagnosticDirectionalLightIndex = (DiagnosticDirectionalLightIndex + 1) % DiagnosticDirectionalLights.Num();
}

void ARealtimeTestActor::ResetDirectionalLightDiagnostic()
{
	if (DiagnosticDisabledDirectionalLight.IsValid())
	{
		DiagnosticDisabledDirectionalLight->SetVisibility(bDiagnosticDirectionalPreviousVisibility);
	}
	DiagnosticDisabledDirectionalLight.Reset();
	DiagnosticDirectionalLights.Reset();
	DiagnosticDirectionalLightIndex = 0;
	bDiagnosticDirectionalPreviousVisibility = true;
}

void ARealtimeTestActor::HandleToggleNeutralBackgroundDiagnostic()
{
	if (!GetWorld() || !IntroFaceCamera)
	{
		return;
	}

	if (!DiagnosticNeutralBackgroundActor)
	{
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* NeutralMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineDebugMaterials/LevelColorationUnlitMaterial.LevelColorationUnlitMaterial"));
		if (!CubeMesh || !NeutralMaterial)
		{
			UE_LOG(LogTemp, Error, TEXT("[BACKGROUND_AB] failed to load diagnostic cube/material"));
			return;
		}

		DiagnosticNeutralBackgroundActor = GetWorld()->SpawnActor<AStaticMeshActor>();
		if (!DiagnosticNeutralBackgroundActor || !DiagnosticNeutralBackgroundActor->GetStaticMeshComponent())
		{
			UE_LOG(LogTemp, Error, TEXT("[BACKGROUND_AB] failed to create diagnostic background"));
			return;
		}
		UStaticMeshComponent* Panel = DiagnosticNeutralBackgroundActor->GetStaticMeshComponent();
		Panel->SetMobility(EComponentMobility::Movable);
		Panel->SetStaticMesh(CubeMesh);
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Panel->SetCastShadow(false);
		Panel->bReceivesDecals = false;
		Panel->bRenderCustomDepth = false;
		Panel->SetLightingChannels(false, false, false);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(NeutralMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.18f, 0.18f, 0.18f, 1.0f));
			Panel->SetMaterial(0, MID);
		}
		DiagnosticNeutralBackgroundActor->AttachToActor(IntroFaceCamera,
			FAttachmentTransformRules::KeepRelativeTransform);
		// CameraローカルXが前方。Jennifer(約108cm先)より後ろの250cmへ、
		// 薄く大きいCubeを置いて画面上の背景だけを覆う。
		DiagnosticNeutralBackgroundActor->SetActorRelativeLocation(FVector(250.0f, 0.0f, 0.0f));
		DiagnosticNeutralBackgroundActor->SetActorRelativeRotation(FRotator::ZeroRotator);
		DiagnosticNeutralBackgroundActor->SetActorRelativeScale3D(FVector(0.05f, 20.0f, 20.0f));
		DiagnosticNeutralBackgroundActor->SetActorHiddenInGame(true);
	}

	bDiagnosticNeutralBackgroundEnabled = !bDiagnosticNeutralBackgroundEnabled;
	DiagnosticNeutralBackgroundActor->SetActorHiddenInGame(!bDiagnosticNeutralBackgroundEnabled);
	UE_LOG(LogTemp, Warning, TEXT("[BACKGROUND_AB] scene=%s neutral_background=%s color=0.18 camera_relative_location=(250,0,0)"),
		*GetConversationLocationTagStem(CurrentConversationLocation),
		bDiagnosticNeutralBackgroundEnabled ? TEXT("ON") : TEXT("OFF"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9141, 8.0f,
			bDiagnosticNeutralBackgroundEnabled ? FColor::Silver : FColor::Cyan,
			FString::Printf(TEXT("[BACKGROUND A/B] Neutral gray: %s"),
				bDiagnosticNeutralBackgroundEnabled ? TEXT("ON") : TEXT("OFF")),
			true, FVector2D(1.5f, 1.5f));
	}
}

void ARealtimeTestActor::HandleCycleCrimsonBufferDiagnostic()
{
	static const TCHAR* Modes[] =
	{
		TEXT("AllDirectionalLights_OFF"),
		TEXT("AllLocalLights_OFF"),
		TEXT("AllLightComponents_OFF"),
		TEXT("AllDirectLighting_OFF"),
		TEXT("FullLit")
	};
	constexpr int32 ModeCount = UE_ARRAY_COUNT(Modes);
	DiagnosticCrimsonBufferModeIndex = (DiagnosticCrimsonBufferModeIndex + 1) % ModeCount;
	const FString Mode = Modes[DiagnosticCrimsonBufferModeIndex];
	ResetDirectLightGroupDiagnostic();
	// 毎回Full Litへ戻し、全寄与をONにしてから対象1項目だけをOFFにする。
	// 前段階の診断フラグが累積しないことを保証する。
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("r.BufferVisualizationTarget None"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("viewmode lit"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.DirectLighting 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.SkyLighting 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.DynamicShadows 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.ReflectionEnvironment 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.AmbientOcclusion 1"));
	if (Mode == TEXT("AllDirectLighting_OFF"))
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.DirectLighting 0"));
	}
	else if (Mode == TEXT("AllDirectionalLights_OFF") ||
		Mode == TEXT("AllLocalLights_OFF") ||
		Mode == TEXT("AllLightComponents_OFF"))
	{
		const FVector JenniferTarget = ResolveJenniferFaceTarget();
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || Actor->IsHidden())
			{
				continue;
			}

			TInlineComponentArray<ULightComponent*> LightComponents(Actor);
			for (ULightComponent* Component : LightComponents)
			{
				if (!Component || !Component->IsVisible())
				{
					continue;
				}

				const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Component);
				const bool bDirectional = Local == nullptr;
				bool bSelected = Mode == TEXT("AllLightComponents_OFF") ||
					(Mode == TEXT("AllDirectionalLights_OFF") && bDirectional);
				if (Mode == TEXT("AllLocalLights_OFF") && Local)
				{
					const float Distance = FVector::Distance(JenniferTarget, Component->GetComponentLocation());
					bSelected = Distance <= Local->AttenuationRadius;
				}
				if (bSelected)
				{
					DiagnosticGroupDisabledLights.Add(Component);
					Component->SetVisibility(false);
					UE_LOG(LogTemp, Warning,
						TEXT("[DIRECT_LIGHT_GROUP_AB] mode=%s disabled=%s component=%s type=%s mobility=%d intensity=%.2f"),
						*Mode, *Actor->GetName(), *Component->GetName(), *Component->GetClass()->GetName(),
						static_cast<int32>(Component->Mobility), Component->Intensity);
				}
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[CRIMSON_BUFFER_AB] scene=%s mode=%s"),
		*GetConversationLocationTagStem(CurrentConversationLocation), *Mode);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9142, 8.0f, FColor::Green,
			FString::Printf(TEXT("[CRIMSON BUFFER] F2: %s"), *Mode),
			true, FVector2D(1.5f, 1.5f));
	}
}

void ARealtimeTestActor::ResetDirectLightGroupDiagnostic()
{
	for (const TWeakObjectPtr<ULightComponent>& Light : DiagnosticGroupDisabledLights)
	{
		if (Light.IsValid())
		{
			Light->SetVisibility(true);
		}
	}
	DiagnosticGroupDisabledLights.Reset();
	if (GetWorld())
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.DirectLighting 1"));
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.SkyLighting 1"));
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.DynamicShadows 1"));
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.ReflectionEnvironment 1"));
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("ShowFlag.AmbientOcclusion 1"));
	}
}

void ARealtimeTestActor::HandleToggleJenniferConversationLightsDiagnostic()
{
	if (!GetWorld() || JenniferConversationLights.Num() == 0)
	{
		return;
	}

	bDiagnosticJenniferKeyLightProbeEnabled = !bDiagnosticJenniferKeyLightProbeEnabled;
	const FVector FaceTarget = ResolveJenniferFaceTarget();
	USpotLightComponent* KeyLight = JenniferConversationLights[0];
	if (bDiagnosticJenniferKeyLightProbeEnabled && KeyLight && IntroFaceCamera)
	{
		FVector TowardCamera = IntroFaceCamera->GetActorLocation() - FaceTarget;
		if (!TowardCamera.Normalize())
		{
			TowardCamera = FVector::ForwardVector;
		}
		KeyLight->SetVisibility(true);
		KeyLight->SetIntensity(500000.0f);
		KeyLight->SetLightColor(FLinearColor(1.0f, 0.0f, 0.35f));
		KeyLight->SetAttenuationRadius(1200.0f);
		KeyLight->SetInnerConeAngle(75.0f);
		KeyLight->SetOuterConeAngle(85.0f);
		KeyLight->SetWorldLocation(FaceTarget + TowardCamera * 100.0f);
		KeyLight->SetWorldRotation((FaceTarget - KeyLight->GetComponentLocation()).Rotation());
		for (int32 Index = 1; Index < JenniferConversationLights.Num(); ++Index)
		{
			if (JenniferConversationLights[Index])
			{
				JenniferConversationLights[Index]->SetVisibility(false);
			}
		}
	}
	else
	{
		const float Multiplier = CurrentConversationLocation == EConversationLocation::Classroom ? 64.0f : 1.0f;
		const float BaseIntensities[] = { 700.0f, 260.0f, 140.0f };
		const FLinearColor BaseColors[] =
		{
			FLinearColor(1.0f, 0.93f, 0.86f),
			FLinearColor(0.88f, 0.94f, 1.0f),
			FLinearColor(1.0f, 0.96f, 0.90f)
		};
		for (int32 Index = 0;
			Index < JenniferConversationLights.Num() && Index < UE_ARRAY_COUNT(BaseIntensities);
			++Index)
		{
			if (USpotLightComponent* Light = JenniferConversationLights[Index])
			{
				Light->SetVisibility(true);
				Light->SetIntensity(BaseIntensities[Index] * Multiplier);
				Light->SetLightColor(BaseColors[Index]);
				Light->SetAttenuationRadius(350.0f);
				Light->SetInnerConeAngle(35.0f);
				Light->SetOuterConeAngle(60.0f);
			}
		}
		UpdateJenniferConversationLightingTransform();
	}
	LogJenniferConversationLightingDiagnostics(CurrentConversationLocation);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9143, 8.0f,
			bDiagnosticJenniferKeyLightProbeEnabled ? FColor::Magenta : FColor::Cyan,
			FString::Printf(TEXT("[JENNIFER LIGHT PROBE] 8: %s"),
				bDiagnosticJenniferKeyLightProbeEnabled ? TEXT("MAGENTA KEY") : TEXT("RESTORED")),
			true, FVector2D(1.5f, 1.5f));
	}
}

void ARealtimeTestActor::LogJenniferConversationLightingDiagnostics(EConversationLocation Location) const
{
	const FString Scene = GetConversationLocationTagStem(Location);
	const FVector FaceTarget = ResolveJenniferFaceTarget();
	for (int32 Index = 0; Index < JenniferConversationLights.Num(); ++Index)
	{
		const USpotLightComponent* Light = JenniferConversationLights[Index];
		if (!Light)
		{
			continue;
		}
		const FVector ToFace = (FaceTarget - Light->GetComponentLocation()).GetSafeNormal();
		const float AngleDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(Light->GetForwardVector(), ToFace), -1.0f, 1.0f)));
		const FLightingChannels Channels = Light->LightingChannels;
		UE_LOG(LogTemp, Warning,
			TEXT("[JENNIFER_LIGHT_RUNTIME] scene=%s name=%s visible=%s active=%s mobility=%d intensity=%.2f color=%s radius=%.2f inner=%.2f outer=%.2f location=%s rotation=%s face_distance=%.2f face_angle=%.2f inside_cone=%s channels=%d%d%d"),
			*Scene, *Light->GetName(), Light->IsVisible() ? TEXT("true") : TEXT("false"),
			Light->IsActive() ? TEXT("true") : TEXT("false"), static_cast<int32>(Light->Mobility),
			Light->Intensity, *Light->GetLightColor().ToString(), Light->AttenuationRadius,
			Light->InnerConeAngle, Light->OuterConeAngle, *Light->GetComponentLocation().ToString(),
			*Light->GetComponentRotation().ToString(), FVector::Distance(Light->GetComponentLocation(), FaceTarget),
			AngleDegrees, AngleDegrees <= Light->OuterConeAngle ? TEXT("true") : TEXT("false"),
			Channels.bChannel0 ? 1 : 0, Channels.bChannel1 ? 1 : 0, Channels.bChannel2 ? 1 : 0);
	}
	if (CharacterActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(CharacterActor);
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			if (!Primitive)
			{
				continue;
			}
			const FLightingChannels Channels = Primitive->LightingChannels;
			UE_LOG(LogTemp, Warning,
				TEXT("[JENNIFER_PRIMITIVE_RUNTIME] scene=%s name=%s class=%s channels=%d%d%d visible=%s hidden=%s cast_shadow=%s"),
				*Scene, *Primitive->GetName(), *Primitive->GetClass()->GetName(),
				Channels.bChannel0 ? 1 : 0, Channels.bChannel1 ? 1 : 0, Channels.bChannel2 ? 1 : 0,
				Primitive->IsVisible() ? TEXT("true") : TEXT("false"),
				Primitive->bHiddenInGame ? TEXT("true") : TEXT("false"),
				Primitive->CastShadow ? TEXT("true") : TEXT("false"));
		}
	}
}

void ARealtimeTestActor::TrySetupVehicleMode()
{
	VehiclePawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	if (!VehiclePawn)
	{
		// プレイヤーがポーンに憑依していない場合、レベル内から車のポーンを直接探す
		TArray<AActor*> FoundPawns;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), FoundPawns);
		for (AActor* Pawn : FoundPawns)
		{
			if (Pawn->GetClass()->GetName().Contains(TEXT("Vehicle")))
			{
				VehiclePawn = Pawn;
				break;
			}
		}
	}

	if (!VehiclePawn)
	{
		++VehicleSetupRetryCount;

		// ワールドパーティションのストリーミングで本物の車が遅れて出現し、
		// 勝手にAuto Possessされる場合があるため、それより先にこちらが
		// スポーン&憑依してしまう(先に憑依しておけば、後から出てきた方に
		// 横取りされにくい)。ほぼ即座に(1回失敗したら)フォールバックする
		const int32 FallbackRetryThreshold = 1;
		if (VehicleSetupRetryCount >= FallbackRetryThreshold && VehiclePawnClassFallback)
		{
			FVector SpawnLocation = FVector::ZeroVector;
			FRotator SpawnRotation = FRotator::ZeroRotator;
			if (APlayerStart* PlayerStart = Cast<APlayerStart>(UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass())))
			{
				SpawnLocation = PlayerStart->GetActorLocation();
				SpawnRotation = PlayerStart->GetActorRotation();
			}

			const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

			// Deferred Spawn: BeginPlayが走る前(FinishSpawningの前)にPossessしておくことで、
			// 車のBlueprint側が「コントローラーがある状態」でBeginPlay/初期化を行えるようにする
			APawn* SpawnedVehicle = GetWorld()->SpawnActorDeferred<APawn>(
				VehiclePawnClassFallback, SpawnTransform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

			if (SpawnedVehicle)
			{
				VehiclePawn = SpawnedVehicle;
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
				{
					PC->Possess(SpawnedVehicle);
					SpawnedVehicle->FinishSpawning(SpawnTransform);

					// BP_VehicleAdvPlayerControllerは起動時に一度だけ車の入力設定
					// (Enhanced Input Mapping Context)を組み込んでいるが、その時点では
					// まだこの車が存在しないため、手動で憑依させた場合は運転入力が
					// 効かなくなってしまう。ここで明示的にマッピングを追加する
					if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
					{
						if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
						{
							if (UInputMappingContext* VehicleIMC = LoadObject<UInputMappingContext>(
								nullptr, TEXT("/Game/VehicleTemplate/Input/IMC_Vehicle_Default.IMC_Vehicle_Default")))
							{
								Subsystem->AddMappingContext(VehicleIMC, 0);
							}
						}
					}
				}
				UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: GameModeのスポーンに失敗したため、車を自前でスポーンしました"));
			}
		}

		// まだ車のポーンがスポーン/憑依されていない。次のタイマーで再試行する
		return;
	}

	GetWorldTimerManager().ClearTimer(VehicleSetupRetryTimerHandle);

	AttachCharacterToVehicle();
	ActivateVehicleCockpitCamera();

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->SetViewTargetWithBlend(VehiclePawn, 0.0f);
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 視点を車のコックピットカメラに切り替えました"));
	}

	// 車の生成・キャッシュを完了させてから、一度だけ部屋モードへ切り替える。
	// これにより起動時は部屋に居ながら、後からY/Hで車モードへ戻れる。
	if (!bInitialRoomModeApplied && RoomCharacterSeat && RoomPlayerSeat)
	{
		bInitialRoomModeApplied = true;
		HandleToggleSceneModeKeyPressed();
	}
}

void ARealtimeTestActor::NudgeCockpitCamera(const FVector& LocalDelta)
{
	if (!CachedFrontCameraMount)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CachedFrontCameraMountが未設定のため、位置調整できません"));
		return;
	}

	// DesiredFrontCameraOffsetは常にVR補正なしの「基準位置」。ここを直接動かして
	// 保存すれば、毎フレームEnforceFrontCameraOffsetが正しく反映・維持してくれる
	DesiredFrontCameraOffset += LocalDelta;
	EnforceFrontCameraOffset();

	const bool bSaved = FFileHelper::SaveStringToFile(DesiredFrontCameraOffset.ToString(), *GetCockpitCameraConfigPath());

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: FrontCamera relative location = %s (保存%s)"),
		*DesiredFrontCameraOffset.ToString(), bSaved ? TEXT("しました") : TEXT("に失敗しました"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9002, 3.f, FColor::Green,
			FString::Printf(TEXT("FrontCamera relative location: %s"), *DesiredFrontCameraOffset.ToString()), true, FVector2D(1.25f, 1.25f));
	}
}

namespace
{
	constexpr float CockpitCameraNudgeStepCm = 20.0f / 6.0f; // 約3.33cm
	constexpr float VREyeHeightNudgeStepCm = 15.0f / 6.0f; // 2.5cm
}

void ARealtimeTestActor::HandleCockpitNudgeForward()
{
	NudgeCockpitCamera(FVector(CockpitCameraNudgeStepCm, 0.0f, 0.0f));
}

void ARealtimeTestActor::HandleCockpitNudgeBackward()
{
	NudgeCockpitCamera(FVector(-CockpitCameraNudgeStepCm, 0.0f, 0.0f));
}

void ARealtimeTestActor::HandleCockpitNudgeLeft()
{
	NudgeCockpitCamera(FVector(0.0f, -CockpitCameraNudgeStepCm, 0.0f));
}

void ARealtimeTestActor::HandleCockpitNudgeRight()
{
	NudgeCockpitCamera(FVector(0.0f, CockpitCameraNudgeStepCm, 0.0f));
}

void ARealtimeTestActor::HandleCockpitNudgeUp()
{
	NudgeCockpitCamera(FVector(0.0f, 0.0f, CockpitCameraNudgeStepCm));
}

void ARealtimeTestActor::HandleCockpitNudgeDown()
{
	NudgeCockpitCamera(FVector(0.0f, 0.0f, -CockpitCameraNudgeStepCm));
}

void ARealtimeTestActor::AdjustVREyeHeightOffset(float DeltaCm)
{
	VehicleCockpitVREyeHeightOffsetCm += DeltaCm;
	FFileHelper::SaveStringToFile(FString::SanitizeFloat(VehicleCockpitVREyeHeightOffsetCm), *GetVREyeHeightConfigPath());
	EnforceFrontCameraOffset();

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: VehicleCockpitVREyeHeightOffsetCm = %.1f"), VehicleCockpitVREyeHeightOffsetCm);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9011, 3.f, FColor::Green,
			FString::Printf(TEXT("VR eye height offset: %.1f"), VehicleCockpitVREyeHeightOffsetCm), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleVREyeHeightIncrease()
{
	AdjustVREyeHeightOffset(VREyeHeightNudgeStepCm);
}

void ARealtimeTestActor::HandleVREyeHeightDecrease()
{
	AdjustVREyeHeightOffset(-VREyeHeightNudgeStepCm);
}

void ARealtimeTestActor::HandleResetVRCenter()
{
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		GEngine->XRSystem->ResetOrientationAndPosition();
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: VRのセンター位置をリセットしました"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9010, 2.f, FColor::Cyan, TEXT("VR center reset"), true, FVector2D(1.25f, 1.25f));
		}
	}
}

void ARealtimeTestActor::HandleQuitGame()
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), PC, EQuitPreference::Quit, false);
	}
}

void ARealtimeTestActor::HandleDebugDumpLiveLinkSubject()
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: LiveLinkClientが利用できません"));
		return;
	}

	ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	const FName SubjectName = TEXT("CABLE Output (VB-Audio Virtual Cable)");

	// まず、実際に存在する全Subject名とRoleをそのままログに出す(名前の食い違いを確認するため)
	TArray<FLiveLinkSubjectKey> AllSubjectKeys = Client.GetSubjects(/*bIncludeDisabledSubject=*/true, /*bIncludeVirtualSubject=*/true);
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 実在するSubject数=%d"), AllSubjectKeys.Num());
	for (const FLiveLinkSubjectKey& Key : AllSubjectKeys)
	{
		TSubclassOf<ULiveLinkRole> ActualRole = Client.GetSubjectRole_AnyThread(Key);
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor:   Subject名=[%s] Role=%s"),
			*Key.SubjectName.ToString(), ActualRole ? *ActualRole->GetName() : TEXT("null"));
	}

	FLiveLinkSubjectFrameData FrameData;
	const bool bSuccess = Client.EvaluateFrame_AnyThread(SubjectName, ULiveLinkBasicRole::StaticClass(), FrameData);

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: EvaluateFrame(%s) 成功=%s"), *SubjectName.ToString(), bSuccess ? TEXT("true") : TEXT("false"));

	if (bSuccess)
	{
		if (const FLiveLinkBaseStaticData* StaticData = FrameData.StaticData.Cast<FLiveLinkBaseStaticData>())
		{
			UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Property数=%d"), StaticData->PropertyNames.Num());
			const FLiveLinkBaseFrameData* BaseFrame = FrameData.FrameData.Cast<FLiveLinkBaseFrameData>();
			for (int32 i = 0; i < StaticData->PropertyNames.Num(); ++i)
			{
				const float Value = (BaseFrame && BaseFrame->PropertyValues.IsValidIndex(i)) ? BaseFrame->PropertyValues[i] : 0.0f;
				UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor:   [%d] %s = %f"), i, *StaticData->PropertyNames[i].ToString(), Value);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: StaticDataがFLiveLinkBaseStaticData形式ではありません(型=%s)"),
				FrameData.StaticData.GetStruct() ? *FrameData.StaticData.GetStruct()->GetName() : TEXT("null"));
		}
	}

	// 【最優先】PIEワールド内の全SkeletalMeshComponentのうち、Visible=trueまたは
	// RecentlyRendered=trueのものを全部洗い出す(メッシュ名では絞り込まない)。
	// 実際に画面に描画されている「本物の顔担当」コンポーネントを特定する
	if (UWorld* World = GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== 描画中コンポーネント検索開始 ==="));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			TArray<USkeletalMeshComponent*> Comps;
			Actor->GetComponents<USkeletalMeshComponent>(Comps);
			for (USkeletalMeshComponent* Comp : Comps)
			{
				if (!Comp)
				{
					continue;
				}
				const bool bVis = Comp->IsVisible();
				const bool bRecent = Comp->WasRecentlyRendered(0.5f);
				if (bVis || bRecent)
				{
					UE_LOG(LogTemp, Warning, TEXT("VISIBLE_COMP Actor=%s ActorClass=%s Comp=%s Mesh=%s Visible=%s Recently=%s"),
						*Actor->GetName(), *Actor->GetClass()->GetName(), *Comp->GetName(),
						Comp->GetSkeletalMeshAsset() ? *Comp->GetSkeletalMeshAsset()->GetPathName() : TEXT("NULL"),
						bVis ? TEXT("true") : TEXT("false"), bRecent ? TEXT("true") : TEXT("false"));
				}
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("=== 描画中コンポーネント検索終了 ==="));
	}

	// 【決定的テスト】CachedFaceComponentが実際に画面に描画されているコンポーネントかを疑い、
	// CharacterActor配下の全SkeletalMeshComponentを列挙して、どれが実際に見えているか確認する
	if (CharacterActor)
	{
		TArray<USkeletalMeshComponent*> AllSkelComps;
		CharacterActor->GetComponents<USkeletalMeshComponent>(AllSkelComps);
		for (USkeletalMeshComponent* Comp : AllSkelComps)
		{
			if (!Comp)
			{
				continue;
			}
			UE_LOG(LogTemp, Warning, TEXT("COMP Name=%s Visible=%s Recently=%s Mesh=%s AnimClass=%s PostProcess=%s"),
				*Comp->GetName(),
				Comp->IsVisible() ? TEXT("true") : TEXT("false"),
				Comp->WasRecentlyRendered(0.5f) ? TEXT("true") : TEXT("false"),
				Comp->GetSkeletalMeshAsset() ? *Comp->GetSkeletalMeshAsset()->GetName() : TEXT("NULL"),
				Comp->GetAnimInstance() ? *Comp->GetAnimInstance()->GetClass()->GetName() : TEXT("NULL"),
				Comp->GetPostProcessInstance() ? *Comp->GetPostProcessInstance()->GetClass()->GetName() : TEXT("NULL"));
		}
	}

	// 【切り分け】LifeSimARKitFace自体にCTRL_expressions.jawOpenが本当に含まれているか、
	// このタイミングで直接EvaluateFrameして確認する(ULiveLinkInstanceが使っている購読先)。
	// PropertyNamesは後の集合比較でも使うため、この関数スコープで保持しておく
	TArray<FName> SourcePropertyNames;
	{
		const FName ArkitSubjectName = TEXT("LifeSimARKitFace");
		FLiveLinkSubjectFrameData ArkitFrameData;
		const bool bArkitSuccess = Client.EvaluateFrame_AnyThread(ArkitSubjectName, ULiveLinkBasicRole::StaticClass(), ArkitFrameData);
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: EvaluateFrame(LifeSimARKitFace) 成功=%s"), bArkitSuccess ? TEXT("true") : TEXT("false"));
		if (bArkitSuccess)
		{
			if (const FLiveLinkBaseStaticData* ArkitStatic = ArkitFrameData.StaticData.Cast<FLiveLinkBaseStaticData>())
			{
				SourcePropertyNames = ArkitStatic->PropertyNames;
				const FLiveLinkBaseFrameData* ArkitFrame = ArkitFrameData.FrameData.Cast<FLiveLinkBaseFrameData>();
				const int32 Idx = ArkitStatic->PropertyNames.IndexOfByKey(FName(TEXT("CTRL_expressions.jawOpen")));
				if (Idx != INDEX_NONE)
				{
					const float V = (ArkitFrame && ArkitFrame->PropertyValues.IsValidIndex(Idx)) ? ArkitFrame->PropertyValues[Idx] : -999.0f;
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: LifeSimARKitFaceのCTRL_expressions.jawOpen(index=%d) = %f"), Idx, V);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: LifeSimARKitFaceにCTRL_expressions.jawOpenというPropertyが存在しません"));
				}
			}
		}
	}

	// 【決定的テスト】CTRL_expressions.jawOpenが、LiveLink元のデータだけでなく、
	// 実際にFaceのAnimInstance/SkeletalMeshComponentの「評価済みカーブ」まで
	// 届いているかを直接測定する
	if (CharacterActor)
	{
		TArray<USkeletalMeshComponent*> SkeletalComps;
		CharacterActor->GetComponents<USkeletalMeshComponent>(SkeletalComps);

		USkeletalMeshComponent* FaceComp = nullptr;
		for (USkeletalMeshComponent* Comp : SkeletalComps)
		{
			if (Comp && Comp->GetFName() == TEXT("Face"))
			{
				FaceComp = Comp;
				break;
			}
		}

		if (FaceComp)
		{
			const FName JawCurve(TEXT("CTRL_expressions.jawOpen"));

			float CompValue = 0.0f;
			const bool bCompFound = FaceComp->GetCurveValue(JawCurve, -999.0f, CompValue);

			UAnimInstance* AnimInst = FaceComp->GetAnimInstance();

			// GetCurveValueはAttributeCurveタイプしか見ないため、他のタイプ(Material/MorphTarget)に
			// 分類されていないか、全タイプを横断して確認する
			if (AnimInst)
			{
				const EAnimCurveType TypesToCheck[] = { EAnimCurveType::AttributeCurve, EAnimCurveType::MaterialCurve, EAnimCurveType::MorphTargetCurve };
				const TCHAR* TypeNames[] = { TEXT("Attribute"), TEXT("Material"), TEXT("MorphTarget") };
				TArray<FName> ActiveAttributeNames;
				for (int32 t = 0; t < 3; ++t)
				{
					TArray<FName> ActiveNames;
					AnimInst->GetActiveCurveNames(TypesToCheck[t], ActiveNames);
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: タイプ%s のアクティブカーブ数=%d"), TypeNames[t], ActiveNames.Num());
					for (const FName& Name : ActiveNames)
					{
						if (Name.ToString().Contains(TEXT("jaw"), ESearchCase::IgnoreCase))
						{
							UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   [%s] jawを含む: %s"), TypeNames[t], *Name.ToString());
						}
					}
					if (TypesToCheck[t] == EAnimCurveType::AttributeCurve)
					{
						ActiveAttributeNames = ActiveNames;
					}
				}

				// 【集合比較】LifeSimARKitFaceの260個のProperty名と、実際にActiveなAttributeカーブ名の
				// 集合を比較し、そもそも交差があるのか、あるならjawOpenが含まれるかを確認する
				{
					TSet<FName> SourceSet(SourcePropertyNames);
					TSet<FName> ActiveSet(ActiveAttributeNames);
					TSet<FName> Intersection = SourceSet.Intersect(ActiveSet);

					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: SourcePropertyCount=%d ActiveAttributeCount=%d IntersectionCount=%d"),
						SourcePropertyNames.Num(), ActiveAttributeNames.Num(), Intersection.Num());

					int32 Printed = 0;
					for (const FName& Name : Intersection)
					{
						UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   共通名: %s"), *Name.ToString());
						if (++Printed >= 20)
						{
							break;
						}
					}

					const bool bJawInSource = SourceSet.Contains(FName(TEXT("CTRL_expressions.jawOpen")));
					const bool bJawInActive = ActiveSet.Contains(FName(TEXT("CTRL_expressions.jawOpen")));
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: CTRL_expressions.jawOpen Source内=%s Active内=%s"),
						bJawInSource ? TEXT("true") : TEXT("false"), bJawInActive ? TEXT("true") : TEXT("false"));
				}
			}

			float AnimValue = -999.0f;
			bool bAnimFound = false;
			if (AnimInst)
			{
				bAnimFound = AnimInst->GetCurveValue(JawCurve, AnimValue);
			}

			UE_LOG(LogTemp, Warning,
				TEXT("RealtimeTestActor: JawCurve: Component Found=%s Value=%f / AnimInstance Found=%s Value=%f / AnimClass=%s"),
				bCompFound ? TEXT("true") : TEXT("false"), CompValue,
				bAnimFound ? TEXT("true") : TEXT("false"), AnimValue,
				AnimInst ? *AnimInst->GetClass()->GetPathName() : TEXT("NULL"));

			// 【切り分け】実際にRigLogicを実行しているのはPost-Process AnimInstanceの方なので、
			// そちらのカーブ値も別途確認する(メインAnimInstanceとは別オブジェクト)
			if (UAnimInstance* PostProcessInst = FaceComp->GetPostProcessInstance())
			{
				float PostProcessValue = -999.0f;
				const bool bPostProcessFound = PostProcessInst->GetCurveValue(JawCurve, PostProcessValue);
				UE_LOG(LogTemp, Warning,
					TEXT("RealtimeTestActor: JawCurve(PostProcess): Found=%s Value=%f / PostProcessClass=%s"),
					bPostProcessFound ? TEXT("true") : TEXT("false"), PostProcessValue,
					*PostProcessInst->GetClass()->GetPathName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Post-Process AnimInstanceがありません"));
			}

					// 【決定的比較】Face_AnimBPエディタのPreview Meshでは口が開くことを確認済み。
			// ゲーム実行中のFaceコンポーネントの実際の構成(Mesh/AnimClass/PostProcessClass)を
			// フルパスでログ出力し、成功したプレビュー構成と完全一致するか比較する
			UE_LOG(LogTemp, Warning, TEXT("RUNTIME_FACE Mesh=%s"),
				FaceComp->GetSkeletalMeshAsset() ? *FaceComp->GetSkeletalMeshAsset()->GetPathName() : TEXT("NULL"));
			UE_LOG(LogTemp, Warning, TEXT("RUNTIME_FACE MainAnim=%s"),
				FaceComp->GetAnimInstance() ? *FaceComp->GetAnimInstance()->GetClass()->GetPathName() : TEXT("NULL"));
			UE_LOG(LogTemp, Warning, TEXT("RUNTIME_FACE PostProcess=%s"),
				FaceComp->GetPostProcessInstance() ? *FaceComp->GetPostProcessInstance()->GetClass()->GetPathName() : TEXT("NULL"));
			UE_LOG(LogTemp, Warning, TEXT("RUNTIME_FACE PredictedLOD=%d ForcedLOD=%d Visible=%s VisibilityBasedAnimTick=%d"),
				FaceComp->GetPredictedLODLevel(), FaceComp->GetForcedLOD(),
				FaceComp->IsVisible() ? TEXT("true") : TEXT("false"),
				(int32)FaceComp->VisibilityBasedAnimTickOption);

	// 【診断】Post-Process AnimInstanceのクラスに、RigLogic関連のAnimNodeプロパティが
			// 実際に(コンパイル後に)存在するかを、Blueprintの見た目に頼らずリフレクションで確認する
			if (UAnimInstance* PPForDump = FaceComp->GetPostProcessInstance())
			{
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: PostProcessクラスのAnimNodeプロパティ一覧:"));
				for (TFieldIterator<FStructProperty> PropIt(PPForDump->GetClass()); PropIt; ++PropIt)
				{
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   プロパティ名=%s 型=%s"),
						*PropIt->GetName(), PropIt->Struct ? *PropIt->Struct->GetName() : TEXT("?"));
				}
			}

			// 【決定的テスト】RigLogicノードの内部(private)には触らず、SKM_Payton_FB_Character_FaceMesh
			// 自体に付いているDNAアセットを直接読み、CTRL_expressions.jawOpenが「入力コントロール」として
			// 本当に存在するかを確認する。RigLogicは常に現在バインドされているメッシュのDNAを読むため、
			// これで「DNAにjawOpen入力自体が無い」のか「DNAにはあるのに反映されない」のかを切り分けられる
			if (USkeletalMesh* MeshForDNA = FaceComp->GetSkeletalMeshAsset())
			{
				// 【切り分け】UDNAAsset/UDNAAssetUserDataという型名で決め打ちせず、
				// 実際に付いているAsset User Dataを全部列挙して確認する
				if (const TArray<UAssetUserData*>* UserDataArray = MeshForDNA->GetAssetUserDataArray())
				{
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: AssetUserData件数=%d"), UserDataArray->Num());
					for (UAssetUserData* Data : *UserDataArray)
					{
						if (Data)
						{
							UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   UserData クラス=%s 名前=%s"),
								*Data->GetClass()->GetPathName(), *Data->GetName());
						}
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: GetAssetUserDataArrayがnullでした"));
				}

				UDNA* DNA = nullptr;
				if (UDNAAssetUserData* DNAUserData = MeshForDNA->GetAssetUserData<UDNAAssetUserData>())
				{
					DNA = DNAUserData->DNAAsset;
				}
				if (DNA)
				{
					TSharedPtr<IDNAReader> Reader = DNA->GetDNAReader();
					if (Reader.IsValid())
					{
						const uint16 ControlCount = Reader->GetRawControlCount();
						UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: DNA RawControlCount=%d"), ControlCount);
						bool bFoundJawOpen = false;
						for (uint16 i = 0; i < ControlCount; ++i)
						{
							const FString CtrlName = Reader->GetRawControlName(i);
							if (CtrlName.Contains(TEXT("jaw"), ESearchCase::IgnoreCase))
							{
								UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   DNA RawControl[%d] = %s"), i, *CtrlName);
								if (CtrlName.Equals(TEXT("CTRL_expressions.jawOpen"), ESearchCase::IgnoreCase))
								{
									bFoundJawOpen = true;
								}
							}
						}
						UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: DNAにCTRL_expressions.jawOpenが存在=%s"),
							bFoundJawOpen ? TEXT("true") : TEXT("false"));
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: DNAReaderが無効です"));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: このメッシュにUDNAAssetが付いていません"));
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Faceコンポーネントが見つかりませんでした(測定スキップ)"));
		}
	}
}

void ARealtimeTestActor::HandleDebugTogglePostProcess()
{
	if (!CharacterActor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> SkeletalComps;
	CharacterActor->GetComponents<USkeletalMeshComponent>(SkeletalComps);

	USkeletalMeshComponent* FaceComp = nullptr;
	for (USkeletalMeshComponent* Comp : SkeletalComps)
	{
		if (Comp && Comp->GetFName() == TEXT("Face"))
		{
			FaceComp = Comp;
			break;
		}
	}

	if (!FaceComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Faceコンポーネントが見つかりませんでした"));
		return;
	}

	const bool bNewState = !FaceComp->GetDisablePostProcessBlueprint();
	FaceComp->SetDisablePostProcessBlueprint(bNewState);
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Post-Process AnimBPを%sにしました"), bNewState ? TEXT("無効") : TEXT("有効"));
}

void ARealtimeTestActor::HandleDebugToggleFaceVisibility()
{
	if (!CharacterActor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> SkeletalComps;
	CharacterActor->GetComponents<USkeletalMeshComponent>(SkeletalComps);

	for (USkeletalMeshComponent* Comp : SkeletalComps)
	{
		if (Comp && Comp->GetFName() == TEXT("Face"))
		{
			// Bodyのテストと完全に同じ、SetVisibilityだけのシンプルな形に揃える
			const bool bNewVisible = !Comp->IsVisible();
			// Face本体だけを切り替える。子のGroom等まで表示すると、現在のFaceに
			// 非対応のBindingが有効化されて赤く崩れた形状が重なるため伝播させない。
			Comp->SetVisibility(bNewVisible, false);
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: FaceのVisibilityを%sにしました"), bNewVisible ? TEXT("true") : TEXT("false"));
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Faceコンポーネントが見つかりませんでした"));
}

void ARealtimeTestActor::HandleDebugToggleAddedStaticMeshVisibility()
{
	if (!CharacterActor)
	{
		return;
	}

	TArray<UStaticMeshComponent*> StaticMeshComps;
	CharacterActor->GetComponents<UStaticMeshComponent>(StaticMeshComps);
	for (UStaticMeshComponent* Comp : StaticMeshComps)
	{
		if (Comp && Comp->GetFName() == TEXT("StaticMesh"))
		{
			const bool bNewVisible = !Comp->IsVisible();
			Comp->SetVisibility(bNewVisible, true);
			UE_LOG(LogTemp, Warning,
				TEXT("RealtimeTestActor: StaticMeshのVisibilityを%sにしました Asset=%s"),
				bNewVisible ? TEXT("true") : TEXT("false"),
				Comp->GetStaticMesh() ? *Comp->GetStaticMesh()->GetPathName() : TEXT("NULL"));
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: StaticMeshコンポーネントが見つかりませんでした"));
}

void ARealtimeTestActor::HandleDebugToggleHairVisibility()
{
	if (!CharacterActor)
	{
		return;
	}

	TArray<UActorComponent*> Components;
	CharacterActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!Comp)
		{
			continue;
		}

		// BP_Paytonの頭髪コンポーネントはHair。Eyebrows/Eyelashes/PeachFuzzは
		// FaceメッシュとのBinding互換性を別途確認するため、ここでは表示しない。
		if (Comp->GetFName() == TEXT("Hair") && Comp->GetClass()->GetName().Contains(TEXT("Groom")))
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
			{
				const bool bNewVisible = !SceneComp->IsVisible();
				SceneComp->SetVisibility(bNewVisible, false);
				UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Hair GroomのVisibilityを%sにしました Class=%s"),
					bNewVisible ? TEXT("true") : TEXT("false"), *Comp->GetClass()->GetPathName());
				return;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Hair Groomコンポーネントが見つかりませんでした"));
}

void ARealtimeTestActor::HandleDebugToggleBodyVisibility()
{
	if (!CharacterActor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> SkeletalComps;
	CharacterActor->GetComponents<USkeletalMeshComponent>(SkeletalComps);

	for (USkeletalMeshComponent* Comp : SkeletalComps)
	{
		if (Comp && Comp->GetFName() == TEXT("Body"))
		{
			const bool bNewVisible = !Comp->IsVisible();
			Comp->SetVisibility(bNewVisible, true);
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: BodyのVisibilityを%sにしました"), bNewVisible ? TEXT("true") : TEXT("false"));
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Bodyコンポーネントが見つかりませんでした"));
}

void ARealtimeTestActor::HandleDebugToggleOriginalPoseableVisibility()
{
	if (!OriginalPaytonPoseableMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: OriginalPaytonPoseableMeshが見つかりませんでした"));
		return;
	}

	const bool bNewVisible = !OriginalPaytonPoseableMesh->IsVisible();
	OriginalPaytonPoseableMesh->SetVisibility(bNewVisible, false);
	UE_LOG(LogTemp, Warning,
		TEXT("RealtimeTestActor: V2 PoseableMeshのVisibilityを%sにしました Mesh=%s"),
		bNewVisible ? TEXT("true") : TEXT("false"),
		OriginalPaytonPoseableMesh->GetSkinnedAsset()
			? *OriginalPaytonPoseableMesh->GetSkinnedAsset()->GetPathName()
			: TEXT("NULL"));
}

void ARealtimeTestActor::HandleDebugToggleOriginalPoseableRootOffset()
{
	if (!OriginalPaytonPoseableMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: OriginalPaytonPoseableMeshが見つかりませんでした"));
		return;
	}

	const FName RootBone(TEXT("root"));
	FTransform RootTransform = OriginalPaytonPoseableMesh->GetBoneTransformByName(RootBone, EBoneSpaces::ComponentSpace);
	RootTransform.AddToTranslation(FVector(0.0f, 0.0f, bDebugOriginalPoseableRootRaised ? -20.0f : 20.0f));
	bDebugOriginalPoseableRootRaised = !bDebugOriginalPoseableRootRaised;
	OriginalPaytonPoseableMesh->SetBoneTransformByName(RootBone, RootTransform, EBoneSpaces::ComponentSpace);
	OriginalPaytonPoseableMesh->RefreshBoneTransforms();

	const FTransform ReadBack = OriginalPaytonPoseableMesh->GetBoneTransformByName(RootBone, EBoneSpaces::ComponentSpace);
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2 root骨20cmテスト Raised=%s ReadBack=%s"),
		bDebugOriginalPoseableRootRaised ? TEXT("true") : TEXT("false"),
		*ReadBack.GetTranslation().ToString());
}

void ARealtimeTestActor::HandleDebugToggleOriginalPoseableJawOffset()
{
	bDebugOriginalPoseableJawOffset = !bDebugOriginalPoseableJawOffset;
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2 jaw右30cmテスト Enabled=%s"),
		bDebugOriginalPoseableJawOffset ? TEXT("true") : TEXT("false"));
}

void ARealtimeTestActor::HandleDebugToggleOriginalPoseableJawDown()
{
	bDebugOriginalPoseableJawDown = !bDebugOriginalPoseableJawDown;
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: V2 jaw下10cmテスト Enabled=%s"),
		bDebugOriginalPoseableJawDown ? TEXT("true") : TEXT("false"));
}

void ARealtimeTestActor::HandleTogglePaytonV9()
{
	if ((!CrimsonGazeMorphComponent && !PaytonV9FaceComponent) || !OriginalPaytonMorphMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Crimson Gaze比較表示コンポーネントが未準備です"));
		return;
	}

	bShowingPaytonV9 = !bShowingPaytonV9;
	if (CrimsonGazeMorphComponent)
	{
		CrimsonGazeMorphComponent->SetVisibility(bShowingPaytonV9, false);
	}
	if (PaytonV9FaceComponent)
	{
		PaytonV9FaceComponent->SetVisibility(bShowingPaytonV9, false);
	}
	if (PaytonV9HairComponent)
	{
		PaytonV9HairComponent->SetVisibility(bShowingPaytonV9, false);
	}
	OriginalPaytonMorphMesh->SetVisibility(!bShowingPaytonV9, false);
	if (OriginalPaytonMouthComponent)
	{
		OriginalPaytonMouthComponent->SetVisibility(!bShowingPaytonV9, false);
	}
	if (OriginalPaytonUpperTeethComponent) OriginalPaytonUpperTeethComponent->SetVisibility(false, false);
	if (CrimsonGazeUpperTeethComponent) CrimsonGazeUpperTeethComponent->SetVisibility(false, false);
	if (LipSync)
	{
		LipSync->SetExpressionFaceMesh(bShowingPaytonV9 ? CrimsonGazeMorphComponent : OriginalPaytonMorphMesh);
	}
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Payton表示を%sへ切り替えました"),
		bShowingPaytonV9 ? TEXT("Crimson Gaze静止モデル") : TEXT("現在の口パクモデル"));
}

void ARealtimeTestActor::HandleToggleSceneModeKeyPressed()
{
	if (!RoomCharacterSeat || !RoomPlayerSeat)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: RoomCharacterSeatまたはRoomPlayerSeatが未設定のため、シーン切り替えができません"));
		return;
	}

	bIsInRoomMode = !bIsInRoomMode;

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	if (bIsInRoomMode)
	{
		// 部屋モードへ切り替え: 助手席へのアタッチを解除してから、
		// キャラクターとプレイヤー(カメラ)をソファの位置へ移動させる
		if (CharacterActor)
		{
			CharacterActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			CharacterActor->SetActorEnableCollision(true);
		}
		TeleportCharacterActorTo(RoomCharacterSeat->GetActorLocation(), RoomCharacterSeat->GetActorRotation());
		TeleportPlayerPawnTo(RoomPlayerSeat->GetActorLocation(), RoomPlayerSeat->GetActorRotation());
		// MyRoomの正常な構図をCanonical Framingとする。別シーンで変更されたFOVも
		// 必ず元カメラ由来のMyRoom FOVへ戻す。
		CaptureCanonicalConversationFraming();
		ApplyConversationSceneExposure(EConversationLocation::MyRoom);
		SetPaytonFillLightsEnabled(false);

		if (PC && IntroFaceCamera)
		{
			PC->SetViewTargetWithBlend(IntroFaceCamera, 0.0f);
		}

		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [Mキー] 部屋モードに切り替えました"));
		CurrentConversationLocation = EConversationLocation::MyRoom;

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("(Room mode)"), true, FVector2D(1.25f, 1.25f));
		}
	}
	else
	{
		// 車モードへ戻す: Paytonを助手席に再アタッチし、コックピットカメラへ視点を戻す
		AttachCharacterToVehicle();
		ActivateVehicleCockpitCamera();
		SetPaytonFillLightsEnabled(true);

		if (PC && VehiclePawn)
		{
			PC->SetViewTargetWithBlend(VehiclePawn, 0.0f);
		}

		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [Mキー] 車モードに切り替えました"));
		CurrentConversationLocation = EConversationLocation::Drive;

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("(Cockpit mode)"), true, FVector2D(1.25f, 1.25f));
		}
	}
}

void ARealtimeTestActor::HandleForceMyRoomKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::MyRoom);
}

void ARealtimeTestActor::HandleForceClassroomKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::Classroom);
}

void ARealtimeTestActor::HandleForceCinemaKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::Cinema);
}

void ARealtimeTestActor::HandleForceDriveKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::Drive);
}

void ARealtimeTestActor::HandleForceJenniferRoomKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::JenniferRoom);
}

void ARealtimeTestActor::HandleForceWalkKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::Walk);
}

void ARealtimeTestActor::HandleForceRestaurantKeyPressed()
{
	TryMoveToConversationLocation(EConversationLocation::Restaurant);
}

void ARealtimeTestActor::HandleConnected()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 接続成功。話しかけてみてください"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("Connected! Just start talking."), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleDisconnected(const FString& Reason)
{
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 切断されました(%s)"), *Reason);
	if (!bLegacyVoiceEnabled)
	{
		StartLegacyVoice();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Disconnected: %s"), *Reason), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleError(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeTestActor: エラー(%s)"), *ErrorMessage);
	// Realtime接続に失敗しても会話不能にならないよう、通常モードへ戻す。
	if (!bLegacyVoiceEnabled)
	{
		StartLegacyVoice();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, FString::Printf(TEXT("Error: %s"), *ErrorMessage), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleUserTranscript(const FString& Text)
{
	bNodTriggeredForCurrentAssistantResponse = false;
	bHandGestureTriggeredForCurrentAssistantResponse = false;
	// 前ターンで音声生成前に失敗・中断した予約を次の返答へ持ち越さない。
	PendingHandGestureSource.Reset();
	PendingHandGestureId.Reset();
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: You: %s"), *Text);
	SessionTranscriptLog.Add(FString::Printf(TEXT("Hiro: %s"), *Text));

	// 「教室に行こう」のような誘い文句だけでなく、Jenniferの「どこがいい？」に対して
	// 「教室」「Restaurant」と場所の名前だけを返すような自然な返事も拾えるよう、
	// 場所名が含まれていればそれだけで提案候補として扱う(実際に移動するかどうかは、
	// この後のJenniferの返事がIsJenniferAgreementで明確なYesかどうかで決まる)
	const EConversationLocation ProposedLocation = DetectProposedLocation(Text);
	if (ProposedLocation != EConversationLocation::None)
	{
		PendingProposedLocation = ProposedLocation;
		PendingProposalTurnsRemaining = 3;
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 場所移動の提案を検出しました Location=%s"),
			*GetConversationLocationDisplayName(ProposedLocation));
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan, FString::Printf(TEXT("You: %s"), *Text), true, FVector2D(1.25f, 1.25f));
	}
}

void ARealtimeTestActor::HandleAssistantTranscript(const FString& Text)
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: AI: %s"), *Text);
	SessionTranscriptLog.Add(FString::Printf(TEXT("Jennifer: %s"), *Text));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Yellow, FString::Printf(TEXT("AI: %s"), *Text), true, FVector2D(1.25f, 1.25f));
	}

	// tool_choice=autoでは、モデルが明確に同意していても文章だけを返し、
	// nod_headを省略する場合がある。Function Callingを第一経路としつつ、
	// 既存の厳格な同意判定を満たした場合だけ一度補完する。
	if (!bNodTriggeredForCurrentAssistantResponse && IsJenniferAgreement(Text))
	{
		const TCHAR* SourceTag = RealtimeVoice && RealtimeVoice->IsConnected()
			? TEXT("REALTIME_FALLBACK")
			: TEXT("LEGACY_FALLBACK");
		StartNod(SourceTag);
		bNodTriggeredForCurrentAssistantResponse = true;
		UE_LOG(LogTemp, Log, TEXT("[NOD][%s] explicit agreement fallback text=%s"), SourceTag, *Text);
	}

	if (!bHandGestureTriggeredForCurrentAssistantResponse && ShouldUseHandGestureFallback(Text))
	{
		const TCHAR* SourceTag = RealtimeVoice && RealtimeVoice->IsConnected()
			? TEXT("REALTIME_FALLBACK")
			: TEXT("LEGACY_FALLBACK");
		RequestHandGesture(TEXT("raise_right_arm"), SourceTag);
		bHandGestureTriggeredForCurrentAssistantResponse = true;
		UE_LOG(LogTemp, Log, TEXT("[GESTURE][%s] semantic fallback text=%s"), SourceTag, *Text);
	}

	if (PendingProposedLocation != EConversationLocation::None)
	{
		const EConversationLocation ProposedLocation = PendingProposedLocation;
		if (IsJenniferAgreement(Text))
		{
			PendingProposedLocation = EConversationLocation::None;

			// この文字起こしが確定した時点では、対応する音声の再生がまだ始まって
			// すらいないことが多い(テキストと音声は別々に届く)。固定の待ち時間を
			// 予測するのではなく、実際にIsAssistantSpeaking()がfalseになる
			// (=本当に喋り終わる)まで一定間隔でチェックし続けてから移動する。
			UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Jenniferが移動に同意しました Location=%s (喋り終わるのを待機します)"),
				*GetConversationLocationDisplayName(ProposedLocation));

			PendingLocationMoveTarget = ProposedLocation;
			PendingLocationMoveElapsedSeconds = 0.0f;
			// 負値は「音声再生をまだ一度も観測していない」を表す。
			PendingLocationMoveQuietSeconds = -1.0f;
			GetWorldTimerManager().ClearTimer(PendingLocationMoveTimerHandle);
			GetWorldTimerManager().SetTimer(PendingLocationMoveTimerHandle, this,
				&ARealtimeTestActor::CheckPendingLocationMove, 0.15f, true);
		}
		else if (IsExplicitRejection(Text))
		{
			PendingProposedLocation = EConversationLocation::None;
			UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Jenniferが移動を断ったため保留を破棄します Location=%s"),
				*GetConversationLocationDisplayName(ProposedLocation));
		}
		else
		{
			// 「学校をVRで見て回るってこと？」のような聞き返しは、賛成でも拒否でもない。
			// ここで即座に提案を破棄せず、数ターンだけ保持して次のユーザーの返事を待つ。
			--PendingProposalTurnsRemaining;
			if (PendingProposalTurnsRemaining <= 0)
			{
				PendingProposedLocation = EConversationLocation::None;
				UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Jenniferの明確な返事が得られないまま保留期限切れ Location=%s"),
					*GetConversationLocationDisplayName(ProposedLocation));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Jenniferの返事が曖昧なため提案を保留継続 Location=%s 残りターン=%d"),
					*GetConversationLocationDisplayName(ProposedLocation), PendingProposalTurnsRemaining);
			}
		}
	}
}

ARealtimeTestActor::EConversationLocation ARealtimeTestActor::DetectProposedLocation(const FString& UserText) const
{
	const FString T = UserText.ToLower();
	if (T.Contains(TEXT("教室")) || T.Contains(TEXT("classroom")) || T.Contains(TEXT("school"))) return EConversationLocation::Classroom;
	if (T.Contains(TEXT("映画館")) || T.Contains(TEXT("映画")) || T.Contains(TEXT("cinema")) || T.Contains(TEXT("movie theater")) || T.Contains(TEXT("movie"))) return EConversationLocation::Cinema;
	if (T.Contains(TEXT("ドライブ")) || T.Contains(TEXT("車内")) || T.Contains(TEXT("drive")) || T.Contains(TEXT("car"))) return EConversationLocation::Drive;
	if (T.Contains(TEXT("彼女の部屋")) || T.Contains(TEXT("君の部屋")) || T.Contains(TEXT("あなたの部屋")) || T.Contains(TEXT("your room")) || T.Contains(TEXT("jennifer's room"))) return EConversationLocation::JenniferRoom;
	if (T.Contains(TEXT("自分の部屋")) || T.Contains(TEXT("僕の部屋")) || T.Contains(TEXT("俺の部屋")) || T.Contains(TEXT("my room"))) return EConversationLocation::MyRoom;
	if (T.Contains(TEXT("散歩")) || T.Contains(TEXT("歩こう")) || T.Contains(TEXT("walk")) || T.Contains(TEXT("stroll"))) return EConversationLocation::Walk;
	if (T.Contains(TEXT("レストラン")) || T.Contains(TEXT("食事に")) || T.Contains(TEXT("restaurant")) || T.Contains(TEXT("dinner"))) return EConversationLocation::Restaurant;
	return EConversationLocation::None;
}

bool ARealtimeTestActor::IsExplicitRejection(const FString& AssistantText) const
{
	const FString T = AssistantText.ToLower();
	return T.Contains(TEXT("no,")) || T.StartsWith(TEXT("no ")) || T.Contains(TEXT("not now"))
		|| T.Contains(TEXT("can't")) || T.Contains(TEXT("cannot")) || T.Contains(TEXT("rather not"))
		|| T.Contains(TEXT("maybe later")) || T.Contains(TEXT("やめて")) || T.Contains(TEXT("行かない"));
}

bool ARealtimeTestActor::IsJenniferAgreement(const FString& AssistantText) const
{
	if (IsExplicitRejection(AssistantText))
	{
		return false;
	}
	const FString T = AssistantText.ToLower();
	return T.Contains(TEXT("yes")) || T.Contains(TEXT("yeah")) || T.Contains(TEXT("yep"))
		|| T.Contains(TEXT("sure")) || T.Contains(TEXT("let's go")) || T.Contains(TEXT("lets go"))
		|| T.Contains(TEXT("i'd love")) || T.Contains(TEXT("sounds good")) || T.Contains(TEXT("sounds fun"))
		|| T.Contains(TEXT("sounds like fun")) || T.Contains(TEXT("okay")) || T.Contains(TEXT("ok,"))
		|| T.Contains(TEXT("of course")) || T.Contains(TEXT("absolutely")) || T.Contains(TEXT("definitely"))
		|| T.Contains(TEXT("totally")) || T.Contains(TEXT("i'm in")) || T.Contains(TEXT("let's do it"))
		|| T.Contains(TEXT("いいよ")) || T.Contains(TEXT("行こう")) || T.Contains(TEXT("行きましょう"))
		|| T.Contains(TEXT("賛成")) || T.Contains(TEXT("いいね")) || T.Contains(TEXT("もちろん"))
		|| T.Contains(TEXT("喜んで")) || T.Contains(TEXT("行きたい")) || T.Contains(TEXT("うん"))
		|| T.Contains(TEXT("オッケー")) || T.Contains(TEXT("オーケー"));
}

bool ARealtimeTestActor::ShouldUseHandGestureFallback(const FString& AssistantText) const
{
	const FString T = AssistantText.ToLower();
	// tool_choice=autoが省略した場合の限定的な補完。説明の根拠、温かい反応、
	// 重要事項の受領が文面へ明示された場合だけ対象にする。
	return T.Contains(TEXT(" because ")) || T.StartsWith(TEXT("because "))
		|| T.Contains(TEXT("for example")) || T.Contains(TEXT("the reason"))
		|| T.Contains(TEXT("important")) || T.Contains(TEXT("valuable"))
		|| T.Contains(TEXT("wonderful to hear")) || T.Contains(TEXT("great to hear"))
		|| T.Contains(TEXT("glad to")) || T.Contains(TEXT("happy to"))
		|| T.Contains(TEXT("thank you")) || T.Contains(TEXT("you're welcome"))
		|| T.Contains(TEXT("i understand how")) || T.Contains(TEXT("i'll remember"));
}

FString ARealtimeTestActor::GetConversationLocationTagStem(EConversationLocation Location)
{
	switch (Location)
	{
	case EConversationLocation::Classroom: return TEXT("Classroom");
	case EConversationLocation::Cinema: return TEXT("Cinema");
	case EConversationLocation::JenniferRoom: return TEXT("JenniferRoom");
	case EConversationLocation::Walk: return TEXT("Walk");
	case EConversationLocation::Restaurant: return TEXT("Restaurant");
	case EConversationLocation::MyRoom: return TEXT("MyRoom");
	case EConversationLocation::Drive: return TEXT("Drive");
	default: return FString();
	}
}

FString ARealtimeTestActor::GetConversationLocationDisplayName(EConversationLocation Location)
{
	switch (Location)
	{
	case EConversationLocation::MyRoom: return TEXT("自分の部屋");
	case EConversationLocation::Classroom: return TEXT("教室");
	case EConversationLocation::Cinema: return TEXT("映画館");
	case EConversationLocation::Drive: return TEXT("ドライブ（車内）");
	case EConversationLocation::JenniferRoom: return TEXT("彼女の部屋");
	case EConversationLocation::Walk: return TEXT("散歩");
	case EConversationLocation::Restaurant: return TEXT("レストラン");
	default: return TEXT("不明");
	}
}

AActor* ARealtimeTestActor::FindConversationSceneAnchor(EConversationLocation Location, bool bPlayerAnchor) const
{
	const FString Stem = GetConversationLocationTagStem(Location);
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FName Tag(*FString::Printf(TEXT("Scene_%s_%s"), *Stem, bPlayerAnchor ? TEXT("Player") : TEXT("Jennifer")));
	TArray<AActor*> Matches;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), Tag, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

void ARealtimeTestActor::BuildFallbackConversationScenes()
{
	const FVector Base = RoomPlayerSeat ? RoomPlayerSeat->GetActorLocation() : FVector(-4970.0, -900.0, 120.0);
	BuildFallbackConversationScene(EConversationLocation::Classroom, Base + FVector(20000.0, 0.0, 0.0));
	BuildFallbackConversationScene(EConversationLocation::Cinema, Base + FVector(40000.0, 0.0, 0.0));
	BuildFallbackConversationScene(EConversationLocation::JenniferRoom, Base + FVector(60000.0, 0.0, 0.0));
	BuildFallbackConversationScene(EConversationLocation::Walk, Base + FVector(80000.0, 0.0, 0.0));
	BuildFallbackConversationScene(EConversationLocation::Restaurant, Base + FVector(100000.0, 0.0, 0.0));
}

void ARealtimeTestActor::BuildFallbackConversationScene(EConversationLocation Location, const FVector& DefaultOrigin)
{
	if (!GetWorld())
	{
		return;
	}

	const FString Stem = GetConversationLocationTagStem(Location);
	const FConversationSceneConfig SceneConfig = LoadConversationSceneConfig(Stem);
	// 【診断用】iniのBackgroundOffsetが実際に読み込めているかを確認する
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 会話シーン設定読込 Stem=%s IniPath=%s BackgroundOffset=%s"),
		*Stem, *(FPaths::ProjectConfigDir() / TEXT("ConversationScenes.ini")), *SceneConfig.BackgroundOffset.ToString());
	const FVector Origin = DefaultOrigin + SceneConfig.OriginOffset;
	const FName RuntimeSceneTag(*FString::Printf(TEXT("ConversationRuntimeScene_%s"), *Stem));
	TArray<AActor*> PreviousSceneActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), RuntimeSceneTag, PreviousSceneActors);
	for (AActor* PreviousSceneActor : PreviousSceneActors)
	{
		if (PreviousSceneActor)
		{
			PreviousSceneActor->Destroy();
		}
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BasicShapeMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 簡易シーン用Cubeを読み込めませんでした"));
		return;
	}

	FLinearColor SceneColor(0.35f, 0.38f, 0.42f, 1.0f);
	switch (Location)
	{
	case EConversationLocation::Classroom:    SceneColor = FLinearColor(0.45f, 0.34f, 0.20f); break;
	case EConversationLocation::Cinema:       SceneColor = FLinearColor(0.12f, 0.035f, 0.045f); break;
	case EConversationLocation::JenniferRoom: SceneColor = FLinearColor(0.55f, 0.22f, 0.32f); break;
	case EConversationLocation::Walk:         SceneColor = FLinearColor(0.12f, 0.34f, 0.10f); break;
	case EConversationLocation::Restaurant:   SceneColor = FLinearColor(0.42f, 0.20f, 0.08f); break;
	default: break;
	}

	auto SpawnBlock = [this, CubeMesh, BasicShapeMaterial, SceneColor, RuntimeSceneTag](const FVector& LocationValue, const FVector& ScaleValue, const FRotator& RotationValue = FRotator::ZeroRotator)
	{
		AStaticMeshActor* Block = GetWorld()->SpawnActor<AStaticMeshActor>(LocationValue, RotationValue);
		if (Block && Block->GetStaticMeshComponent())
		{
			Block->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
			if (BasicShapeMaterial)
			{
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicShapeMaterial, Block);
				Material->SetVectorParameterValue(TEXT("Color"), SceneColor);
				Block->GetStaticMeshComponent()->SetMaterial(0, Material);
			}
			Block->SetActorScale3D(ScaleValue);
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
			Block->Tags.AddUnique(TEXT("ConversationFallbackScene"));
			Block->Tags.AddUnique(RuntimeSceneTag);
		}
		return Block;
	};

	auto SpawnSphere = [this, SphereMesh, BasicShapeMaterial, SceneColor, RuntimeSceneTag](const FVector& LocationValue, const FVector& ScaleValue)
	{
		if (!SphereMesh) return static_cast<AStaticMeshActor*>(nullptr);
		AStaticMeshActor* Sphere = GetWorld()->SpawnActor<AStaticMeshActor>(LocationValue, FRotator::ZeroRotator);
		if (Sphere && Sphere->GetStaticMeshComponent())
		{
			Sphere->GetStaticMeshComponent()->SetStaticMesh(SphereMesh);
			if (BasicShapeMaterial)
			{
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicShapeMaterial, Sphere);
				Material->SetVectorParameterValue(TEXT("Color"), SceneColor * 1.35f);
				Sphere->GetStaticMeshComponent()->SetMaterial(0, Material);
			}
			Sphere->SetActorScale3D(ScaleValue);
			Sphere->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
			Sphere->Tags.AddUnique(TEXT("ConversationFallbackScene"));
			Sphere->Tags.AddUnique(RuntimeSceneTag);
		}
		return Sphere;
	};

	// Fabから追加した一体型の完成背景を、元データの縦横比を維持したまま
	// Boundsの水平中心と床面を会話エリアへ正確に合わせて配置する。
	auto SpawnFabScene = [this, RuntimeSceneTag, SceneConfig](const TCHAR* AssetPath, const FVector& FloorCenter,
		const FVector& DesiredSize, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		const FVector ConfiguredFloorCenter = FloorCenter + SceneConfig.BackgroundOffset;
		const FRotator ConfiguredRotation = Rotation + SceneConfig.BackgroundRotationOffset;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, AssetPath);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Fab背景を読み込めません: %s"), AssetPath);
			return static_cast<AStaticMeshActor*>(nullptr);
		}
		AStaticMeshActor* SceneActor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, ConfiguredRotation);
		if (!SceneActor || !SceneActor->GetStaticMeshComponent())
		{
			return static_cast<AStaticMeshActor*>(nullptr);
		}
		UStaticMeshComponent* SceneMeshComponent = SceneActor->GetStaticMeshComponent();
		// AStaticMeshActorは初期状態がStatic。Movableへ変える前のTransform更新は
		// エンジンに拒否され、Fab背景がワールド原点に残ってしまう。
		SceneMeshComponent->SetMobility(EComponentMobility::Movable);
		SceneMeshComponent->SetStaticMesh(Mesh);
		const FBoxSphereBounds LocalBounds = Mesh->GetBounds();
		const FVector SourceSize = LocalBounds.BoxExtent * 2.0f;
		const float UniformScale = FMath::Min3(
			SourceSize.X > 1.0f ? DesiredSize.X / SourceSize.X : 1.0f,
			SourceSize.Y > 1.0f ? DesiredSize.Y / SourceSize.Y : 1.0f,
			SourceSize.Z > 1.0f ? DesiredSize.Z / SourceSize.Z : 1.0f);
		const FVector EffectiveScale = SceneConfig.BackgroundScale * UniformScale;
		SceneActor->SetActorScale3D(EffectiveScale);

		// Actor原点ではなく、回転・拡大後のBounds中心をX/Yへ合わせる。
		// ZはBounds最下部をFloorCenter.Zへ合わせ、床下・天井裏から見る状態を防ぐ。
		const FVector ScaledCenter = LocalBounds.Origin * EffectiveScale;
		const FVector RotatedCenter = ConfiguredRotation.RotateVector(ScaledCenter);
		const float LocalBottomZ = (LocalBounds.Origin.Z - LocalBounds.BoxExtent.Z) * EffectiveScale.Z;
		const FVector ActorLocation(
			ConfiguredFloorCenter.X - RotatedCenter.X,
			ConfiguredFloorCenter.Y - RotatedCenter.Y,
			ConfiguredFloorCenter.Z - LocalBottomZ);
		SceneActor->SetActorLocation(ActorLocation);
		SceneMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// 露出補正では消せない、什器がJenniferの顔に落とす強い影を防ぐため、
		// 一体型Fab背景モデル自体は動的シャドウを落とさないようにする
		SceneMeshComponent->SetCastShadow(false);
		SceneActor->Tags.AddUnique(TEXT("ConversationFallbackScene"));
		SceneActor->Tags.AddUnique(RuntimeSceneTag);
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Fab背景配置 Asset=%s SourceSize=%s Scale=%.6f FloorCenter=%s RequestedLocation=%s ActualLocation=%s Mobility=%d"),
			AssetPath, *SourceSize.ToString(), UniformScale, *ConfiguredFloorCenter.ToString(), *ActorLocation.ToString(),
			*SceneActor->GetActorLocation().ToString(), static_cast<int32>(SceneMeshComponent->Mobility));

		// 【診断用】見た目が単色ブロック状になる原因がマテリアル欠落かどうかを確定するため、
		// 実際にスポーンしたコンポーネントのマテリアルスロットをそのままログに出す
		const int32 NumMaterials = SceneMeshComponent->GetNumMaterials();
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: Fabマテリアル Asset=%s スロット数=%d"), AssetPath, NumMaterials);
		for (int32 MatIndex = 0; MatIndex < NumMaterials; ++MatIndex)
		{
			UMaterialInterface* Mat = SceneMeshComponent->GetMaterial(MatIndex);
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor:   スロット[%d] = %s"),
				MatIndex, Mat ? *Mat->GetPathName() : TEXT("None(未割り当て)"));
		}
		return SceneActor;
	};

	// カメラとJenniferの間には家具を置かず、正面に必ず見える構図にする。
	const FVector PlayerLocation = Origin + SceneConfig.PlayerOffset;
	const FVector JenniferLocation = Origin + SceneConfig.JenniferOffset;
	auto EnsureMovableAnchorRoot = [](AActor* Anchor)
	{
		if (!Anchor)
		{
			return;
		}
		if (!Anchor->GetRootComponent())
		{
			USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor, TEXT("ConversationAnchorRoot"));
			Anchor->SetRootComponent(AnchorRoot);
			AnchorRoot->RegisterComponent();
		}
		Anchor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	};
	// レベルに仮アンカーが既に置かれていても、その初期位置（多くは原点）を
	// 使用しない。場所ごとのランタイム座標へ明示的に再配置する。
	AActor* PlayerAnchor = FindConversationSceneAnchor(Location, true);
	AActor* JenniferAnchor = FindConversationSceneAnchor(Location, false);
	// レベル内の背景StaticMeshActorへ誤ってアンカータグが付いている場合、
	// それを移動するとStatic mobility警告が出て背景まで動いてしまう。
	// 移動地点には必ず空の専用Actorを使う。
	if (PlayerAnchor && PlayerAnchor->FindComponentByClass<UStaticMeshComponent>())
	{
		PlayerAnchor = nullptr;
	}
	if (JenniferAnchor && JenniferAnchor->FindComponentByClass<UStaticMeshComponent>())
	{
		JenniferAnchor = nullptr;
	}
	if (!PlayerAnchor)
	{
		PlayerAnchor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), PlayerLocation, FRotator::ZeroRotator);
	}
	EnsureMovableAnchorRoot(PlayerAnchor);
	PlayerAnchor->SetActorLocationAndRotation(PlayerLocation, SceneConfig.PlayerRotation);
	PlayerAnchor->SetActorScale3D(SceneConfig.PlayerScale);
	if (!JenniferAnchor)
	{
		JenniferAnchor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), JenniferLocation, FRotator(0.0, 180.0, 0.0));
	}
	EnsureMovableAnchorRoot(JenniferAnchor);
	JenniferAnchor->SetActorLocationAndRotation(JenniferLocation, SceneConfig.JenniferRotation);
	// Jenniferの実サイズはシーンによって変えない。旧ini値は読み込み互換のため
	// 残すが、背景との比率調整にはBackgroundScaleだけを使用する。
	JenniferAnchor->SetActorScale3D(FVector::OneVector);
	if (!PlayerAnchor || !JenniferAnchor)
	{
		return;
	}
	PlayerAnchor->Tags.Add(FName(*FString::Printf(TEXT("Scene_%s_Player"), *Stem)));
	JenniferAnchor->Tags.Add(FName(*FString::Printf(TEXT("Scene_%s_Jennifer"), *Stem)));
	PlayerAnchor->Tags.AddUnique(RuntimeSceneTag);
	JenniferAnchor->Tags.AddUnique(RuntimeSceneTag);
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: %sアンカー Player=%s Jennifer=%s"),
		*GetConversationLocationDisplayName(Location),
		*PlayerAnchor->GetActorLocation().ToString(),
		*JenniferAnchor->GetActorLocation().ToString());

	const bool bUsesCompleteFabScene = Location == EConversationLocation::Classroom
		|| Location == EConversationLocation::Cinema
		|| Location == EConversationLocation::Restaurant;
	const float FloorZ = Origin.Z - 120.0f;
	// 一体型Fabモデルは展示用の片面メッシュが多いため、その内部を床として使わない。
	// どの場所でもJenniferとプレイヤーの足元には独立した床を残す。
	SpawnBlock(FVector(Origin.X, Origin.Y, FloorZ), FVector(14.0, 11.0, 0.2));

	// 各簡易セット専用の柔らかい照明。遠方の車コース用ライトに依存させない。
	// Origin基準ではなくJenniferLocation基準にすることで、iniのJenniferOffsetで
	// 彼女をFabモデルの内側まで移動させても、常に彼女のすぐそばを照らせるようにする。
	APointLight* SceneLight = GetWorld()->SpawnActor<APointLight>(JenniferLocation + FVector(-180.0, 0.0, 300.0), FRotator::ZeroRotator);
	if (SceneLight && SceneLight->PointLightComponent)
	{
		SceneLight->Tags.AddUnique(RuntimeSceneTag);
		SceneLight->Tags.AddUnique(TEXT("LifeSimJenniferScenePointLight"));
		SceneLight->PointLightComponent->SetMobility(EComponentMobility::Movable);
		float SceneLightIntensity = 5200.0f;
		switch (Location)
		{
		case EConversationLocation::Cinema:     SceneLightIntensity = 2600.0f; break;
		// 教室は棚・機材の影が顔にかかりやすいため、シャドウを打ち消せるよう明るめのフィルライトにする
		case EConversationLocation::Classroom:  SceneLightIntensity = 8600.0f; break;
		// レストランは露出オーバーで白飛びしていたため暗めにする
		case EConversationLocation::Restaurant: SceneLightIntensity = 2600.0f; break;
		default: break;
		}
		SceneLight->PointLightComponent->SetIntensity(SceneLightIntensity);
		SceneLight->PointLightComponent->SetAttenuationRadius(1400.0f);
		SceneLight->PointLightComponent->SetLightColor(Location == EConversationLocation::JenniferRoom
			? FLinearColor(1.0f, 0.72f, 0.78f) : FLinearColor(1.0f, 0.90f, 0.75f));
		SceneLight->PointLightComponent->CastShadows = false;
		SceneLight->SetActorHiddenInGame(!bDiagnosticScenePointLightsEnabled);
		SceneLight->PointLightComponent->SetVisibility(bDiagnosticScenePointLightsEnabled);
	}

	// 場所名を目の前に表示して、簡易セットでも現在地を識別できるようにする。
	ATextRenderActor* Label = GetWorld()->SpawnActor<ATextRenderActor>(Origin + FVector(500.0, 0.0, 330.0), FRotator(0.0, 180.0, 0.0));
	if (Label && Label->GetTextRender())
	{
		Label->Tags.AddUnique(RuntimeSceneTag);
		Label->GetTextRender()->SetText(FText::FromString(GetConversationLocationDisplayName(Location)));
		Label->GetTextRender()->SetWorldSize(90.0f);
		Label->GetTextRender()->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		Label->GetTextRender()->SetTextRenderColor(FColor::White);
	}

	if (Location != EConversationLocation::Walk && !bUsesCompleteFabScene)
	{
		// Fabの一体型モデルが片面・開放型でも空だけにならないよう、
		// すべての屋内シーンに確実な床・壁・天井を併設する。
		SpawnBlock(Origin + FVector(-700.0, 0.0, 230.0), FVector(0.2, 11.0, 3.5));
		SpawnBlock(Origin + FVector(650.0, 0.0, 230.0), FVector(0.2, 11.0, 3.5));
		SpawnBlock(Origin + FVector(0.0, -530.0, 230.0), FVector(7.0, 0.2, 3.5));
		SpawnBlock(Origin + FVector(0.0, 530.0, 230.0), FVector(7.0, 0.2, 3.5));
		SpawnBlock(Origin + FVector(0.0, 0.0, 575.0), FVector(14.0, 11.0, 0.2));
	}

	switch (Location)
	{
	case EConversationLocation::Classroom:
		if (!SpawnFabScene(
			TEXT("/Game/Fab/Chemestry_lab_Classroom/CHEMISTRYlabdemoscene/StaticMeshes/CHEMISTRYlabdemoscene.CHEMISTRYlabdemoscene"),
			// 一体型Fabモデルの中心へカメラを入れると、片面ポリゴンを内側から見て消える。
			// Jenniferの背後へ置き、モデル外側から室内セット全体を見せる。
			Origin + FVector(1100.0f, 0.0f, -120.0f), FVector(1800.0f, 1800.0f, 720.0f), FRotator(0.0f, 90.0f, 0.0f)))
		{
			// Fabアセットが無い場合だけ簡易黒板と机を表示する。
			SpawnBlock(Origin + FVector(610.0, 0.0, 260.0), FVector(0.18, 3.8, 1.45));
			for (const float RowX : {180.0f, 390.0f})
			{
				for (const float SideY : {-300.0f, 300.0f})
				{
					SpawnBlock(Origin + FVector(RowX, SideY, -25.0), FVector(0.75, 1.25, 0.12));
					SpawnBlock(Origin + FVector(RowX - 75.0f, SideY, -70.0), FVector(0.45, 0.55, 0.55));
				}
			}
		}
		break;
	case EConversationLocation::Cinema:
		if (!SpawnFabScene(
			TEXT("/Game/Fab/3D_Isometric_Modern_Home_Cinema_Interior/Home_Theatre/StaticMeshes/Home_Theatre.Home_Theatre"),
			Origin + FVector(1100.0f, 0.0f, -120.0f), FVector(1500.0f, 1500.0f, 900.0f), FRotator(0.0f, 90.0f, 0.0f)))
		{
			SpawnBlock(Origin + FVector(620.0, 0.0, 260.0), FVector(0.14, 4.5, 2.3));
			for (const float RowX : {170.0f, 350.0f})
			{
				for (const float SideY : {-350.0f, -220.0f, 220.0f, 350.0f})
				{
					SpawnBlock(Origin + FVector(RowX, SideY, -55.0), FVector(0.65, 0.48, 0.75));
				}
			}
		}
		break;
	case EConversationLocation::JenniferRoom:
		// Room by nightは一部がSkeletalMesh形式なので、Cigar Roomの高品質家具を
		// 基本セットとして利用し、追加パック内の静的オブジェクトも背景へ置く。
		SpawnFabScene(
			TEXT("/Game/Fab/Room_by_night/room_by_night/StaticMeshes/Circle_006_0.Circle_006_0"),
			Origin + FVector(300.0f, 260.0f, -120.0f), FVector(360.0f, 300.0f, 300.0f));
		// ベッド、枕、サイドテーブル。
		SpawnBlock(Origin + FVector(100.0, 280.0, -45.0), FVector(3.2, 2.0, 0.45));
		SpawnBlock(Origin + FVector(330.0, 280.0, 15.0), FVector(0.7, 1.6, 0.25));
		SpawnBlock(Origin + FVector(50.0, -250.0, -55.0), FVector(1.0, 1.0, 0.55));
		break;
	case EConversationLocation::Walk:
		SpawnFabScene(
			TEXT("/Game/Fab/Park_Bench/park_bench/StaticMeshes/park_bench.park_bench"),
			Origin + FVector(680.0f, 0.0f, -120.0f), FVector(1100.0f, 1100.0f, 650.0f), FRotator(0.0f, 180.0f, 0.0f));
		SpawnFabScene(
			TEXT("/Game/Fab/Park_Bench_With_Backrest/Park_Bench_L/StaticMeshes/Park_Bench_L.Park_Bench_L"),
			Origin + FVector(360.0f, 330.0f, -105.0f), FVector(240.0f, 95.0f, 115.0f), FRotator(0.0f, -90.0f, 0.0f));
		// Fab公園を主背景にし、足元の遊歩道だけを補助する。
		SpawnBlock(Origin + FVector(0.0, 0.0, -105.0), FVector(14.0, 2.8, 0.15));
		break;
	case EConversationLocation::Restaurant:
		if (!SpawnFabScene(
			TEXT("/Game/Fab/Resto_ni_Teo/resto_ni_teo/StaticMeshes/resto_ni_teo.resto_ni_teo"),
			Origin + FVector(1100.0f, 0.0f, -120.0f), FVector(2100.0f, 1900.0f, 760.0f), FRotator(0.0f, 90.0f, 0.0f)))
		{
			for (const float SideY : {-310.0f, 310.0f})
			{
				SpawnBlock(Origin + FVector(260.0, SideY, -30.0), FVector(1.15, 1.15, 0.12));
				SpawnBlock(Origin + FVector(120.0, SideY, -72.0), FVector(0.42, 0.52, 0.58));
				SpawnBlock(Origin + FVector(400.0, SideY, -72.0), FVector(0.42, 0.52, 0.58));
			}
			SpawnBlock(Origin + FVector(590.0, 0.0, 15.0), FVector(0.55, 4.0, 1.0));
		}
		break;
	default:
		break;
	}

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 簡易%sシーンを自動生成しました"), *GetConversationLocationDisplayName(Location));
}

bool ARealtimeTestActor::IsCurrentVoiceStillSpeaking() const
{
	if (RealtimeVoice && RealtimeVoice->IsConnected())
	{
		// IsAssistantSpeaking()単体だと音声チャンクの間で一瞬falseになりうるため、
		// 「まだ再生し終えていない残り秒数」もあわせて見る
		return RealtimeVoice->IsAssistantSpeaking() || RealtimeVoice->GetRemainingPlaybackSeconds() > 0.0f;
	}
	return LegacyTTS && LegacyTTS->IsPlaying();
}

void ARealtimeTestActor::CheckPendingLocationMove()
{
	const EConversationLocation Location = PendingLocationMoveTarget;
	if (Location == EConversationLocation::None)
	{
		GetWorldTimerManager().ClearTimer(PendingLocationMoveTimerHandle);
		return;
	}

	constexpr float TickIntervalSeconds = 0.15f;
	PendingLocationMoveElapsedSeconds += TickIntervalSeconds;

	// テキスト確定から音声再生が実際に始まるまでのタイムラグを見込んだ猶予時間。
	// Legacy方式ではHandleAssistantTranscriptがLegacyTTS->SpeakTextより先に呼ばれるため、
	// この猶予がないとまだ再生が始まってすらいない状態で「静か」と誤判定してしまう。
	constexpr float GracePeriodSeconds = 0.6f;
	// 「喋り終わった」と確定するために必要な連続無音時間(チャンク間の一瞬の途切れ対策)
	constexpr float RequiredQuietSeconds = 0.4f;
	// 万一ずっと再生中と判定され続けた場合の安全装置(無限待機防止)
	constexpr float MaxWaitSeconds = 20.0f;

	const bool bTimedOut = PendingLocationMoveElapsedSeconds >= MaxWaitSeconds;
	if (bTimedOut)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 発話終了待ちがタイムアウトしたため強制的に移動します Location=%s"),
			*GetConversationLocationDisplayName(Location));
		GetWorldTimerManager().ClearTimer(PendingLocationMoveTimerHandle);
		PendingLocationMoveTarget = EConversationLocation::None;
		TryMoveToConversationLocation(Location);
		return;
	}

	if (PendingLocationMoveElapsedSeconds < GracePeriodSeconds)
	{
		return;
	}

	if (IsCurrentVoiceStillSpeaking())
	{
		PendingLocationMoveQuietSeconds = 0.0f;
		return;
	}

	// transcript／Chat応答は音声再生より先に確定する。再生を一度も観測して
	// いないfalseは「終了」ではなく「まだ始まっていない」ため待機を続ける。
	if (PendingLocationMoveQuietSeconds < 0.0f)
	{
		return;
	}

	PendingLocationMoveQuietSeconds += TickIntervalSeconds;
	if (PendingLocationMoveQuietSeconds >= RequiredQuietSeconds)
	{
		GetWorldTimerManager().ClearTimer(PendingLocationMoveTimerHandle);
		PendingLocationMoveTarget = EConversationLocation::None;
		TryMoveToConversationLocation(Location);
	}
}

void ARealtimeTestActor::TryMoveToConversationLocation(EConversationLocation Location)
{
	ResetDirectLightGroupDiagnostic();
	ResetDirectionalLightDiagnostic();
	ResetMyRoomLightDiagnostic();
	// 同じ場所のキーを再度押した場合も、位置とカメラを再適用する。

	if (Location == EConversationLocation::MyRoom)
	{
		if (!RoomCharacterSeat || !RoomPlayerSeat)
		{
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 自分の部屋の移動地点が未設定です"));
			return;
		}
		if (CharacterActor)
		{
			CharacterActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			CharacterActor->SetActorEnableCollision(true);
		}
		TeleportCharacterActorTo(RoomCharacterSeat->GetActorLocation(), RoomCharacterSeat->GetActorRotation());
		TeleportPlayerPawnTo(RoomPlayerSeat->GetActorLocation(), RoomPlayerSeat->GetActorRotation());
		CaptureCanonicalConversationFraming();
		ApplyConversationSceneExposure(EConversationLocation::MyRoom);
		bIsInRoomMode = true;
		if (VehiclePawn)
		{
			VehiclePawn->SetActorHiddenInGame(true);
		}
		SetPaytonFillLightsEnabled(false);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			PC->bAutoManageActiveCameraTarget = false;
			if (IntroFaceCamera)
			{
				PC->SetViewTargetWithBlend(IntroFaceCamera, 0.35f);
			}
		}
		CurrentConversationLocation = Location;
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this, &ARealtimeTestActor::LogConversationCameraDiagnostics, Location));
		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 会話により自分の部屋へ移動しました"));
		return;
	}

	if (Location == EConversationLocation::Drive)
	{
		if (VehiclePawn)
		{
			VehiclePawn->SetActorHiddenInGame(false);
		}
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			PC->bAutoManageActiveCameraTarget = true;
		}
		if (bIsInRoomMode)
		{
			HandleToggleSceneModeKeyPressed();
		}
		else
		{
			AttachCharacterToVehicle();
			ActivateVehicleCockpitCamera();
		}
		CurrentConversationLocation = Location;
		return;
	}

	// 数値調整を再ビルドなしで反映するため、移動のたびにiniを再読込して
	// 対象場所のランタイムシーンだけを作り直す。
	const FVector Base = RoomPlayerSeat ? RoomPlayerSeat->GetActorLocation() : FVector(-4970.0, -900.0, 120.0);
	float SceneXOffset = 0.0f;
	switch (Location)
	{
	case EConversationLocation::Classroom:    SceneXOffset = 20000.0f; break;
	case EConversationLocation::Cinema:       SceneXOffset = 40000.0f; break;
	case EConversationLocation::JenniferRoom: SceneXOffset = 60000.0f; break;
	case EConversationLocation::Walk:         SceneXOffset = 80000.0f; break;
	case EConversationLocation::Restaurant:   SceneXOffset = 100000.0f; break;
	default: break;
	}
	BuildFallbackConversationScene(Location, Base + FVector(SceneXOffset, 0.0f, 0.0f));

	AActor* PlayerAnchor = FindConversationSceneAnchor(Location, true);
	AActor* JenniferAnchor = FindConversationSceneAnchor(Location, false);
	if (!PlayerAnchor || !JenniferAnchor)
	{
		const FString LocationName = GetConversationLocationDisplayName(Location);
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: %sの移動地点が未配置です。必要タグ=Scene_%s_Player / Scene_%s_Jennifer"),
			*LocationName, *GetConversationLocationTagStem(Location), *GetConversationLocationTagStem(Location));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Orange,
				FString::Printf(TEXT("%s: scene anchors are not configured yet"), *LocationName), true, FVector2D(1.25f, 1.25f));
		}
		return;
	}

	if (CharacterActor)
	{
		CharacterActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		CharacterActor->SetActorEnableCollision(true);
	}
	TeleportCharacterActorTo(JenniferAnchor->GetActorLocation(), JenniferAnchor->GetActorRotation());
	RestoreJenniferCanonicalScale(TEXT("TryMoveToConversationLocation"));
	TeleportPlayerPawnTo(PlayerAnchor->GetActorLocation(), PlayerAnchor->GetActorRotation());
	if (IntroFaceCamera && CharacterActor)
	{
		// 固定Z値ではなく、実際に描画しているMeshy顔のWorld Boundsを使う。
		// これにより、キャラクター原点やメッシュ固有オフセットが変わっても
		// カメラが胴体内部・床下へ入らず、顔と同じ高さから正面を映せる。
		const FVector FaceTarget = ResolveJenniferFaceTarget();
		const float CanonicalDistance = bHasCanonicalConversationFraming
			? CanonicalConversationCameraDistanceCm : 270.0f;

		const FConversationSceneConfig SceneConfig = LoadConversationSceneConfig(GetConversationLocationTagStem(Location));
		FVector CameraLocation;
		FRotator CameraRotation;
		if (SceneConfig.bHasCameraOffset)
		{
			const float ConfiguredDistance = SceneConfig.CameraOffset.Size();
			UE_LOG(LogTemp, Warning,
				TEXT("[SCENE_CAMERA][BEFORE_CONFIG] scene=%s actor_scale=%s configured_distance=%.2fcm configured_fov=%.2f offset=%s"),
				*GetConversationLocationTagStem(Location), *CharacterActor->GetActorScale3D().ToString(),
				ConfiguredDistance, SceneConfig.CameraFOV, *SceneConfig.CameraOffset.ToString());

			// 各シーンのZは上下構図として残すが、顔までの実距離とFOVは共通化する。
			const float VerticalOffset = FMath::Clamp(SceneConfig.CameraOffset.Z,
				-CanonicalDistance * 0.8f, CanonicalDistance * 0.8f);
			FVector HorizontalDirection(SceneConfig.CameraOffset.X, SceneConfig.CameraOffset.Y, 0.0f);
			if (!HorizontalDirection.Normalize())
			{
				HorizontalDirection = -CharacterActor->GetActorForwardVector();
				HorizontalDirection.Z = 0.0f;
				HorizontalDirection.Normalize();
			}
			const float HorizontalDistance = FMath::Sqrt(FMath::Max(0.0f,
				FMath::Square(CanonicalDistance) - FMath::Square(VerticalOffset)));
			CameraLocation = FaceTarget + HorizontalDirection * HorizontalDistance
				+ FVector::UpVector * VerticalOffset;
			CameraRotation = SceneConfig.bHasCameraRotation
				? SceneConfig.CameraRotation
				: (FaceTarget - CameraLocation).Rotation();
		}
		else
		{
			FVector ViewDirection = PlayerAnchor->GetActorLocation() - FaceTarget;
			ViewDirection.Z = 0.0f;
			if (!ViewDirection.Normalize())
			{
				ViewDirection = -CharacterActor->GetActorForwardVector();
				ViewDirection.Z = 0.0f;
				ViewDirection.Normalize();
			}
			CameraLocation = FaceTarget + ViewDirection * CanonicalDistance;
			CameraRotation = (FaceTarget - CameraLocation).Rotation();
		}
		TeleportPlayerPawnTo(CameraLocation, CameraRotation);
		if (UCameraComponent* LocationCameraComponent = IntroFaceCamera->GetCameraComponent())
		{
			LocationCameraComponent->SetFieldOfView(CanonicalConversationCameraFOVDegrees);
		}
		ApplyConversationSceneExposure(Location);
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 場所カメラ FaceTarget=%s Camera=%s"),
			*FaceTarget.ToString(), *CameraLocation.ToString());
		UE_LOG(LogTemp, Warning,
			TEXT("[SCENE_CAMERA][AFTER_COMMON] scene=%s actor_scale=%s camera_distance=%.2fcm fov=%.2f"),
			*GetConversationLocationTagStem(Location), *CharacterActor->GetActorScale3D().ToString(),
			FVector::Distance(FaceTarget, CameraLocation), CanonicalConversationCameraFOVDegrees);
	}
	if (VehiclePawn)
	{
		// 車両は所持したまま残すが、教室などへ車内メッシュが混入しないよう描画だけ止める。
		VehiclePawn->SetActorHiddenInGame(true);
		TArray<UCameraComponent*> VehicleCameras;
		VehiclePawn->GetComponents<UCameraComponent>(VehicleCameras);
		for (UCameraComponent* VehicleCamera : VehicleCameras)
		{
			if (VehicleCamera)
			{
				VehicleCamera->SetActive(false);
			}
		}
	}
	// PlayerPawnは車両なので、視点には使わない。移動先へ運んだ専用カメラを使用する。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (IntroFaceCamera)
		{
			PC->SetViewTargetWithBlend(IntroFaceCamera, 0.0f);
		}
	}
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
		this, &ARealtimeTestActor::LogConversationCameraDiagnostics, Location));
	bIsInRoomMode = true;
	CurrentConversationLocation = Location;
	if (Location == EConversationLocation::Classroom)
	{
		FTimerHandle ClassroomLightingRefreshTimer;
		GetWorldTimerManager().SetTimer(
			ClassroomLightingRefreshTimer,
			this,
			&ARealtimeTestActor::RefreshClassroomJenniferLightingAfterMove,
			0.25f,
			false);
	}
	SetPaytonFillLightsEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 会話により%sへ移動しました"),
		*GetConversationLocationDisplayName(Location));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			FString::Printf(TEXT("Moved to: %s"), *GetConversationLocationDisplayName(Location)), true, FVector2D(1.75f, 1.75f));
	}
}

void ARealtimeTestActor::HandleUserStartedSpeaking()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: (interrupted)"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("(you started talking)"), true, FVector2D(1.25f, 1.25f));
	}
}
