#include "WhisperTranscriberComponent.h"
#include "MicRecorderComponent.h"
#include "Json.h"
#include "Misc/Guid.h"

UWhisperTranscriberComponent::UWhisperTranscriberComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Boundary = TEXT("----UE5WhisperBoundary") + FGuid::NewGuid().ToString();
}

void UWhisperTranscriberComponent::TranscribeFromMicRecorder(UMicRecorderComponent* MicRecorder)
{
	SttTotalStartTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY][STT] audio_prepare_start"));
	if (!MicRecorder)
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: MicRecorderComponentが指定されていません"));
		OnTranscriptionFailed.Broadcast(TEXT("MicRecorderComponentが指定されていません"));
		return;
	}

	TArray<uint8> WavBytes;
	if (!MicRecorder->BuildWavBytes(WavBytes))
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: 録音データをWAV化できませんでした"));
		OnTranscriptionFailed.Broadcast(TEXT("録音データをWAV化できませんでした(録音データが空の可能性があります)"));
		return;
	}
	UE_LOG(LogTemp, Log,
		TEXT("[LATENCY][LEGACY][STT] audio_prepare_done elapsed=%.3f sec wav_bytes=%d"),
		FPlatformTime::Seconds() - SttTotalStartTimeSeconds, WavBytes.Num());

	SendWavBytesToWhisper(WavBytes, false);
}

void UWhisperTranscriberComponent::SendWavBytesToWhisper(const TArray<uint8>& WavBytes, bool bIsRetry)
{
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: ApiKeyが設定されていません"));
		OnTranscriptionFailed.Broadcast(TEXT("ApiKeyが設定されていません"));
		return;
	}

	LastRequestedWavBytes = WavBytes;
	bLastRequestWasRetry = bIsRetry;
	// multipart/form-dataのボディを手動で構築する(UE5のHTTPモジュールに専用APIがないため)
	TArray<uint8> RequestBody;

	auto AppendText = [&RequestBody](const FString& Text)
	{
		FTCHARToUTF8 Converter(*Text);
		RequestBody.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	};

	// --- modelパート ---
	AppendText(FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendText(TEXT("Content-Disposition: form-data; name=\"model\"\r\n\r\n"));
	AppendText(Model + TEXT("\r\n"));

	// --- languageパート(指定がある場合のみ) ---
	if (!Language.IsEmpty())
	{
		AppendText(FString::Printf(TEXT("--%s\r\n"), *Boundary));
		AppendText(TEXT("Content-Disposition: form-data; name=\"language\"\r\n\r\n"));
		AppendText(Language + TEXT("\r\n"));
	}

	// --- fileパート(WAVバイナリ本体) ---
	AppendText(FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendText(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"));
	AppendText(TEXT("Content-Type: audio/wav\r\n\r\n"));
	RequestBody.Append(WavBytes);
	AppendText(TEXT("\r\n"));

	// --- 終端境界 ---
	AppendText(FString::Printf(TEXT("--%s--\r\n"), *Boundary));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(TEXT("https://api.openai.com/v1/audio/transcriptions"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	HttpRequest->SetContent(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UWhisperTranscriberComponent::OnWhisperResponseReceived);
	SttRequestStartTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] stt_start"));
	UE_LOG(LogTemp, Log,
		TEXT("[LATENCY][LEGACY][STT] request_start model=%s retry=%s request_bytes=%d"),
		*Model, bIsRetry ? TEXT("true") : TEXT("false"), RequestBody.Num());
	HttpRequest->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("WhisperTranscriber: Whisper APIへ送信しました(%d bytes)"), RequestBody.Num());
}

void UWhisperTranscriberComponent::OnWhisperResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	const double ResponseReceivedTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY][STT] response_done model=%s elapsed=%.3f sec"),
		*Model,
		SttRequestStartTimeSeconds > 0.0
			? ResponseReceivedTimeSeconds - SttRequestStartTimeSeconds
			: 0.0);
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: HTTPリクエストに失敗しました(ネットワークエラー)"));
		OnTranscriptionFailed.Broadcast(TEXT("HTTPリクエストに失敗しました(ネットワークエラー)"));
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	const FString ResponseBody = Response->GetContentAsString();

	if (StatusCode != 200)
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: APIエラー(Status=%d): %s"), StatusCode, *ResponseBody);

		// レート制限(429)で、まだリトライしていなければ、少し待ってから1回だけ再送する
		if (StatusCode == 429 && !bLastRequestWasRetry)
		{
			UE_LOG(LogTemp, Warning, TEXT("WhisperTranscriber: レート制限のため2秒後に再試行します"));

			if (UWorld* World = GetWorld())
			{
				TArray<uint8> BytesToRetry = LastRequestedWavBytes;
				World->GetTimerManager().SetTimer(
					RetryTimerHandle,
					[this, BytesToRetry]()
					{
						SendWavBytesToWhisper(BytesToRetry, true);
					},
					2.0f,
					false
				);
				return;
			}
		}

		OnTranscriptionFailed.Broadcast(FString::Printf(TEXT("Whisper APIエラー(Status=%d)"), StatusCode));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: レスポンスのJSON解析に失敗しました: %s"), *ResponseBody);
		OnTranscriptionFailed.Broadcast(TEXT("レスポンスのJSON解析に失敗しました"));
		return;
	}

	FString TranscribedText;
	if (!JsonObject->TryGetStringField(TEXT("text"), TranscribedText))
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: レスポンスに'text'フィールドが見つかりません: %s"), *ResponseBody);
		OnTranscriptionFailed.Broadcast(TEXT("レスポンスに'text'フィールドが見つかりませんでした"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY][STT] parse_done elapsed=%.3f sec"),
		FPlatformTime::Seconds() - ResponseReceivedTimeSeconds);
	const double SttElapsedSeconds = SttTotalStartTimeSeconds > 0.0
		? FPlatformTime::Seconds() - SttTotalStartTimeSeconds
		: 0.0;
	UE_LOG(LogTemp, Log, TEXT("[LATENCY][LEGACY] stt_done elapsed=%.3f sec"), SttElapsedSeconds);
	UE_LOG(LogTemp, Log, TEXT("[STT][LEGACY][RESULT] model=%s text=\"%s\""), *Model, *TranscribedText);
	UE_LOG(LogTemp, Log, TEXT("WhisperTranscriber: 文字起こし成功: %s"), *TranscribedText);
	OnTranscriptionComplete.Broadcast(TranscribedText);
}
