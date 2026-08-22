# Jennifer 着席機能仕様書 (SEATED_CHARACTER_SPEC_v1.md)

**Phase A: Body-Crimson追従 / Phase B〜D: 着席**

> v1: (ChatGPT作成) 着席機能の初期仕様。調査済みのBody/Crimson分離構造を前提に、head追従を先行検証し、その後に1部屋1脚の着席へ進む段階設計を定義。
>   理由: 座りAnimationを先行するとBodyとCrimsonの位置が分離する可能性があるため。
> - [Claude指摘→反映] セクション4の「現在のBody: AnimInstanceがNone」という記述が、他の確認済み事実と同列の1行として扱われていたため、セクション4.1として独立させ、Phase B着手前に必ず再確認すべき重要事項に格上げした。実装順序にも専用のステップを追加した。
>   理由: もしBodyに現在AnimBPが実質機能していない場合、Phase Bが想定する「既存AnimBPへMontage用Slotを追加する」という前提自体が成立せず、また既存のうなずき・手ジェスチャー(AnimBP評価後に加算する設計)の土台も揺らぐ可能性があるため。この重大さに対して、仕様書内での扱いが軽すぎた。
> - [Claude指摘→反映] セクション10(既存機能との共存)がSeated(着席完了後)の状態のみを想定しており、EnteringSeat/LeavingSeat(姿勢遷移中)にジェスチャー・うなずきが割り込んだ場合の挙動が未定義だったため、セクション11.1として新設し、対応するテスト項目(13.1、C-6/C-7)を追加した。
>   理由: HAND_GESTURE_SPECで確立した「進行中の再トリガーを無視する」という考え方が、ここでは逆方向(姿勢遷移中にジェスチャー/うなずきが割り込む)の組み合わせとして未検討のまま残っていたため。
> - [Claude指摘→反映] 実装順序(セクション14)のステップ3・4が、セクション7.1の確認順序(まずSlot有無を確認→その結果に応じて座りBase Poseの入れ方を設計)と逆になっており、先にアセットを用意してから再生経路を確認する順番になっていたため、「まず再生経路を確認→その結果に合わせてアセットを用意する」の順に入れ替えた。あわせてセクション7冒頭にも同じ順序を明記した。
>   理由: Slotが存在しない、または安全に追加できないと後から判明した場合、先に用意したアセットの形式(Montage前提かBase Pose前提か)自体が無駄になる可能性があり、手戻りを避けるため。
> - [チャッピー指摘→反映] Phase Aの成功条件(セクション6.4)とテスト項目(A系)に、「Body head回転とCrimson独自head Boneのうなずき加算が同時に発生しても、回転が過剰に重なって不自然にならないこと」(A-4)を追加した。
>   理由: headへAttachする設計上、Body側headの回転がCrimson全体へ入った上に、さらにCrimson独自headのうなずきが加算されるため、二重回転が視覚的に不自然にならないかの確認が抜けていたため。
> - [チャッピー指摘→反映] Seat Anchorの基準点が「椅子の座面中心」なのか「Jennifer Actor rootの最終位置」なのか未定義だったため、セクション8.1に「Seat Anchor = 着席時のJennifer Actor root Transform」と1つに固定して明文化した。
>   理由: この基準点が部屋ごとにバラバラだと、各部屋の補正値が意味するものがズレ、Seat Anchorデータの管理が破綻するため。
> - [チャッピー指摘→反映] セクション11.2として、「着席中に別Sceneへ直接切り替えられた場合、正常な離席フロー(LeavingSeat)を経由せずに、座りAnimation・Seat状態・一時Offsetを強制的にリセットする」経路を追加し、対応するテスト項目(C-8)も追加した。
>   理由: C-5(離席テスト)は正常なLeavingSeatを経た場合のみを想定しており、Scene切替によってこの正常フローを経由せず割り込まれるケースが未対応だったため。
> - [チャッピー指摘→反映] セクション9(Phase D)に、「全Sceneで必ず座る」ことを前提とせず、各Sceneに`SeatingEnabled`フラグを持たせて椅子が存在するSceneだけを着席対象にする方針を追加した。展開対象の一覧から`Walk`を、椅子の存在が確認できていないという理由で除外した。
>   理由: これまでの調査結果からは`Walk`(散歩等を想定したSceneと思われる)に椅子が存在するとは確認できておらず、全Scene一律で着席を前提にすると、椅子の無いSceneで不具合が起きるリスクがあるため。
> - [チャッピー指摘→反映] セクション11の`LeavingSeat`の実体が未定義だったため、「v1では専用のStand Up Animationは必須とせず、まずSeated状態解除→Standing Pose復帰を優先する。座りAnimationの逆再生は推奨しない」と明記した。
>   理由: 実装者が座りAnimationの逆再生やRoot Motionを勝手に採用してしまうリスクがあり、v1で採用する方式を1つに決めておく必要があったため。
> - [チャッピー指摘→反映] セクション8.3として、Character CapsuleとSeat Anchor周辺のCollisionが干渉する可能性への対応(Collisionの恒久的な無効化はせず、着席中だけ一時的な切り替えを検討する)を追加し、対応するテスト項目(C-1b)も追加した。
>   理由: 椅子・テーブル等のCollisionとCharacter Capsuleが干渉し、テレポートやEnteringSeatの位置合わせが失敗する可能性が、これまでの仕様に含まれていなかったため。
> - [チャッピー指摘→反映] セクション7.1に、「着席Animation再生完了後、Seated状態でどう座位Poseを継続保持するか」を実装前に決める項目を追加し、「Montage終了で自動的にStandingへ戻ることは許容しない」と明記した。対応するテスト項目(B-2)も追加した。
>   理由: Montageは通常一度再生されて終わるため、何も対策しなければ再生完了後に通常のStanding/Idle Poseへ自動的に戻ってしまう可能性があり、これまでの仕様には「終了後どう座位を維持するか」が明示されていなかったため。
> - [チャッピー指摘→反映] セクション8.1で「Seat Anchor = 最終Actor root Transform」と定義しつつ、同時に「腰位置補正」「前後左右微調整」という別の補正値も持たせており、最終Transformであるはずのものにさらに補正を加えるという矛盾があったため、「Seat Anchor(椅子側の基準Transform、Jennifer固有の値は含まない) + SeatOffset(Jennifer側の補正値)」の2段階へ明確に分離した。ランタイムでこれ以上の追加Offsetを重ねないことも明記した。8.2の記述もこの定義に合わせて更新した。
>   理由: 「最終Transformなのにさらに補正が乗る」という中間状態を放置すると、実装者がどちらを信頼すべきか判断できず、部屋ごとにデータの意味がバラつくリスクがあったため。
> - [チャッピー指摘→反映] セクション11.2(Scene切替時の強制リセット)に、8.3で一時変更したCollision設定(Collision Profile/Capsule Size/Collision Response)の復元も含めるよう明記した。対応するテスト項目(C-8)も更新した。
>   理由: 座りAnimation・Seat状態・一時Offsetの解除だけがリセット対象として明記されており、Collision設定の復元が抜けていた場合、次のSceneへ縮小したCapsuleがそのまま持ち越されるような、気づきにくい不具合につながるため。
> - [チャッピー指摘→反映] セクション9.1として、「`SeatingEnabled=true`だが必要なSeat Anchor/SeatOffset/着席Animation等が実際には取得できない(設定ミス等)場合、無理に着席処理を続行せず、Warningを出してStandingへ安全にフォールバックする」というルールを新設した。対応するテスト項目(D-2)も追加した。
>   理由: `SeatingEnabled`の導入自体は良い判断だったが、フラグがtrueなのに必要なデータが揃っていない場合の挙動が未定義のままだと、クラッシュや原点(0,0,0)への意図しない移動につながるリスクがあったため。
> - [チャッピー指摘→反映] セクション8.1の「Seat Anchor + SeatOffset」という記述が、TransformをWorld空間での単純な数値加算であるかのように読めたため、「SeatOffsetはSeat AnchorのLocal Spaceにおける相対Transformとし、最終TransformはUEのTransform合成で算出する(単純加算しない)」と明確化した。
>   理由: 椅子の向きが部屋ごとに異なる(例えば90度回転した椅子)ため、Transform合成方法が曖昧なまま実装すると、「ある部屋だけ前後方向の補正が横方向へずれる」という事故が起こり得るため。
> - [チャッピー指摘→反映] セクション8.4として、Character Movement/Gravity/Velocityの扱いを新設した。EnteringSeat開始時にMovement ModeとVelocityを保存し、着席中はMovementを停止・Velocityを0にし、Standing復帰またはScene強制切替時に復元する方針を明記した。11.2(強制リセット)にもこの復元を含め、対応するテスト項目(C-1c、C-5、C-8)を追加・更新した。
>   理由: Character Movement Componentが有効なまま着席すると、Gravityによる沈み込み、残留Velocityによる位置ズレ、Walking処理によるSeat位置の意図しない移動が起こり得るが、これまでの仕様にはCollisionの扱いはあってもMovement/Velocityへの言及が抜けていたため。
> - [チャッピー指摘→反映(文書整理)] タイトル直下の「Phase 1: Body-Crimson追従 / Phase 2: 着席」を、本文のPhase A〜D構成に合わせて「Phase A: Body-Crimson追従 / Phase B〜D: 着席」に修正した。セクション1(目的)の「Jenniferを各Conversation Sceneの椅子へ自然に着席させる」も、`SeatingEnabled`導入後の実態に合わせ「着席対象となるConversation Sceneの椅子へ」に修正した。
>   理由: タイトルが本文のPhase命名(A〜D)と一致しておらず、目的の記述もSeatingEnabledで「全Scene一律ではない」と変更した内容と食い違って見えたため。
> - [チャッピー指摘→反映] セクション8(Phase C冒頭)に残っていた「JenniferをSeat Anchorへテレポートし」という表現を、「Seat AnchorとSeatOffetから合成した最終World Transformへテレポートし」に修正した。また、本文各所の`Seat Anchor + SeatOffset`という表記を`Compose(SeatAnchor, SeatOffset)`に統一した。
>   理由: 8.1で「単純加算ではなくTransform合成」と定義したにもかかわらず、Phase C冒頭や8.2の本文表現には`+`という加算を連想させる古い表記が残っており、読み手に「単純な数値加算」と誤読される余地があったため。
> - [チャッピー指摘→反映] セクション8.4の「EnteringSeat開始時にVelocityを保存し、Standing復帰時に復元する」という方針を、「Movement Modeのみ保存・復元し、Velocityは復元対象とせず、着席開始時・離席時とも常に0とする」方針へ変更した。11.2(Scene強制リセット)、テスト項目(C-5、C-8)も同じ方針に合わせて修正した。
>   理由: 着席前のVelocityをそのまま復元すると、例えば着席直前に何らかの速度を持っていた場合、立ち上がった瞬間に古い速度のまま動き出してしまう危険な仕様だったため。診断用にログへ記録することは許容するが、実際の復元対象には含めないこととし、将来「ドアから歩いて座る」実装時のVelocity継承は別途定義することとした。
> - [hiroshiさん依頼→Claude作成] セクション11.0として、「Scene開始時のデフォルトの入り口を`Seated`にする」という方針変更を新設した。`SeatingEnabled = true`のSceneでは、Scene開始(BeginPlay/Scene到達)時点でJenniferが最初から着席済みの状態でロードされ、`Standing`→`EnteringSeat`(座る動作のアニメーション再生)を経由する通常の遷移フローはデフォルト経路として使わないこととした。あわせて、Phase C(セクション8)・Phase D(セクション9)・完了条件(セクション15)の記述、およびテスト項目(C-0新設、D-1更新)をこの方針に合わせて更新した。
>   理由: hiroshiさんの要望により、「各部屋の椅子に最初から座っている」ところから始める形へ変更するため。ただし`EnteringSeat`/`LeavingSeat`という状態・処理自体は、Phase Cでの手動診断テスト用途、およびセクション16で予定している「ドアから歩いて椅子に座る」機能で再利用する前提のため、削除せず維持することとした。

