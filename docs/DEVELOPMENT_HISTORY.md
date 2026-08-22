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

## 2026年8月21日(金): 会話連動アニメーションとScene構図の統一

- **担当・目的**: Codexが、Jenniferの会話中の自然な身体表現と、場所を切り替えても見かけの大きさが変わらない会話画面を実現するために実装・実機調整を行った。
- **うなずき**: Crimson用の簡易Head Rigと`UJenniferNodSkeletalMeshComponent`を追加し、下へ約7度動いて元へ戻るDeltaTimeベースの単発nodを実装。F10手動確認後、Realtime/LegacyのFunction Callingと明確な同意文の限定的fallbackへ接続した。表情、まばたき、口パクとの同時動作を確認した。
- **手ジェスチャー**: 右上腕・前腕・手首へComponent Space加算を適用し、`raise_right_arm / wave_right / present_right`を実装。手動巡回とRealtime/Legacy Function Callingへ接続し、音声再生開始時にジェスチャーを開始するよう同期した。MetaHumanの実際のBone軸と身体基準方向を用い、wave時に顔を隠さない横位置、present時の掌方向を実機調整した。
- **表情診断**: EキーのDirect Morph、RキーのRealtime text経由に加え、通常会話・AIテスト・手動発火をログで区別。Jennifer設定を大学生へ更新した。
- **会話履歴**: Legacyの`AChatManager`でsystemを先頭に保持しつつ、user/assistant/tool履歴を直近ターンへ制限して保持。HTTP 2xx検査と空`tool_call_id`防御も追加した。
- **Scene Scale**: JenniferのActor Scaleを起動時Canonical値へ固定し、Sceneごとの差を背景Scale側へ限定した。
- **会話カメラ**: MyRoomの正常なCamera距離/FOVをCanonical Framingとして保存。他Sceneでも距離/FOVを統一し、各Scene固有値は上下・左右の構図差に限定した。MyRoom復帰時にはFOVを明示復元する。

## 2026年8月22日(土): Scene別Manual ExposureとClassroom照明の安定化

- **担当・目的**: Codexが、MyRoomで確認済みの良好なJenniferの見え方を基準に、背景の雰囲気を維持しながら各Sceneの露出を揃えるためにA/B診断と実機調整を行った。
- **基準状態の確定**: 過去ログの`AB_MANUAL_ZERO`を再検証し、MyRoomの良好状態が`AutoExposureMethod=Manual`、Physical Camera Exposure OFF、Bias `0.0`、各Override ON、PostProcess Blend Weight `1.0`で成立していたことを確定・再適用した。
- **Scene別Bias**: 全会話SceneをManual Exposure／Physical Camera Exposure OFFへ統一し、背景とJenniferを実機比較してBiasを調整した。現在値はMyRoom `0.0`、Classroom `-6.0`、Cinema `-3.5`、JenniferRoom `-2.5`、Walk `-2.5`、Restaurant `-3.5`。
- **Lighting Channel**: Jenniferの描画PrimitiveをChannel 1、専用Key/Fill/Top Spot LightをChannel 1へ統一し、Scene直接光によるCG調の強い陰影を遮断した。Scene直接光をClassroomで再許可すると旧来の不自然な顔へ戻ることをA/B確認したため不採用とした。
- **Classroom専用照明**: Bias `-6.0`に対する専用灯倍率を`64倍`とし、Scene移動完了後`0.25秒`でVisibility、Active、Intensity、Color、Radius、Cone、カメラ基準Transform、Render Stateを再適用する。手動で8キーを2回押した時だけ適正化していた初期化タイミング問題を自動復元し、ユーザーが良好状態を確認した。
- **診断機能**: 8キーで強いマゼンタKey Light 1灯へ切り替え、再押下で通常3灯へ復元できる。ログにはLightのActive/Visibility/Intensity/Color/距離/Cone/ChannelとJennifer全PrimitiveのChannel/Visibility/Shadowを出力する。
- **非採用仮説**: Material差、Post Process Volume差、SkyLight単独差、Reflection Capture、Exposure残留、単一Scene Point Light、baked indirect、Jenniferの向き、LOD、背景の視覚的対比だけでは主因を説明できなかった。これらへ恒久変更は行っていない。

## 今後の計画

- Crimson簡易Head Rig化、視線、歩行・走行、座る・寝転ぶ等のキャラクター動作(詳細は`CHARACTER_ANIMATIONS.md`を参照)
- ストーリー展開(詳細は`STORY.md`を参照)
