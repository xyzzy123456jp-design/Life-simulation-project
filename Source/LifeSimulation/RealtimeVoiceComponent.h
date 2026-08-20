#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "RealtimeVoiceComponent.generated.h"

class IWebSocket;
class USoundWaveProcedural;
class UAudioComponent;

enum class EExpressionApplyResult : uint8
{
	Applied,
	UnknownEmotion,
	SubsystemUnavailable,
	TargetComponentUnavailable,
	InvalidWorld
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRealtimeConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealtimeDisconnected, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealtimeError, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealtimeUserTranscript, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealtimeAssistantTranscript, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRealtimeUserStartedSpeaking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRealtimeAssistantStartedSpeaking);

/**
 * OpenAI Realtime API(gpt-realtime系)とWebSocketで常時接続し、
 * マイク音声をリアルタイムにストリーミング送信、AIの音声応答を
 * リアルタイムに受信・再生する。Whisper/ChatManager/TTSComponentの
 * 3つを1つに置き換える、音声対音声の会話コンポーネント。
 *
 * サーバー側VAD(音声区間検出)を使うため、無音判定やWhisper送信タイミングの
 * 手動制御は不要になる。ユーザーが話し始めると自動でAIの発話が中断される。
 *
 * 【エコー対策】AIが喋っている間(bIsAssistantSpeaking=true)は、
 * マイクからの音声送信を止める(ミュート)。スピーカー再生音をマイクが拾って
 * サーバー側VADが誤って「ユーザーが話し始めた」と判定してしまうのを防ぐ。
 * ヘッドホン使用時は本来不要な対策だが、スピーカー環境でも安定させるために入れている。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API URealtimeVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URealtimeVoiceComponent();

	// OpenAIのAPIキー
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	FString ApiKey;

	// 使用するRealtimeモデル
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	FString Model = TEXT("gpt-realtime-2.1");

	// 声(marin, cedar, alloy, ash, ballad, coral, echo, sage, shimmer, verse など)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	FString Voice = TEXT("coral");

	// AIキャラクターの人格設定(旧ChatManagerのsystemメッセージに相当)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	FString Instructions = TEXT("You are a friendly AI character having a natural voice conversation in VR, helping the user practice English. Reply naturally in English, the way a friendly conversation partner would - usually 1-3 sentences, spoken style.");

	// AI発話中にマイク送信をミュートするか(エコー誤検知対策)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	bool bMuteMicWhileAssistantSpeaking = true;

	// AIの声の再生音量の倍率(声が小さいと感じる場合はここを上げる。1.0が等倍)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Realtime")
	float PlaybackVolumeMultiplier = 2.0f;

	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeConnected OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeDisconnected OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeError OnError;

	// ユーザーの発言が文字起こしされた時
	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeUserTranscript OnUserTranscript;

	// AIの発言テキスト(音声と同時に生成される)
	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeAssistantTranscript OnAssistantTranscript;

	// ユーザーが話し始めたことをサーバーが検知した時(AIの発話は自動的に中断される)
	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeUserStartedSpeaking OnUserStartedSpeaking;

	// AIが話し始めた時
	UPROPERTY(BlueprintAssignable, Category = "Realtime")
	FOnRealtimeAssistantStartedSpeaking OnAssistantStartedSpeaking;

	// 接続を開始する(マイクキャプチャも同時に開始する)
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	void Connect();

	// 接続を切断する
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category = "Realtime")
	bool IsConnected() const { return bIsConnected; }

	// Create a user input_text item and start a new response. This initial
	// response.create is intentionally independent from the post-tool-call one.
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	bool SendTextMessage(const FString& Text);

	// AIが現在喋っているか(リップシンクに使う)
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	bool IsAssistantSpeaking() const;

	// 手動でAIの発話を割り込んで止める(Pキー等から呼ぶ想定)。
	// サーバーに現在のレスポンス生成の中断(response.cancel)を送り、
	// ローカルの再生とマイクミュートも即座に解除する。
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	void Interrupt();

	// 現在の再生音量(リップシンクに使う。0.0〜1.0)
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	float GetCurrentAmplitude() const { return CurrentAmplitude; }

	// デバッグ用: 実際の再生残り時間の推定値(秒)。0なら「再生完了と判定した」ことを意味する
	UFUNCTION(BlueprintCallable, Category = "Realtime")
	float GetRemainingPlaybackSeconds() const { return RemainingPlaybackSeconds; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// --- WebSocket ---
	TSharedPtr<IWebSocket> WebSocket;
	void HandleWebSocketConnected();
	void HandleWebSocketConnectionError(const FString& Error);
	void HandleWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleWebSocketMessage(const FString& Message);
	void SendJson(const class TSharedRef<class FJsonObject>& JsonObject);
	void SendSessionUpdate();

	// --- Realtime Function Calling / facial expression ---
	EExpressionApplyResult ApplyExpression(const FString& Emotion, float Intensity);
	void HandleFunctionCallArgumentsDone(const TSharedPtr<class FJsonObject>& JsonObject);
	void HandleResponseDoneForFunctionCalls(const TSharedPtr<class FJsonObject>& JsonObject);
	void SendFunctionCallOutput(const FString& CallId, const FString& OutputJson);
	void SendResponseCreate();
	void MarkFunctionCallCompleted(const FString& ResponseId, const FString& CompletionKey);
	void TryFinalizeFunctionResponse(const FString& ResponseId);
	void CleanupFunctionResponse(const FString& ResponseId);
	void ClearPendingFunctionCallState();
	static FString MakeFunctionCallCompletionKey(const FString& CallId, const FString& ItemId, const FString& ResponseId, int32 OutputIndex);

	struct FPendingFunctionResponse
	{
		TSet<FString> ExpectedCallKeys;
		TSet<FString> CompletedCallKeys;
		bool bResponseDoneReceived = false;
		bool bResponseCreateSent = false;
	};

	TMap<FString, FPendingFunctionResponse> PendingFunctionResponses;
	TSet<FString> GloballyCompletedFunctionCallKeys;
	// Only the exact, case-sensitive user prefix "Expression test:" arms this
	// flag. It classifies the next express_emotion call without changing the
	// existing expression application path.
	bool bNextExpressionCallIsAiTest = false;
	int32 PendingTextExpressionTestRequests = 0;

	// --- マイクキャプチャ(24kHzへダウンサンプリングしつつ送信) ---
	Audio::FAudioCapture AudioCapture;
	bool StartMicCapture();
	void StopMicCapture();
	void OnMicAudioCapture(const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow);
	int32 CaptureSampleRate = 0;
	int32 CaptureNumChannels = 0;
	double ResamplePhase = 0.0; // 24kHzへの連続的なダウンサンプリング用の位相

	// --- AIの音声再生(受信したPCMを継続的にキューイングして再生し続ける) ---
	UPROPERTY()
	USoundWaveProcedural* PlaybackSoundWave;

	UPROPERTY()
	UAudioComponent* PlaybackAudioComponent;

	void EnsurePlaybackReady();
	void QueuePlaybackAudio(const TArray<uint8>& Pcm16Bytes);
	void StopPlaybackImmediately();

	bool bIsConnected = false;
	float CurrentAmplitude = 0.0f;

	// AIが現在喋っているか(response.created〜response.output_audio.doneの間。
	// 最初の音声チャンクが届く前からミュートを開始するために使う)
	bool bIsAssistantSpeaking = false;

	// 割り込み(手動Interrupt or ユーザー発話検知によるバージイン)の直後、
	// キャンセルがサーバーに届くまでの間に飛んでくる残留音声チャンクを
	// 再生してしまわないよう、次のresponse.createdが来るまで音声を破棄するフラグ
	bool bDiscardIncomingAudioDeltas = false;

	// キューイング済み音声のうち、まだスピーカーで再生し終わっていないと推定される残り秒数。
	// response.output_audio.doneは「サーバーが送信を終えた」タイミングであって
	// 「スピーカーの再生が終わった」タイミングではないため、これを別途自前で計算して
	// 実際の再生完了までマイクのミュートを延長する(エコー対策の本体)。
	float RemainingPlaybackSeconds = 0.0f;

	// 音声チャンクの(再生時間, RMS音量)を時間軸順に保持するキュー。
	// ネットワークはリアルタイムより速くまとめてチャンクを送ってくることがあるため、
	// 「チャンクが届いた瞬間」の音量をそのままCurrentAmplitudeにしてしまうと、
	// 実際にスピーカーで鳴っている音とズレる(リップシンクが実際の音声より
	// 早送りで進んでしまう)。そのため、実際の再生タイミングに合わせて
	// TickComponentで順番に消費し、CurrentAmplitudeへ反映する。
	TArray<TPair<float, float>> AmplitudeEnvelopeQueue;

	// オーディオ出力デバイス側の遅延を見込んだ安全マージン(秒)。
	// 新しい発話の最初のチャンクをキューする時にのみ一度だけ加算する。
	UPROPERTY(EditAnywhere, Category = "Realtime")
	float MicUnmuteSafetyMarginSeconds = 0.15f;

	// Realtime APIの音声フォーマットは24kHz固定(pcm16)
	static constexpr int32 RealtimeSampleRate = 24000;
};