## 1. 目的

Jenniferを着席対象となるConversation Sceneの椅子へ自然に着席させる機能を実装する。ただし、現在のCrimson顔メッシュはMetaHuman Bodyのhead / neck_02 Boneへ追従していないため、最初にBodyの頭部アニメーションへCrimson Component全体を安全に追従させる基盤を確立する。

本仕様の最終到達点は「各部屋でJenniferが指定されたSeat Anchorに配置され、In-Placeの着席状態を維持しながら、既存の表情・まばたき・口パク・うなずき・手ジェスチャーを可能な範囲で継続できること」である。

## 2. スコープ

- Phase A: CrimsonをMetaHuman Bodyのheadへ追従させる基盤の検証・実装
- Phase B: MetaHuman Skeleton対応のIn-Place着席Animationの導入
- Phase C: 1部屋・1脚でのSeat Anchor着席テスト
- Phase D: 各Conversation SceneへのSeat Anchor展開

## 3. 今回のスコープ外

- ドアから椅子までの連続歩行
- NavMesh / AI Move To / 経路探索
- 歩行Animationと着席Animationの遷移
- Root Motionを利用した着席位置決め
- Function Calling / AI判断による着席・離席
- CrimsonのMetaHuman Skeletonへの全面的な再Rig

## 4. 調査済みの事実(実装前提)

