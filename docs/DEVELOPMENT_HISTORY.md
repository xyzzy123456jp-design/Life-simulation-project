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

## 今後の計画

- 視線(目の動き)、自然なまばたき、頭の上下(見上げる・見下ろす)、歩行・走行、座る・寝転ぶ等のキャラクター動作(詳細は`CHARACTER_ANIMATIONS.md`を参照)
- ストーリー展開(詳細は`STORY.md`を参照)
