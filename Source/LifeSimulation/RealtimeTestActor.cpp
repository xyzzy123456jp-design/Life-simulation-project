#include "RealtimeTestActor.h"
#include "RealtimeVoiceComponent.h"
#include "LipSyncComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
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

namespace
{
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
}

void ARealtimeTestActor::BeginPlay()
{
	// Super::BeginPlay()は子コンポーネント(LipSync含む)のBeginPlayを内部で呼び出すため、
	// LipSyncがCharacterActor/VoiceSourceを参照する前に、必ずここで先に設定しておく
	LipSync->CharacterActor = CharacterActor;
	LipSync->VoiceSource = RealtimeVoice;

	Super::BeginPlay();

	RealtimeVoice->ApiKey = ApiKey;
	RealtimeVoice->OnConnected.AddDynamic(this, &ARealtimeTestActor::HandleConnected);
	RealtimeVoice->OnDisconnected.AddDynamic(this, &ARealtimeTestActor::HandleDisconnected);
	RealtimeVoice->OnError.AddDynamic(this, &ARealtimeTestActor::HandleError);
	RealtimeVoice->OnUserTranscript.AddDynamic(this, &ARealtimeTestActor::HandleUserTranscript);
	RealtimeVoice->OnAssistantTranscript.AddDynamic(this, &ARealtimeTestActor::HandleAssistantTranscript);
	RealtimeVoice->OnUserStartedSpeaking.AddDynamic(this, &ARealtimeTestActor::HandleUserStartedSpeaking);

	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 接続を開始します"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Silver, TEXT("Connecting to Realtime API..."));
	}

	RealtimeVoice->Connect();

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

	if (InputComponent)
	{
		// P/Mキーは車のEnhanced Input(ハンドブレーキ等)と衝突していたため変更。
		// F1/F2はVRソフト(SteamVR/Pimaxクライアント等)に横取りされるため、
		// 普通の文字キー(Y/H)に変更する
		InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ARealtimeTestActor::HandleInterruptKeyPressed);
		InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ARealtimeTestActor::HandleToggleSceneModeKeyPressed);

		// 【Xboxコントローラー】Yボタンで部屋⇔車切り替え、Bボタンで会話割り込み
		InputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &ARealtimeTestActor::HandleToggleSceneModeKeyPressed);
		InputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &ARealtimeTestActor::HandleInterruptKeyPressed);

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

		UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: Pキー・Mキーのバインドを実行しました"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: InputComponentが取得できず、キーのバインドに失敗しました"));
	}
}

void ARealtimeTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 毎フレーム、キャラクターをカメラの方に向かせ続ける(部屋モードの時のみ。
	// 車モードでは助手席にアタッチ済みなので、相対姿勢を崩さないよう回転させない)
	if (bIsInRoomMode && CharacterActor && IntroFaceCamera)
	{
		FVector ToCamera = IntroFaceCamera->GetActorLocation() - CharacterActor->GetActorLocation();
		FRotator FacingRotation = ToCamera.Rotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += FacingYawOffset; // メッシュの前方向オフセット補正
		CharacterActor->SetActorRotation(FacingRotation);
	}

	// 【デバッグ用】Paytonの実際の座標を画面に常時表示(テレポート後に位置がズレていないか確認するため)
	if (GEngine && CharacterActor)
	{
		GEngine->AddOnScreenDebugMessage(9001, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("Payton pos: %s"), *CharacterActor->GetActorLocation().ToString()));
		GEngine->AddOnScreenDebugMessage(9013, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("Payton rot: %s"), *CharacterActor->GetActorRotation().ToString()));
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
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: [INPUT] Pキーで割り込みました"));

	RealtimeVoice->Interrupt();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("(interrupted)"));
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
			FString::Printf(TEXT("FrontCamera relative location: %s"), *DesiredFrontCameraOffset.ToString()));
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
			FString::Printf(TEXT("VR eye height offset: %.1f"), VehicleCockpitVREyeHeightOffsetCm));
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
			GEngine->AddOnScreenDebugMessage(9010, 2.f, FColor::Cyan, TEXT("VR center reset"));
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

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("(Room mode)"));
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

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("(Cockpit mode)"));
		}
	}
}

void ARealtimeTestActor::HandleConnected()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: 接続成功。話しかけてみてください"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("Connected! Just start talking."));
	}
}

void ARealtimeTestActor::HandleDisconnected(const FString& Reason)
{
	UE_LOG(LogTemp, Warning, TEXT("RealtimeTestActor: 切断されました(%s)"), *Reason);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Disconnected: %s"), *Reason));
	}
}

void ARealtimeTestActor::HandleError(const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("RealtimeTestActor: エラー(%s)"), *ErrorMessage);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, FString::Printf(TEXT("Error: %s"), *ErrorMessage));
	}
}

void ARealtimeTestActor::HandleUserTranscript(const FString& Text)
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: You: %s"), *Text);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan, FString::Printf(TEXT("You: %s"), *Text));
	}
}

void ARealtimeTestActor::HandleAssistantTranscript(const FString& Text)
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: AI: %s"), *Text);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Yellow, FString::Printf(TEXT("AI: %s"), *Text));
	}
}

void ARealtimeTestActor::HandleUserStartedSpeaking()
{
	UE_LOG(LogTemp, Log, TEXT("RealtimeTestActor: (interrupted)"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Orange, TEXT("(you started talking)"));
	}
}
