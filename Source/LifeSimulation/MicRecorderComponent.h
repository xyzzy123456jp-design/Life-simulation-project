#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "MicRecorderComponent.generated.h"

/**
 * マイクからの音声をメモリ上のPCMバッファに録音するコンポーネント。
 * ファイル保存はデバッグ確認用のオプション機能であり、
 * 本番の用途ではGetRecordedSamples()の内容を直接Whisper API送信に使う想定。
 *
 * ※ このAPIはUE5のバージョンによって細部が異なる場合があります。
 *    お使いのエンジンバージョンでビルドエラーが出た場合、
 *    Audio::FAudioCapture / Audio::FOnAudioCaptureFunction まわりの
 *    シグネチャをエンジンソース(AudioCaptureCoreモジュール)で確認してください。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIFESIMULATION_API UMicRecorderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMicRecorderComponent();

	// 録音開始
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	bool StartRecording();

	// 録音停止
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	void StopRecording();

	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	bool IsRecording() const { return bIsRecording; }

	// デバッグ用: 録音済みPCMデータをWAVファイルとして書き出す(耳で確認するため)
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	bool SaveRecordedAudioToWav(const FString& FilePath);

	// 録音済みPCMデータをメモリ上でWAVバイト列に変換する(ファイル保存はしない)。
	// Whisper API等への直接送信はこちらを使う。
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	bool BuildWavBytes(TArray<uint8>& OutWavBytes);

	// 録音済みの生PCM(float, -1.0〜1.0, インターリーブ)を取得
	const TArray<float>& GetRecordedSamples() const { return RecordedSamples; }

	// 録音データに実質的な音声(閾値を超える音量)が含まれているかを判定する。
	// 無音・ノイズだけの録音をWhisper APIに送ってハルシネーションさせないためのチェック用。
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	bool HasSignificantAudio(float Threshold = 0.02f);

	// 録音バッファの末尾からWindowSeconds秒分のRMS音量を取得する。
	// リアルタイムに「今しゃべっているか/黙っているか」を判定するのに使う。
	UFUNCTION(BlueprintCallable, Category = "MicRecorder")
	float GetRecentRms(float WindowSeconds = 0.3f);

	int32 GetSampleRate() const { return SampleRate; }
	int32 GetNumChannels() const { return NumChannels; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnAudioCapture(const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow);

	Audio::FAudioCapture AudioCapture;

	TArray<float> RecordedSamples;
	FCriticalSection RecordedSamplesLock;

	bool bIsRecording = false;
	int32 SampleRate = 0;
	int32 NumChannels = 0;
};