| 項目 | 確認済み内容 |
|---|---|
| CrimsonのAttach | 旧StaticMeshと同じ親ComponentへAttachされ、MetaHuman Body / head Socket / neck_02 SocketへはAttachされていない。 |
| Leader Pose | Crimson生成時に`SetLeaderPoseComponent(nullptr, false, false)`が設定され、MetaHuman BodyのLeader Poseから明示的に切り離されている。 |
| Actor移動 | Scene切替時のCharacter Actor全体のLocation / Rotation / ScaleにはCrimsonも追従する。 |
| Bone Animation | Bodyのpelvis / spine / neck / head Bone変形はCrimsonへ反映されない。 |
| Crimson Skeleton | 独自の簡易Skeleton(少なくとも`root → head`)を持つ。MetaHuman Body Skeletonとは非互換。 |
| うなずき | Crimson独自head Boneへ加算しており、Body head追従とは役割を分離できる。 |
| 座りAnimation | 現プロジェクト内にSit / Sitting / Seated等として確認できる着席Animation Sequence / Montageは存在しない。 |
| Scene位置決め | 既存の`JenniferAnchor` Transformを使った`TeleportCharacterActorTo()`構造はSeat Anchorへ流用可能。 |
| MyRoom | `RoomCharacterSeat`という参照は存在するが、現状は座りAnimationではなく単なる移動地点として利用されている。 |

### 4.1 【重要・要再確認】Body AnimInstanceが`None`だった件

ランタイムログでは、Body AnimInstanceが`None`の状態が確認されている。これは単なる「Montage再生可能性が未確定」という軽い話ではない可能性がある。**もしBodyに現在AnimBPが一切設定されていない(または実行時に外れている)場合、以下の前提そのものが揺らぐ:**

- Phase B(セクション7)が想定する「既存AnimBPへMontage用Slotを追加する」という方式自体が成立しない(AnimBPが動いていなければSlotを追加する意味がない)
- 既存のうなずき・手ジェスチャーは「AnimBP評価(`OnBoneTransformsFinalized`)後に、その結果へ加算する」設計になっているが、AnimBPが実質何も出力していない(Reference Poseのまま)場合、これまで「正常に動いている」と見えていたものが、実際には偶然Reference Poseの上に加算値が乗っていただけの可能性がある

