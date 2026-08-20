# キャラクター動作 (CHARACTER_ANIMATIONS.md)

Jennifer(旧Payton、内部アセット名Crimson)の身体・表情動作に関する構想と実装方針のメモ。

## 実装済み

- **口パク**: MetaHumanの「From Custom Mesh」でMeshyの顔をMetaHuman化し、標準のARKit LiveLink/RigLogic経由で動作。`ARKitLiveLinkSubsystem::PushJawOpen()`経由。
- **起動時の顔アップ**: `MicTestActor`が起動時にキャラクターをカメラの方へ`SetActorRotation`で向かせる仕組み(`AddTickPrerequisiteActor`でTick順序を明示的に固定し、他システムの上書きに打ち勝つ)。
- **感情表情**: Realtime/Legacyの両経路から共通のDirect Morph適用先へ`neutral / happy / surprised / sad / confused / embarrassed`を適用する。
- **自然なまばたき**: Crimsonの`eyeBlinkLeft / eyeBlinkRight`を`ULipSyncComponent`の既存Tick内で両目同期駆動する。間隔2.5～6.0秒、閉じる0.08～0.12秒、保持0.03～0.08秒、開く0.10～0.15秒。初回もランダム、DeltaTimeベース、値は0～1。表情収束・表情Backend可否、Legacy/Realtime、TTS/STT/VAD、jawOpenから独立している。

## 検討中・未実装

### 視線(目の動き)
- ARKit標準の視線カーブ(`eyeLookUpLeft/Right`, `eyeLookDownLeft/Right`, `eyeLookInLeft/Right`, `eyeLookOutLeft/Right`)を、`jawOpen`と同じ経路(`ARKitLiveLinkSubsystem`)で送る想定。
- プレイヤーの方向を向き続ける「Look At」的な挙動を想定。

### うなずき（次回開始地点: Crimson簡易Head Rig化の設計・調査）

2026年8月20日の調査時点では実装しない。

- MetaHuman Bodyの階層は`neck_01 → neck_02 → head`。
- MetaHuman側には`ABP_MetaHuman_f_med_nrw_Retargeting`、`Face_AnimBP`、`Face_PostProcess_AnimBP`、`HeadMovementIK_Proc_CtrlRig`等の既存Head/Neck制御があり、追加回転の所有権競合を避ける必要がある。
- 現在表示中のCrimsonは`root` Boneしか持たず、全頂点がrootへ100%ウェイトされている。MetaHumanのLeader Poseへ接続されていないため、MetaHumanのHead Boneを動かしても追従しない。
- Crimson rootを回すと顔・髪だけでなく肩・胴体も回る。このため現状のままC++からHead Bone回転を追加する方法は採用しない。

次回は次の順で進める。

1. Crimsonへneck/head系Boneを追加する方法を検討する。
2. 頭・顔・髪と肩・胴体のウェイト分離方法を検討する。
3. MetaHuman側との接続方式を決定する。
4. AnimBPまたはControl Rigによる安全な加算回転方式を決定する。
5. AI連動なしで「下へ5～8度 → 元へ戻る」単発nodを実装・確認する。
6. 動作確認後、Function Calling連動を別工程で検討する。

### 歩行・走行
- MetaHuman公式サンプルアニメーション(Idle/Walk/Run)とBlend Space/State Machineの組み合わせで実現可能。
- 想定シチュエーション(操作可能キャラクターにするか、AIが自動で移動するか、カットシーン演出か)は未確定。

### 座る・寝転ぶ
- 専用アニメーション+Body AnimBPのステートマシンで実現可能。
- 車に乗っている間は自動的に「座る」ポーズに切り替える、という使い方を想定(VRカーライフシミュレーターの構想に合わせて)。
