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
#include "Components/PrimitiveComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "IXRTrackingSystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	// Blueprintと全コンポーネントのBeginPlay後に表示用顔を構築する。以前はSuperより
	// 前だったため、初回ロード時だけSkeletalMeshの初期化に表示状態を上書きされていた。
	SetupPaytonNewFace();

	// 非ドライブ地点用カメラは、レベル上の参照や親子関係を引き継がない独立Actorとして
	// 毎回生成する。これで車両Blueprintに視点を奪われないようにする。
	if (GetWorld())
	{
		const FTransform InitialCameraTransform = IntroFaceCamera
			? IntroFaceCamera->GetActorTransform()
			: FTransform(GetActorRotation(), GetActorLocation());
		IntroFaceCamera = GetWorld()->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), InitialCameraTransform);
		if (IntroFaceCamera)
		{
			IntroFaceCamera->SetActorLabel(TEXT("LifeSimConversationCamera"));
			UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 会話地点用カメラをC++で生成しました"));
		}
	}

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
					TEXT("/Game/Meshy_Crimson_Gaze_Jaw/SK_Crimson_Gaze_Jaw.SK_Crimson_Gaze_Jaw"));
				UStaticMesh* CrimsonGazeMesh = CrimsonGazeJawMesh ? nullptr : LoadObject<UStaticMesh>(nullptr,
					TEXT("/Game/Meshy_Crimson_Gaze/Meshy_AI_Crimson_Gaze_0816095605_texture.Meshy_AI_Crimson_Gaze_0816095605_texture"));
				UPrimitiveComponent* CrimsonPreviewComponent = nullptr;
				if (CrimsonGazeJawMesh)
				{
					CrimsonGazeMorphComponent = NewObject<USkeletalMeshComponent>(CharacterActor, TEXT("CrimsonGazeMorphComponent"));
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
	JenniferAnchor->SetActorScale3D(SceneConfig.JenniferScale);
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
	if (CharacterActor)
	{
		// TeleportCharacterActorToは位置・回転のみ反映するため、ini側のJenniferScaleを
		// 実際のキャラクターにも適用する(JenniferAnchor自体には既に反映済み)
		CharacterActor->SetActorScale3D(JenniferAnchor->GetActorScale3D());
	}
	TeleportPlayerPawnTo(PlayerAnchor->GetActorLocation(), PlayerAnchor->GetActorRotation());
	if (IntroFaceCamera && CharacterActor)
	{
		// 固定Z値ではなく、実際に描画しているMeshy顔のWorld Boundsを使う。
		// これにより、キャラクター原点やメッシュ固有オフセットが変わっても
		// カメラが胴体内部・床下へ入らず、顔と同じ高さから正面を映せる。
		FVector FaceTarget = CharacterActor->GetActorLocation() + FVector(0.0f, 0.0f, 160.0f);
		if (OriginalPaytonMorphMesh && OriginalPaytonMorphMesh->IsRegistered())
		{
			OriginalPaytonMorphMesh->UpdateBounds();
			FaceTarget = OriginalPaytonMorphMesh->Bounds.Origin;
		}
		else
		{
			const FBox CharacterBounds = CharacterActor->GetComponentsBoundingBox(true);
			if (CharacterBounds.IsValid)
			{
				FaceTarget = FVector(
					CharacterBounds.GetCenter().X,
					CharacterBounds.GetCenter().Y,
					CharacterBounds.Max.Z - FMath::Clamp(CharacterBounds.GetExtent().Z * 0.12f, 10.0f, 30.0f));
			}
		}

		const FConversationSceneConfig SceneConfig = LoadConversationSceneConfig(GetConversationLocationTagStem(Location));
		FVector CameraLocation;
		FRotator CameraRotation;
		if (SceneConfig.bHasCameraOffset)
		{
			CameraLocation = FaceTarget + SceneConfig.CameraOffset;
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
			CameraLocation = FaceTarget + ViewDirection * 190.0f;
			CameraRotation = (FaceTarget - CameraLocation).Rotation();
		}
		TeleportPlayerPawnTo(CameraLocation, CameraRotation);
		if (UCameraComponent* LocationCameraComponent = IntroFaceCamera->GetCameraComponent())
		{
			LocationCameraComponent->SetFieldOfView(SceneConfig.CameraFOV);
			// 自動露出がライトの強弱を打ち消してしまうため、場所ごとの明るさはここで直接指定する
			LocationCameraComponent->PostProcessSettings.bOverride_AutoExposureBias = SceneConfig.bHasExposureBias;
			if (SceneConfig.bHasExposureBias)
			{
				LocationCameraComponent->PostProcessSettings.AutoExposureBias = SceneConfig.ExposureBias;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 場所カメラ FaceTarget=%s Camera=%s"),
			*FaceTarget.ToString(), *CameraLocation.ToString());
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
	bIsInRoomMode = true;
	CurrentConversationLocation = Location;
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