**Phase B着手前(実装順序のステップ4より前)に、以下を必ず確認すること**:

1. `None`という結果が、特定のタイミング(初期化前など)でのみ観測される一時的な状態なのか、ゲーム中ずっと`None`のままなのかを確認する
2. Body Skeletal Mesh Componentに、そもそもAnimation Blueprintクラスが割り当てられているか(コンポーネントのAnim Class設定)をUEエディタ上で確認する
3. 割り当てられているにもかかわらず実行時に`None`になっている場合、その原因(初期化順序、`SetAnimInstanceClass()`の呼び忘れ等)を特定する
4. この前提が崩れている場合、Phase Bの設計(Slot追加によるMontage再生)自体を見直す必要がある


## 5. 基本設計方針

Body側は全身姿勢(立位・着席)を所有し、Crimson側は顔表現(Morph、口パク、まばたき、独自headによるうなずき)を所有する。Body headのTransformはCrimson Component全体の基準Transformとして利用し、Crimson内部のhead Bone制御とは混同しない。

**所有権の原則**:

- MetaHuman Body: pelvis / spine / neck / headを含む全身姿勢
- Body head追従層: Crimson Component rootの位置・回転追従
- Crimson内部: 表情Morph / eyeBlink / jawOpen / 独自head Boneのうなずき

## 6. Phase A — Body headへのCrimson追従

### 6.1 第一候補: head Socket/Bone Attach

最初に、`CrimsonGazeMorphComponent`をMetaHuman Bodyのhead Socket/BoneへAttachする方式を最小差分で検証する。着席Animation、Seat Anchor、歩行処理はこの段階では追加しない。

### 6.2 Attach前の診断ログ

- Crimson World Location / Rotation / Scale
- Crimson Relative Transform
- 現在のAttach Parent Component名
- Body Component World Transform
- Body head Bone/Socket World Transform

### 6.3 Transform維持

Attach時にSnapToTargetを無条件使用しない。現在の立ち姿で成立しているCrimsonのWorld Transformを維持したまま親だけをheadへ変更する。UEのKeepWorldTransform相当を第一候補とし、必要ならhead基準のRelative Offsetを保存・適用する。

既存のBoundsベース自動整列(Scale/Offset)を破壊しないこと。現在確認されている自動整列値や最終Scaleは診断情報として扱い、ハードコードされた固定補正へ安易に置き換えない。

### 6.4 Phase A 成功条件

- Attach前後で立ち姿の顔位置・Rotation・Scaleが視覚的に変化しない
- Body head / neck / spineを診断的に動かした際、Crimson Component全体が追従する
- 表情Morphが従来通り動作する
- 自動まばたきが従来通り動作する
- jawOpen / 口パクが従来通り動作する
- Crimson独自head Boneによるうなずきが従来通り動作する
- **Body head回転(headへのAttachにより追従する分)とCrimson独自head Boneのうなずき加算が同時に発生しても、回転が過剰に重なって不自然に見えない(A-4参照)**
- 歯・Material・Lighting Channel・Visibilityが変化しない

### 6.5 フォールバック

Socket Attachで位置ずれ、Scale破綻、回転軸不整合、既存顔機能との競合が発生する場合は、無理にOffset調整を重ねず変更を戻す。第二候補としてBodyの`OnBoneTransformsFinalized`後にhead Transformを取得し、保存した基準Offsetを合成してCrimson rootへ適用する方式を検討する。

通常Tickでの追従はAnimBP評価順による1フレーム遅延の可能性があるため、Bone finalize後方式より優先しない。

## 7. Phase B — 着席Animation

Phase Aが成功してから着手する。**まず7.1の再生経路確認を行い、その結果に応じたAnimation形式(Montage前提かBase Pose前提か)を確定してから、着席Animationアセットを用意すること。** アセットを先に用意してから再生方式を確認する順序にはしない(実装順序14章参照)。MetaHuman Skeleton互換のIn-Place着席Animation Sequenceを用意する。Root MotionはSeat Anchorによる位置決めと競合する可能性があるため、初期実装では使用しない。

### 7.1 実装前確認

- Body AnimBP / AnimGraphにMontage用Slotが存在するかUEエディタで確認
- Slotが無い場合、既存AnimBPを壊さず座りBase Poseを入れる方法を設計
- `PlayAnimation()`でAnimBPを丸ごと置換する方式は原則採用しない
- 着席AnimationのSkeleton互換性、Reference Pose、Root Motion設定を確認
- **【重要】着席Animation(Montage等)の再生完了後、`Seated`状態でどう座位Poseを継続保持するかを決める。** Montageは通常一度再生されて終わるため、何もしなければ終了後にBody AnimBPの通常のStanding/Idle Poseへ自動的に戻ってしまう可能性がある。Loop再生、専用のState Machineステート、最終フレームでのPose保持など、採用方式は上記のSlot確認結果を踏まえて決定する。**Montage終了によって自動的にStandingへ戻ることは許容しない。**

## 8. Phase C — 1部屋・1脚テスト

最初は1 Scene、1 Chairのみを対象とする。Scene切替やAI連動を同時に実装しない。

