# キャラクター動作 (CHARACTER_ANIMATIONS.md)

Jennifer(旧Payton、内部アセット名Crimson)の身体・表情動作に関する構想と実装方針のメモ。

## 実装済み

- **口パク**: MetaHumanの「From Custom Mesh」でMeshyの顔をMetaHuman化し、標準のARKit LiveLink/RigLogic経由で動作。`ARKitLiveLinkSubsystem::PushJawOpen()`経由。
- **起動時の顔アップ**: `MicTestActor`が起動時にキャラクターをカメラの方へ`SetActorRotation`で向かせる仕組み(`AddTickPrerequisiteActor`でTick順序を明示的に固定し、他システムの上書きに打ち勝つ)。

## 検討中・未実装

### 会話にあわせて表情を変える
- ARKit互換のブレンドシェイプ(`mouthSmileLeft/Right`, `browDownLeft/Right`, `eyeSquintLeft/Right`など)を、`jawOpen`と同じ経路(`ARKitLiveLinkSubsystem`)で送る想定。
- ChatGPT/Realtime APIの返答内容(テキストや感情トーン)に応じて、喜び・驚き・困惑などの表情を対応するブレンドシェイプの組み合わせに変換して適用する。
- 単純な例: 返答に「!」が多い/ポジティブな語彙が多い → 笑顔寄りのブレンドシェイプを強める、といったルールベースの対応付けから始めるのが手軽。
- 将来的には、応答生成時に感情ラベル(happy/sad/surprisedなど)も一緒に出力させ、それをブレンドシェイプのプリセットにマッピングする方式が精度が高い。

### 視線(目の動き)
- ARKit標準の視線カーブ(`eyeLookUpLeft/Right`, `eyeLookDownLeft/Right`, `eyeLookInLeft/Right`, `eyeLookOutLeft/Right`)を、`jawOpen`と同じ経路(`ARKitLiveLinkSubsystem`)で送る想定。
- プレイヤーの方向を向き続ける「Look At」的な挙動を想定。

### 自然なまばたき
- RigLogicは`Use ARKit Face`(LiveLink外部入力モード)だと、LiveLinkから明示的に値を送らないカーブは動かない。
- ランダムな間隔(2〜6秒程度)で`eyeBlinkLeft`/`eyeBlinkRight`を短時間(0.1〜0.2秒)だけ動かす自前実装が必要。
- 現状`ARKitLiveLinkSubsystem`に瞬き専用のPush関数はまだ無い。今後追加予定。

### 頭の上下(見上げる・見下ろす)
- 特定のイベント・会話内容に応じて、はっきり見上げる・見下ろす大きな動きを想定。
- C++からの直接ボーン操作(`SetBoneTransformByName`)はUE5.8で使用不可と判明済み。
- 代わりにAnimation Blueprint側に「Transform (Modify) Bone」ノードを追加し、`head`/`neck_01`ボーンに外部から角度を加算できる変数(例: `HeadLookRotation`)を用意する方式を推奨。
- C++/イベント側からはAnimInstance経由でこの変数を書き換えるだけで済む。

### 歩行・走行
- MetaHuman公式サンプルアニメーション(Idle/Walk/Run)とBlend Space/State Machineの組み合わせで実現可能。
- 想定シチュエーション(操作可能キャラクターにするか、AIが自動で移動するか、カットシーン演出か)は未確定。

### 座る・寝転ぶ
- 専用アニメーション+Body AnimBPのステートマシンで実現可能。
- 車に乗っている間は自動的に「座る」ポーズに切り替える、という使い方を想定(VRカーライフシミュレーターの構想に合わせて)。
