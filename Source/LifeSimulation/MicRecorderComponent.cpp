#include "MicRecorderComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

UMicRecorderComponent::UMicRecorderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMicRecorderComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMicRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsRecording)
	{
		StopRecording();
	}
	Super::EndPlay(EndPlayReason);
}

bool UMicRecorderComponent::StartRecording()
{
	if (bIsRecording)
	{
		UE_LOG(LogTemp, Warning, TEXT("MicRecorder: 既に録音中です"));
		return false;
	}

	{
		FScopeLock Lock(&RecordedSamplesLock);
		RecordedSamples.Empty();
	}

	Audio::FAudioCaptureDeviceParams Params = Audio::FAudioCaptureDeviceParams();

	// キャプチャコールバック(オーディオスレッドから呼ばれる)
	Audio::FOnAudioCaptureFunction OnCapture =
		[this](const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
	{
		OnAudioCapture(AudioData, NumFrames, InNumChannels, InSampleRate, StreamTime, bOverflow);
	};

	if (!AudioCapture.OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
	{
		UE_LOG(LogTemp, Error, TEXT("MicRecorder: キャプチャストリームのオープンに失敗しました"));
		return false;
	}

	if (!AudioCapture.StartStream())
	{
		UE_LOG(LogTemp, Error, TEXT("MicRecorder: キャプチャストリームの開始に失敗しました"));
		return false;
	}

	bIsRecording = true;
	UE_LOG(LogTemp, Log, TEXT("MicRecorder: 録音を開始しました"));
	return true;
}

void UMicRecorderComponent::StopRecording()
{
	if (!bIsRecording)
	{
		return;
	}

	AudioCapture.StopStream();
	AudioCapture.CloseStream();
	bIsRecording = false;

	FScopeLock Lock(&RecordedSamplesLock);
	UE_LOG(LogTemp, Log, TEXT("MicRecorder: 録音を停止しました。サンプル数=%d, SampleRate=%d, Channels=%d"),
		RecordedSamples.Num(), SampleRate, NumChannels);
}

void UMicRecorderComponent::OnAudioCapture(const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
{
	// オーディオスレッドから呼ばれるためロックして共有バッファに追記する
	FScopeLock Lock(&RecordedSamplesLock);

	SampleRate = InSampleRate;
	NumChannels = InNumChannels;

	const float* FloatAudioData = static_cast<const float*>(AudioData);
	const int32 NumSamples = NumFrames * InNumChannels;
	RecordedSamples.Append(FloatAudioData, NumSamples);
}

bool UMicRecorderComponent::SaveRecordedAudioToWav(const FString& FilePath)
{
	TArray<uint8> WavBytes;
	if (!BuildWavBytes(WavBytes))
	{
		return false;
	}

	return FFileHelper::SaveArrayToFile(WavBytes, *FilePath);
}

bool UMicRecorderComponent::HasSignificantAudio(float Threshold)
{
	FScopeLock Lock(&RecordedSamplesLock);

	if (RecordedSamples.Num() == 0)
	{
		return false;
	}

	// RMS(二乗平均平方根)で音量レベルを見積もる
	double SumOfSquares = 0.0;
	for (float Sample : RecordedSamples)
	{
		SumOfSquares += static_cast<double>(Sample) * static_cast<double>(Sample);
	}
	const double Rms = FMath::Sqrt(SumOfSquares / RecordedSamples.Num());

	UE_LOG(LogTemp, Log, TEXT("MicRecorder: 録音のRMS音量=%.4f (閾値=%.4f)"), Rms, Threshold);

	return Rms >= Threshold;
}

float UMicRecorderComponent::GetRecentRms(float WindowSeconds)
{
	FScopeLock Lock(&RecordedSamplesLock);

	if (RecordedSamples.Num() == 0 || SampleRate <= 0)
	{
		return 0.0f;
	}

	const int32 ChannelCount = FMath::Max(1, NumChannels);
	const int32 WindowSamples = FMath::Clamp(
		FMath::RoundToInt(WindowSeconds * SampleRate * ChannelCount),
		1,
		RecordedSamples.Num()
	);
	const int32 StartIndex = RecordedSamples.Num() - WindowSamples;

	double SumOfSquares = 0.0;
	for (int32 i = StartIndex; i < RecordedSamples.Num(); ++i)
	{
		SumOfSquares += static_cast<double>(RecordedSamples[i]) * static_cast<double>(RecordedSamples[i]);
	}

	return static_cast<float>(FMath::Sqrt(SumOfSquares / WindowSamples));
}

bool UMicRecorderComponent::BuildWavBytes(TArray<uint8>& OutWavBytes)
{
	FScopeLock Lock(&RecordedSamplesLock);

	if (RecordedSamples.Num() == 0 || SampleRate == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MicRecorder: WAV化できる録音データがありません"));
		return false;
	}

	// float(-1.0〜1.0) を 16bit PCM に変換
	TArray<int16> PcmData;
	PcmData.SetNumUninitialized(RecordedSamples.Num());
	for (int32 i = 0; i < RecordedSamples.Num(); ++i)
	{
		const float Clamped = FMath::Clamp(RecordedSamples[i], -1.0f, 1.0f);
		PcmData[i] = static_cast<int16>(Clamped * 32767.0f);
	}

	const int32 DataSize = PcmData.Num() * sizeof(int16);
	const int32 ByteRate = SampleRate * NumChannels * sizeof(int16);
	const int16 BlockAlign = static_cast<int16>(NumChannels * sizeof(int16));
	const int32 ChunkSize = 36 + DataSize;

	OutWavBytes.Reset();
	OutWavBytes.Reserve(44 + DataSize);

	auto AppendString = [&OutWavBytes](const char* Str)
	{
		OutWavBytes.Append(reinterpret_cast<const uint8*>(Str), 4);
	};
	auto AppendInt32 = [&OutWavBytes](int32 Value)
	{
		OutWavBytes.Append(reinterpret_cast<const uint8*>(&Value), 4);
	};
	auto AppendInt16 = [&OutWavBytes](int16 Value)
	{
		OutWavBytes.Append(reinterpret_cast<const uint8*>(&Value), 2);
	};

	AppendString("RIFF");
	AppendInt32(ChunkSize);
	AppendString("WAVE");
	AppendString("fmt ");
	AppendInt32(16); // fmtチャンクサイズ
	AppendInt16(1);  // PCM形式
	AppendInt16(static_cast<int16>(NumChannels));
	AppendInt32(SampleRate);
	AppendInt32(ByteRate);
	AppendInt16(BlockAlign);
	AppendInt16(16); // ビット深度
	AppendString("data");
	AppendInt32(DataSize);
	OutWavBytes.Append(reinterpret_cast<const uint8*>(PcmData.GetData()), DataSize);

	return true;
}
