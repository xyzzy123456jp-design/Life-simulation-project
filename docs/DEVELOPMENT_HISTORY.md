# 開発履歴 (DEVELOPMENT_HISTORY.md)

VRカーライフシミュレータープロジェクトの開発経過記録。

## 2026年8月7日(金): 最初のプロトタイプ完成

- Unreal5 車のシミュレーターサンプルでChatGPTでの音声チャットに対応

## 2026年8月8日(土): MetaHumanで喋ることができた

- Unreal5のMetaHumanサンプルで喋ることができた

## 2026年8月10日(月): VRドライブ + 部屋への行き来

- VRでドライビング、Paytonの部屋を作って行き来できるようになった

## その後: 口パク方式の模索とキャラクター刷新

- ARKit LiveLink経由(RigLogic駆動)での口パク方式を実装(自前LiveLinkソースでjawOpen等のARKit標準カーブを配信)
- 会話エンジンをWhisper/ChatManager/TTSの3コンポーネント構成から、OpenAI Realtime API(WebSocket常時接続)ベースのRealtimeVoiceComponentに置き換え
- キャラクターをPaytonからJennifer(内部アセット名Crimson、Meshyで生成した顔)に変更する過程で、ARKit LiveLink方式が新しい顔メッシュと噛み合わず口パクが動かなくなる問題が発生
- 一時的にBlenderでメッシュへ直接jawOpenモーフターゲットを追加する方式に変更して解決(歯・口腔メッシュも削除)。あわせて重いメッシュ(156万頂点/422MB)を軽量版(約39万頂点/107.7MB)に差し替え、起動時の約20秒フリーズも解消
- 最終的に、MetaHumanの「From Custom Mesh」機能でMeshyの顔を正式にMetaHuman化し、標準のARKit LiveLink/RigLogic経由の口パクに戻して正常動作するようになった

## 2026年8月20日(木): Legacy感情会話・低遅延化・自動まばたき

- **担当・目的**: Codexが、Legacy会話でもJenniferの感情を顔と声へ反映し、録音/STT待ちを短縮し、静止時の自然さを改善するために実装・実機診断を行った。
- **Legacy表情**: `express_emotion`を通常API経路にも統合。対応感情は`neutral / happy / surprised / sad / confused / embarrassed`。顔には`neutral=0`、それ以外は診断で視認性を確認したminimum `0.8`を適用し、AIが返した元intensityは別に保持する。
- **Legacy感情TTS**: `tts-1`から`gpt-4o-mini-tts`へ変更し、voiceは`coral`を維持。AI元emotion/intensityから短い`instructions`を生成する。高intensityでも早口・大声になりすぎない制約を常に含め、顔用minimum 0.8はTTSへ流用しない。
- **録音/VAD**: 実測は発話RMS約`0.07～0.24`、無音RMS約`0.012～0.023`で、旧閾値`0.25`は発話を検出できず20秒上限へ到達する原因だった。`VoiceActivityThreshold`だけを`0.25 → 0.05`へ変更し、無音確定`1.8秒`と最大録音`20秒`は維持。修正前5会話は`silence=2 / max_duration=3`、修正後5会話は`silence=5 / max_duration=0`。TTS再生と録音開始は重複せず、スピーカー回り込みは主因ではなかった。
- **STT**: 詳細計測でローカルのWAV準備・JSON解析はほぼ0秒、待ち時間の大半がAPI応答と判明。同条件A/Bでは`whisper-1`平均`1.949秒`、`gpt-4o-mini-transcribe`平均`1.151秒`で、平均`0.798秒（40.9%）`短縮。英語中心の通常利用で認識品質も同等以上だったため後者を正式採用した。録音/VAD条件は変更していない。
- **自動まばたき**: Crimsonへ`eyeBlinkLeft / eyeBlinkRight` Morphを追加し、両目同期・初回を含むランダム間隔で自動駆動。閉眼不足と外眼角側の変形不足を生成スクリプトで調整し、UEへ再インポート済み。詳細仕様は`FACIAL_EXPRESSION_SPEC_v5.md`を参照。
- **整理**: 自動まばたきの所有権を`ULipSyncComponent`へ一本化し、旧Poseable Meshのblink bone初期化と未使用状態を削除した。

### 次フェーズ（調査済み・未実装）

次回は**Crimson簡易Head Rig化の設計・調査**から開始する。現在のCrimsonは`root` Boneのみで全頂点がrootへ100%ウェイトされ、MetaHuman Leader Poseにも接続されていない。この状態でrootを回すと肩・胴体まで回るため、C++からのHead Bone回転は採用しない。詳細は`CHARACTER_ANIMATIONS.md`を参照。

## 今後の計画

- Crimson簡易Head Rig化、視線、歩行・走行、座る・寝転ぶ等のキャラクター動作(詳細は`CHARACTER_ANIMATIONS.md`を参照)
- ストーリー展開(詳細は`STORY.md`を参照)