**v1の主目的は「Scene開始時、Jenniferが最初からその椅子に座った状態で表示されること」を1部屋で確認することである(11.0参照)。** Jenniferを`Seat Anchor`と`SeatOffset`から合成した最終World Transformへ配置し、最初から座位Poseの状態で初期化する。座る動作(`EnteringSeat`のアニメーション再生)自体は、Phase Cの診断用テストとして手動トリガーで確認できるようにしておくが、Scene開始時のデフォルト経路としては使わない。

### 8.1 Seat Anchorに必要な情報

**【重要】Seat Anchorの定義を一本化する**: 以前の版では「Seat Anchor = 着席時のJennifer Actor rootの最終Transform」としつつ、同時に「腰位置補正」「前後左右微調整」という別の補正値も持たせており、最終Transformであるはずのものにさらに補正を加えるという矛盾があった。以下のように2段階へ明確に分離する。

- **Seat Anchor**: 椅子側の基準Transform(例: 椅子オブジェクトの位置・向き、または椅子に対して事前に決めた基準点)。**Jennifer固有の値は含まない。**
- **SeatOffset**: Seat Anchorから見た、Jenniferを実際にテレポートさせる最終Actor root位置・向きまでの補正値(腰位置補正、前後・左右微調整を含む)。Jennifer側の事情(身長・体格・Capsuleサイズ等)に応じた調整はすべてここに集約する。

**最終的にJenniferをテレポートさせる位置は`Seat Anchor`と`SeatOffset`の合成で一度だけ算出し、ランタイムでこれ以上の追加Offsetを重ねて加算しないこと。** Seat Anchor作成時にSeatOffsetを含めて事前に確定させ、実行中に毎回別の補正を継ぎ足す設計にはしない。

**【重要】Transform合成方法を明確化する**: `Compose(SeatAnchor, SeatOffset)`は単純な数値(Location/Rotationの値)の加算ではない。特に椅子が回転している場合、`SeatOffset`の「前へ10cm」のような値は、その椅子の向き(Seat AnchorのRotation)を基準にした方向でなければならない。

- **`SeatOffset`はSeat AnchorのLocal Spaceにおける相対Transform(Relative Transform)として定義する。**
- 最終的なWorld Transformは、UEのTransform合成(例: `FTransform::operator*`や`FTransform::GetRelativeTransformReverse()`相当の、親子関係のTransform合成)によって算出し、**Location/RotationをそれぞれWorld空間で単純加算しない。**
- 部屋ごとに椅子の向きが異なる(例えば90度回転した椅子)ため、この合成方法が曖昧なままだと、「ある部屋だけ前後方向の補正が横方向へずれる」という事故が起こる。実コード内でTransform合成の順序(どちらを親としてどちらを子として扱うか)を統一すること

Seat Anchor / SeatOffsetとして各Sceneが持つ情報:

- Seat Anchor Location / Rotation(椅子側の基準Transform)
- SeatOffset(Seat Anchorから最終Actor root位置・向きまでの補正値。腰位置補正・前後左右微調整を含む)
- 座る / 立つ状態
- 使用する着席Animation

### 8.2 既存位置決め処理の流用

既存の`JenniferAnchor` Transform取得 → `TeleportCharacterActorTo(Location, Rotation)`の基本構造を流用する。ただし流用時は、`Compose(SeatAnchor, SeatOffset)`で事前に算出した最終位置・向きを`TeleportCharacterActorTo()`へ渡す形にする(8.1参照)。MyRoomの`RoomCharacterSeat`は名前だけで着席を意味すると仮定せず、実Transformを確認してSeat Anchorとして利用可能か判断する。

### 8.3 Character CapsuleとCollisionの関係

椅子・テーブル等のCollisionと、JenniferのCharacter Capsuleが干渉し、テレポートやEnteringSeatの位置合わせが失敗する(Actor rootをSeat Anchorまで正しく移動できない)可能性がある。**Character Capsuleが椅子Collisionと干渉する場合、Collision設定を恒久的に無効化しないこと。** 着席中(`EnteringSeat`〜`Seated`〜`LeavingSeat`の間)だけ、安全なCollision Profileへ一時的に切り替える、または必要最小限の衝突制御(例: 着席時だけCapsuleサイズを縮小する、特定のCollision Channelのみ無視する等)を検討する。Standing復帰時には、必ず元のCollision設定へ戻すこと。

### 8.4 Character Movement / Gravity / Velocityの扱い(【必須】)

JenniferがCharacter(Character Movement Componentを持つ)である場合、Seat Anchorへテレポートした後もCharacter Movementが有効なままだと、以下のような問題が起こり得る。

- 重力(Gravity)によって、着席直後に少し落ちる/床方向へ補正される
- テレポート前に残っていたVelocityによって、着席直後に位置がズレる
- Walking等の移動処理が、着席中もSeat位置を動かしてしまう

**対応方針**:

- `EnteringSeat`開始時に、現在のCharacter Movement Mode(Walking等)を保存する。**Velocityは復元対象としない**(理由は後述)
- 着席開始時、Velocityを`0`へクリアする
- 着席中(`Seated`)は、必要に応じてCharacter Movementを停止する(例: `MOVE_None`や`MOVE_Custom`への切り替え)
- `Standing`復帰時、またはScene強制切替時(11.2)には、保存しておいた元のMovement Modeのみを確実に復元する。**Velocityは復元せず、原則として`0`から開始する。**

