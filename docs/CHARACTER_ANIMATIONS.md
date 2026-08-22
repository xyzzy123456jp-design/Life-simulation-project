# キャラクター動作 (CHARACTER_ANIMATIONS.md)

Jennifer(旧Payton、内部アセット名Crimson)の身体・表情動作に関する構想と実装方針のメモ。

## 実装済み

- **口パク**: MetaHumanの「From Custom Mesh」でMeshyの顔をMetaHuman化し、標準のARKit LiveLink/RigLogic経由で動作。`ARKitLiveLinkSubsystem::PushJawOpen()`経由。
- **起動時の顔アップ**: `MicTestActor`が起動時にキャラクターをカメラの方へ`SetActorRotation`で向かせる仕組み(`AddTickPrerequisiteActor`でTick順序を明示的に固定し、他システムの上書きに打ち勝つ)。
- **感情表情**: Realtime/Legacyの両経路から共通のDirect Morph適用先へ`neutral / happy / surprised / sad / confused / embarrassed`を適用する。
- **自然なまばたき**: Crimsonの`eyeBlinkLeft / eyeBlinkRight`を`ULipSyncComponent`の既存Tick内で両目同期駆動する。間隔2.5～6.0秒、閉じる0.08～0.12秒、保持0.03～0.08秒、開く0.10～0.15秒。初回もランダム、DeltaTimeベース、値は0～1。表情収束・表情Backend可否、Legacy/Realtime、TTS/STT/VAD、jawOpenから独立している。
- **うなずき**: Crimson簡易Head Rigの独自`head` Boneへ約7度の下向き加算を行い、元へ戻す単発nodを実装。F10手動テストとRealtime/Legacy会話連動に対応し、表情・まばたき・口パクと同時動作する。
- **右手ジェスチャー**: MetaHuman BodyのBone finalize後へ右腕・前腕・手首のTransformを加算し、`raise_right_arm / wave_right / present_right`を手動およびRealtime/Legacy Function Callingから実行する。

## 検討中・未実装

### 視線(目の動き)
- ARKit標準の視線カーブ(`eyeLookUpLeft/Right`, `eyeLookDownLeft/Right`, `eyeLookInLeft/Right`, `eyeLookOutLeft/Right`)を、`jawOpen`と同じ経路(`ARKitLiveLinkSubsystem`)で送る想定。
- プレイヤーの方向を向き続ける「Look At」的な挙動を想定。

### 歩行・走行
- MetaHuman公式サンプルアニメーション(Idle/Walk/Run)とBlend Space/State Machineの組み合わせで実現可能。
- 想定シチュエーション(操作可能キャラクターにするか、AIが自動で移動するか、カットシーン演出か)は未確定。

### 座る・寝転ぶ

2026年8月22日に実装前のTransform追従調査まで完了。座り機能自体は未実装。

- プロジェクト内には、そのまま利用できることを確認済みのMetaHuman座りAnimation Sequence/Montageがない。Scene移動はActor Teleport方式なので、Seat Anchorが位置・向きを所有するIn-Placeアニメーションを第一候補とする。
- MyRoomは`RoomCharacterSeat`を移動地点として使用する。他Sceneは`JenniferAnchor`のみで、座面高・腰位置・椅子Actor・座る/立つ状態は未管理。既存のAnchor→Teleport経路はSeat Anchorへ流用できる。
- Crimsonは旧`StaticMesh`と同じ親へAttachされ、生成時に一度だけBoundsで自動整列される。MetaHuman Bodyの`head / neck_02`へAttachされず、Leader Poseも無効。Character Actor全体には追従するが、Bodyのpelvis/spine/neck/headアニメーションには追従しない。
- 表情、まばたき、口パク、Crimson独自headによるうなずきは座り中も独立して維持できる見込み。右手ジェスチャーも処理経路は維持できるが、座位Base Poseとの見た目は実機確認が必要。
- 第一候補はBody `head` SocketへCrimson ComponentをAttachし、現在の見た目を保つ基準Relative Offsetを保存する方式。これでReference Pose差を吸収できない場合は、Body Bone finalize後にBody `head` Transformと基準Offsetを合成してCrimson rootへ適用する。
- CrimsonとMetaHuman BodyはSkeleton階層が非互換なので、Leader Pose接続やSkeleton全体の対応付けは第一候補にしない。
- 現在のランタイムログではBody AnimInstanceが`None`で、既存の個別補正Poseも全身座位ではない。そのため、座位時のBody head移動量は座りアニメーションまたは診断用全身Poseを用意してから計測する。

実装時は次の順で進める。

1. Body headへのCrimson追従方式を立位で検証し、表情・まばたき・口パク・うなずきが壊れないことを確認する。
2. MetaHuman Skeleton対応のIn-Place座りアニメーションを用意する。
3. 各SceneへSeat Anchorと座る/立つ状態を追加する。
4. Body AnimBPへ座位Base Poseを安全に組み込む。
5. 座位でCrimson位置、右手ジェスチャー、カメラ構図を確認する。
