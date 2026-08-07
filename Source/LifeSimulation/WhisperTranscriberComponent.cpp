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

	SendWavBytesToWhisper(WavBytes);
}

void UWhisperTranscriberComponent::SendWavBytesToWhisper(const TArray<uint8>& WavBytes)
{
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("WhisperTranscriber: ApiKeyが設定されていません"));
		OnTranscriptionFailed.Broadcast(TEXT("ApiKeyが設定されていません"));
		return;
	}

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
	AppendText(TEXT("whisper-1\r\n"));

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
	HttpRequest->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("WhisperTranscriber: Whisper APIへ送信しました(%d bytes)"), RequestBody.Num());
}

void UWhisperTranscriberComponent::OnWhisperResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
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

	UE_LOG(LogTemp, Log, TEXT("WhisperTranscriber: 文字起こし成功: %s"), *TranscribedText);
	OnTranscriptionComplete.Broadcast(TranscribedText);
}