**【重要】Velocityを保存・復元しない理由**: 「着席前のVelocityを保存し、離席時に復元する」という設計は危険である。例えば、着席直前にJenniferが何らかの速度(例: `300 cm/s`)を持っていた場合、それをそのまま復元すると、立ち上がった瞬間に古い速度のまま動き出してしまう。診断・デバッグ目的でVelocityの値をログへ記録しておくことは構わないが、**実際の復元対象には含めないこと。** 将来「ドアから歩いて椅子に座る」(セクション16)を実装する際、歩行の勢いをどう着席・離席へつなげるかは改めて別途定義する。今回のv1では、Movement Modeのみを復元し、Velocityは常に`0`として扱う。

## 9. Phase D — 各Sceneへの展開

**「全Sceneで必ず座る」ことを前提にしない。** 各Sceneに`SeatingEnabled`(そのSceneで着席対象とするかどうかのフラグ)を持たせ、実際に椅子が存在し着席させたいSceneだけを対象にする。例えば`Walk`(散歩等、屋外を想定したSceneと思われる)には、これまでの調査結果から椅子の存在が確認できていないため、無理に着席対象へ含めない。`SeatingEnabled = false`のSceneでは、Jenniferは従来通りの立位のまま、Seat Anchor関連の処理を一切実行しない。

1部屋で着席が安定してから、`SeatingEnabled = true`のScene(MyRoom / Classroom / Cinema / JenniferRoom / Restaurant等、椅子の存在が確認できたもの)へ順次展開する。各Sceneごとに椅子の座面高さ・向き・腰位置・足のクリアランスを確認し、Scene固有データとして管理する。**展開後は、11.0で定義した通り、いずれのSceneでもScene開始時からJenniferが最初から着席済みの状態で表示される。**

`ConversationScenes.ini`へ追加する場合は、既存`JenniferOffset` / `JenniferRotation`との責務を明確に分離し、Seat Anchor関連値(`SeatingEnabled`を含む)を既存の立ち位置設定へ混在させない。

### 9.1 `SeatingEnabled=true`だがSeat情報が欠損している場合の安全フォールバック(【必須】)

設定ミス等により、`SeatingEnabled = true`のSceneで、Seat Anchor・SeatOffset・着席Animation等の必要なSeat情報が実際には取得できない(未設定・破損している)場合に、**無理に着席処理を続行しないこと。**

- 必要なSeat情報が取得できない場合、Warningログを(スパムにならないよう)1回出す
- そのSceneでは着席処理を行わず、Standingへフォールバックする(従来の`JenniferAnchor`ベースの立ち位置処理をそのまま使う)
- クラッシュを起こさないこと、および原点`(0,0,0)`等の意図しない座標へJenniferが移動してしまう事故を起こさないこと

## 10. 既存機能との共存

| 機能 | 方針 | 注意 |
|---|---|---|
| 表情Morph | 維持必須 | Crimsonへ直接適用 |
| 自動まばたき | 維持必須 | Body AnimBPから独立 |
| 口パク / jawOpen | 維持必須 | Crimsonへ直接適用 |
| 歯 | 維持必須 | Crimson子Componentとの整合を確認 |
| うなずき | 維持必須 | Crimson独自head Bone制御を維持 |
| 手ジェスチャー | 原則維持 | 着席姿勢では腕角度・椅子との干渉を個別確認 |
| Lighting | 変更禁止 | 現在確定したScene別Exposure / Jennifer Lighting Channel / 専用照明を不用意に変更しない |

## 11. 状態遷移(初期版)

初期実装では最低限、以下の状態を区別する。

- Standing: 通常立位
- EnteringSeat: Seat Anchorへ位置・向きを合わせる
- Seated: 着席Pose/Animationを維持(継続保持の具体的な方式は7.1参照。Montage終了で自動的にStandingへ戻らないこと)
- LeavingSeat: 着席状態を解除して立位へ戻す

**【v1での`LeavingSeat`の実体を明確化】**: v1では自然な立ち上がりAnimation(専用のStand Up Animation)は必須としない。まず「Seated状態を解除し、Standing Poseへ復帰する」ことを優先する。具体的には、以下のいずれかを採用してよいが、実装者が座りAnimationの逆再生やRoot Motionを勝手に採用しないよう、この時点で1つに決めること。

- 座りAnimation(Montage)の再生を単純に停止し、Body AnimBPの通常のStanding/Idle Poseへ戻す(最もシンプル)
- 座りAnimationの逆再生(不採用候補。動きが不自然になりやすく、Root Motionの扱いも複雑になりやすいため、v1では推奨しない)

専用のStand Up Animationは、必要になった段階で次フェーズとして追加する。

Phase CではEnteringSeatをテレポートで実現する。歩行導入後にEnteringSeatの内部実装を経路移動へ置き換えられるよう、着席状態管理と移動方法を分離する。

### 11.0 Scene開始時のデフォルト状態(【必須】v1方針変更)

**`SeatingEnabled = true`のSceneでは、Scene開始(BeginPlay/Scene到達)時点でJenniferは最初から`Seated`状態でロードされる。** `Standing`から`EnteringSeat`(テレポート+着席Animation再生)を経由してから`Seated`へ至る、という通常の遷移フローは**Scene開始時のデフォルト経路としては使わない。**

具体的な初期化手順:

1. Scene到達時、`Seat Anchor`と`SeatOffset`を`Compose()`して算出した最終位置・向きへ、Jenniferを直接配置する(テレポート)
2. 着席Animation(Montage等)の再生を、最初から途中スキップせず「最終フレーム相当の座位Pose」または「7.1で確定した座位保持方式のPoseそのもの」を初期状態として直接適用する。**Scene開始時に、通常の`EnteringSeat`の遷移アニメーション(座る動作そのものの再生)を見せる必要はない。**
3. 状態を`Seated`として初期化する

**`EnteringSeat`/`LeavingSeat`という状態・処理自体は削除しない。** 以下の用途のために引き続き必要である。

- Phase Cでの手動テスト(実際に座る動作のアニメーションを確認する診断用途)
- セクション16で予定している「ドアから歩いて椅子に座る」機能(その際は`Standing`(入室時の立位)→歩行→`EnteringSeat`→`Seated`という、本来の遷移フローを使うことになる)

つまり、**「Scene開始時のデフォルトの入り口をどこにするか」だけが変更される**。状態遷移の仕組み自体(`Standing`/`EnteringSeat`/`Seated`/`LeavingSeat`の4状態)は維持し、将来`EnteringSeat`を使う機能が追加された際にそのまま再利用できるようにする。

`SeatingEnabled = false`のScene(例: `Walk`)は、従来通り`Standing`のまま初期化される(9章参照)。

### 11.1 姿勢遷移中のジェスチャー・うなずき割り込みへの対応(【必須】実装前に定義すること)

セクション10で「うなずき: 維持必須」「手ジェスチャー: 原則維持」としているのは、**Seated(着席完了後)の状態を指す。** EnteringSeat・LeavingSeat(姿勢が遷移している最中)に、Zキーによる手ジェスチャーや自動うなずきが割り込んだ場合の挙動は未定義であり、実装前に決めること。

- **手ジェスチャー**: `HAND_GESTURE_SPEC_v1`で確立した「進行中の再トリガーは無視し、`gesture_busy`相当の結果を返す」という考え方と同様に、EnteringSeat/LeavingSeat中の新規ジェスチャートリガーをどう扱うか(無視する/遷移完了後に実行する等)を決める
- **うなずき**: 自動まばたきと同様、うなずきはBase Poseの状態に依存しない独立設計になっているため、姿勢遷移中も継続してよいと考えられるが、EnteringSeat/LeavingSeatという大きなBody姿勢変化と同時にCrimson側でうなずきの加算が乗った場合に、見た目上不自然にならないか(例: 座ろうとしている最中に首だけ大きく動く)を目視確認する
- 座り姿勢への遷移中に手を上げる、といった動作の組み合わせが物理的に破綻しないかは、後述のテスト項目(13章)に追加する(13.1参照)

### 11.2 Scene切替時の強制リセット(【必須】)

**着席中(`Seated`/`EnteringSeat`/`LeavingSeat`のいずれの状態でも)に、数字キー等で別Sceneへ直接切り替えられた場合、次Sceneの状態へ入る前に、座りAnimation・Seat状態・一時的なOffset・8.3で一時変更したCollision設定(Collision Profile / Capsule Size / Collision Response)・8.4で保存したCharacter Movement Modeをすべて確実に元へ戻すこと(Velocityは復元せず`0`のままとする)。** C-5(離席テスト)は「Leaving Seatを経て正常に立位へ戻る」場合のみを想定しているが、Scene切替はこの正常な離席フローを経由せずに割り込む可能性があるため、別途の強制リセット経路が必要になる。**Collision・Movement Modeの復元漏れは、次Sceneへ縮小したCapsuleや停止したMovementのまま持ち越される等、気づきにくい不具合につながるため、他の状態と同列に必ず含めること。**

- Scene切替のトリガーを受け取った時点で、現在のSeat状態が`Standing`以外であれば、着席Animation・Seat Anchorへの追従・一時Offset・一時変更したCollision設定・Character Movement状態をすべて即座にクリア・復元してから次Sceneのテレポート処理へ進む
- この強制リセットは、通常の`LeavingSeat`(アニメーションを伴う自然な離席)とは別の、緊急パス(即座に立位相当の状態へ戻す)として実装してよい

## 12. 安全条件 / 禁止事項

- Phase A完了前に座りAnimationを本実装しない
- CrimsonとMetaHuman BodyをLeader Poseで無理に接続しない
- 座り追従だけのためにCrimsonをMetaHuman Skeletonへ再Rigしない
- 既存の表情・まばたき・口パク・うなずき所有権を変更しない
- 現在確定したLighting / Exposure設定を着席実装の都合で変更しない
- 座り位置調整と歩行/NavMesh実装を同じ工程で行わない
- Root Motionを初期着席位置決めに使用しない
- 問題発生時に複数要因を同時変更せず、Phase単位で原因を切り分ける

## 13. テスト項目

| ID | テスト | 合格条件 |
|---|---|---|
| A-1 | 立位Attach前後 | 顔位置・回転・Scaleが変わらない |
| A-2 | Body head診断移動 | Crimson全体がheadへ追従 |
| A-3 | 顔機能 | 表情・blink・jawOpen・nodが正常 |
| A-4 | Body head回転とCrimson nodの二重回転 | Body headが回転している状態でうなずき(Crimson独自head)が発生しても、回転が過剰に重なって不自然にならない(Body head回転 + Crimson nod加算の組み合わせを目視確認) |
| B-1 | 着席Animation単体 | Body全身が自然な座位になる |
| B-2 | 座位の継続保持 | 着席Animation再生完了後も座位Poseが維持され、一定時間放置してもStandingへ自動的に戻らない |
| C-0 | Scene開始時の初期着席 | `SeatingEnabled=true`のSceneをBeginPlay/Scene到達させた時点で、EnteringSeatのアニメーション再生を経ずに、Jenniferが最初から`Seated`状態(Seat Anchor+SeatOffsetの位置・座位Pose)で表示される(11.0) |
| C-1 | Seat Anchor | 腰が座面に一致し、めり込みが許容範囲 |
| C-1b | Capsule/Collision | 椅子・テーブル等のCollisionとCharacter Capsuleが干渉してテレポート・位置合わせが失敗しない。Standing復帰時にCollision設定が元通りになっている(8.3) |
| C-1c | Movement/Gravity/Velocity | 着席後、数秒間放置してもGravity・残留Velocity・Walking等の移動処理によってActor rootがSeat Anchorからドリフトしない(8.4) |
| C-2 | 顔追従 | 座位でもBody首位置とCrimsonが分離しない |
| C-3 | 複合動作 | 座位で表情・blink・lip-sync・nodが正常 |
| C-4 | 手ジェスチャー | 椅子/体との大きな干渉がない |
| C-5 | 離席 | 立位へ戻した際にOffset/状態が残留しない。保存したCharacter Movement Modeが正常に復元される(Velocityは復元されず`0`から開始する)(8.4) |
| D-1 | Scene切替 | `SeatingEnabled = true`の各Sceneで、到達時点から最初から着席済みの状態(C-0と同様)で配置される。`SeatingEnabled = false`のScene(例: Walk)では従来通り立位のままで、Seat Anchor関連の処理が一切実行されない |
| D-2 | Seat情報欠損時のフォールバック | `SeatingEnabled = true`だがSeat Anchor/SeatOffset/着席Animation等が意図的に欠損した状態を作り、Standingへ安全にフォールバックすること(クラッシュ・原点移動が起きない)を確認する(9.1) |

### 13.1 姿勢遷移中の割り込みテスト

| ID | テスト | 合格条件 |
|---|---|---|
| C-6 | 遷移中のジェスチャー割り込み | EnteringSeat/LeavingSeat中にZキーでジェスチャーを実行しても、11.1で定めた挙動通りに動作し、姿勢が破綻しない |
| C-7 | 遷移中のうなずき | EnteringSeat/LeavingSeat中に自動うなずきが発生しても、不自然な動きにならない(目視確認) |
| C-8 | Scene切替による強制リセット | `Seated`/`EnteringSeat`/`LeavingSeat`のいずれかの状態で別Sceneへ直接切り替えても、次Scene到達後に座りAnimation・Seat状態・一時Offset・一時変更したCollision設定(Profile/Capsule Size/Response)が残留しておらず、Character Movement Modeが正しく復元され、Velocityが古い値のまま持ち越されていない(0から開始する)(11.2) |

## 14. 実装順序

1. Body head Socket/Bone追従の最小テスト
2. 既存顔機能の回帰確認
3. **Body AnimInstanceが実際に`None`かどうか、その原因を確認する(4.1参照。この結果次第でPhase Bの設計自体を見直す可能性がある)**
4. Body AnimBP / AnimGraphにMontage用Slotが存在するか確認し、安全な再生経路を設計する(7.1)
5. 上記4で確定した経路に合わせて、座りAnimationアセットを用意する
6. 1部屋・1脚のSeat Anchor作成
7. テレポート着席テスト
8. 座位での顔追従・表情・口パク・うなずき確認
9. 手ジェスチャーの干渉確認・必要最小限の調整
10. 姿勢遷移中(EnteringSeat/LeavingSeat)のジェスチャー・うなずき割り込みを確認する(11.1、13.1)
11. 各SceneへSeat Anchor展開
12. 全Scene回帰テスト
13. 別仕様として『ドアから歩いて椅子へ座る』へ進む

## 15. 完了条件

各対象SceneでJenniferがScene開始時点から最初から指定椅子へ正しい位置・向きで着席した状態になっており(11.0)、BodyとCrimsonが分離せず、表情・まばたき・口パク・うなずきが従来通り動作すること。着席/離席によるTransformやAnimation状態の残留がなく、Scene切替後も安定すること。

## 16. 次工程(別仕様)

本仕様完了後、ドアからSeat Anchorまでの連続歩行を別仕様として設計する。NavMesh / AI Move Toまたは固定経路、歩行Animation、到着判定、着席への遷移を対象とする。着席状態管理とSeat Anchorは本仕様の成果を再利用する。
