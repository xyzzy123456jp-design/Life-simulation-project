# シーンスケール補正仕様 (SCENE_SCALE_SPEC_v1.md)

> v1: (Claude作成) 部屋を切り替える(車内↔Cigar Room等)たびにJenniferのサイズが背景に対して合わなくなる問題への対応方針を仕様化。
>   経緯: 当初は「背景にあわせてJenniferの大きさを自動調整する」という案で検討を始めたが、UE5は1 Unreal Unit = 1cmを基準にレベルを作る前提であり、Jenniferの身長はどのシーンでも変わらないのが自然という指摘を受け、「Jennifer側を毎回調整する」のではなく「まずどちら側の実寸がズレているか診断し、ズレている側(基本的には背景側)にだけScale補正をかける」方針に転換した。あわせて、Skeletal CharacterであるJennifer自身のActor Scaleをシーンごとに変える設計はIK・AnimBP・コリジョンへの副作用リスクがあるため避け、背景(レベルインスタンス)側の`Scale3D`を補正する設計とした。
> - [チャッピー指摘→反映] `SetActorScale3D(FVector(SceneScale))`がDataTableの値をそのまま最終Scaleとしてセットしており、レベルインスタンスが元々`(1.2,1.2,1.2)`等の意図的なScaleを持っていた場合にそれを上書きしてしまう問題を修正。`BaseScale`(元のScale、一度だけキャプチャ)× `CorrectionScale`(DataTableの補正倍率)で最終Scaleを算出する方式に変更した(セクション3)。
>   理由: DataTableの値を「最終Scale」ではなく「補正倍率」として扱わないと、既存の意図的なScale設定を壊し、また往復のたびに再適用しても値が正しく戻らない可能性があるため。
> - [チャッピー指摘→反映] Level InstanceのScaleがPivot基準で行われるため、Scale変更によって床面・入口・Spawn位置等の基準AnchorのWorld座標がズレる可能性を確認する項目(セクション3.5)を追加した。
>   理由: Level InstanceのTransformは内部コンテンツにもそのまま適用されるため、部屋のPivotが中央付近にある場合、Scale変更で壁や家具がPivotへ寄り、Jenniferとの大きさは合っても床の高さや入口の位置がズレる可能性があるため。
> - [チャッピー指摘→反映] 対象レベルインスタンス内部にTrigger・Spawn Point・Blocking Volume等のGameplay Actorが混在していないか確認する項目(セクション3.6)を追加した。
>   理由: 「静的メッシュ主体なら副作用は起きにくい」という前提を検証しないまま進めると、部屋の見た目だけを縮小したつもりが、トリガーや座る位置まで動いてしまう可能性があるため。
> - [チャッピー指摘→反映] 診断基準を「165〜175cm程度の妥当な範囲」という曖昧な目安から、「JenniferのCanonical Height(基準身長)を1つ決め、それとの比較で判定する」方式に変更した。あわせて、Bounding Boxではなく直立リファレンスポーズでの実測を推奨し、`Alt`ドラッグという操作指定は具体的すぎ複製操作と紛らわしいため「UE Editorの距離計測機能、またはReference Cubeとの比較」という表現に修正した。
>   理由: Jenniferの設定身長が160cmでも180cmでもそれ自体はバグではなく、固定の数値レンジで判定する基準は妥当ではないため。
> - [チャッピー指摘→反映] `BaseScale`をSceneManager側に単一の`FVector`として持つ設計だったため、Vehicle用・CigarRoom用など複数のレベルインスタンスを行き来すると、後にキャプチャした方のBaseScaleでもう片方を上書きしてしまう問題を修正。`TMap<TWeakObjectPtr<ALevelInstance>, FVector> BaseScaleByInstance`でレベルインスタンスごとに個別管理する方式に変更した。
>   理由: 単一変数だと、例えばVehicle(BaseScale=1.0)とCigarRoom(BaseScale=1.2)が別Actorの場合、最後にキャプチャした方の値で他方まで上書きされ、往復すると誤ったBaseScaleが使われるバグになるため。
> - [チャッピー指摘→反映] レベルインスタンスが破棄→再生成される実装だった場合、古いActorに紐づくBaseScaleを再利用してはならない旨を明記した。`TMap`のキーをActorポインタにすることで、再生成された新しいActorは自動的に別エントリとして扱われ、`CaptureBaseScale()`の再呼び出しが必要になる設計にした。
>   理由: 仕様自身がセクション3で「表示/非表示なのか、生成/破棄なのか確認する」としている以上、生成/破棄のケースでの挙動を定義しないままにしておくと実装時に矛盾が生じるため。
> - [チャッピー指摘→反映] `CorrectionScale`(例: Cigar Roomの`0.82`)の算出方法が未定義だったため、`CorrectionScale = DesiredRealWorldSize / CurrentMeasuredSize`という式を明記し、ドア高等の実測値から算出する具体例を追加した。あわせて、1つの基準オブジェクトだけでなく2〜3個で倍率を照合することを推奨した。
>   理由: 数値の出所が仕様書に書かれていないと、新しい背景シーンを追加するたびに毎回どう補正倍率を決めればいいか再現できないため。
> - [チャッピー指摘→反映] 設計方針(セクション1)と合格条件(セクション6)で、Jenniferの基準Actor Scaleを「常に`1,1,1`」と断定していた表現を、「Canonicalな基準値からシーンによって変化しない」という表現に修正した。
>   理由: 将来Jenniferの正しい基準Actor Scaleが`1,1,1`以外(例: `0.98`)になった場合に、`1,1,1`固定という記述自体が矛盾を生むため。要件の本質は特定の数値ではなく、シーンに応じて変化しないことにあるため。
> - [チャッピー指摘→反映] `BaseScaleByInstance`のキーに`TWeakObjectPtr<ALevelInstance>`を使っていたが、Epic公式ドキュメント上`TWeakObjectPtr`は`TMap`のキーとして未サポートのため、`TObjectKey<ALevelInstance>`に変更した。あわせて「弱参照だからActor破棄時に自然に無効化される」という誤った前提の説明を削除し、破棄時に明示的に`OnLevelInstanceDestroyed()`でエントリを`Remove`する設計、およびSceneManager終了・Level変更時に`Empty()`する設計を追加した。
>   理由: `TWeakObjectPtr`をTMapキーに使うこと自体がEpic公式ドキュメント上サポートされておらず、単なる改善ではなく修正が必須の問題だったため。
> - [チャッピー指摘→反映] 「Level Instance ActorをランタイムでScaleすると内部コンテンツまで期待どおり変わる」という本仕様全体の前提を、実機で検証しないまま設計を進めていたため、新設のステップ0.5として、PIE中にCigar Room Level InstanceのScaleを一時的に変更し、壁・床・家具・Collision・ストリーミング後の保持まで確認するGo/No-Goテストを追加した。
>   理由: この前提が崩れる場合、Level Instance全体をランタイムでScaleするという設計自体を見直す必要があり、後工程で発覚すると手戻りが大きいため、実装着手前に必ず確認すべき事項だったため。
> - [チャッピー指摘→反映] DataTableの列名が`Scale`のままだったため、意味を明確にするため`CorrectionScale`に変更した。
>   理由: `Scale`という列名のままだと、値が最終Scaleであるかのように誤解され、`SetActorScale3D(FVector(Row.Scale))`のようにセクション3で既に修正した「最終値として直接セットする」バグを、DataTableの列名から復活させてしまうリスクがあるため。
> - [Claude指摘→反映] `GetSceneScaneForScene()`という関数名のtypo(正しくは`GetSceneScaleForScene()`)が1箇所だけ残っており、他のコード例と表記が食い違っていたため修正した。
> - [Claude指摘→反映] `CaptureBaseScale()`が呼ばれるタイミングがMキーでの切り替え(ロード・生成)時のみを想定しており、ゲーム開始時に既に表示されているシーン(通常は車内)側のレベルインスタンスが同じイベントを経由しない可能性を見落としていたため、セクション3の確認事項に追加し、`BeginPlay`等で表示中の全シーンに`CaptureBaseScale()`を呼ぶ旨を明記した。
>   理由: 補正倍率が`1.0`のシーンでは「未キャプチャで何もしない」結果と「正しく補正した」結果が偶然一致するため見た目上は問題が起きにくいが、将来そのシーンにも補正が必要になった場合にサイレントに効かなくなるリスクがあるため。
> - [チャッピー指摘→反映] ステップ0.5のGo/No-Goテストが絶対値`1.0 → 0.8`で固定されていたため、対象レベルインスタンスの元Scaleが`1.0`以外(例: `1.2`)だった場合にテストの意味が変わってしまう問題を修正。`TestScale = CurrentScale × 0.8`という相対倍率でテストする方式に変更した。
>   理由: 本仕様がセクション3で導入した`BaseScale × CorrectionScale`という考え方と、絶対値でのテストが矛盾していたため。
> - [チャッピー指摘→反映] `OnLevelInstanceDestroyed()`を「破棄直後」に呼ぶという表現が、対象Actorのポインタが既に無効化された後を指しうる危険な表現だったため、「破棄直前(対象Actorをまだ有効なポインタとして識別できるタイミング)」に呼ぶ、という表現に修正した。
>   理由: 「破棄した後にRemoveを試みる」という順序は、破棄済みポインタを扱うリスクがあり、「Removeしてから破棄する」の順序であるべきだったため。
> - [チャッピー指摘→反映] 既にコード例として存在していた`OnSceneManagerShutdownOrLevelChange()`(`BaseScaleByInstance.Empty()`)が、変更対象ファイル一覧・実装順序・合格条件のいずれにも反映されていなかったため、3箇所すべてに追加した。
>   理由: コード例にあるのに変更対象ファイル一覧や合格条件から漏れていると、実装時・テスト時にこの処理の実装自体が見落とされるリスクがあるため。

このドキュメントは、複数の背景シーン(車内・Cigar Room・今後追加予定の背景)の間で、Jenniferと背景の実寸スケールを合わせるための仕様。
Claude Codeが実装に着手できるよう、既存のMキーシーン切り替え処理を踏まえて具体化する。

## ステップ0(実装前に必ず行うこと): どちら側の実寸がズレているか診断する

**この診断を飛ばして実装を始めてはならない。** 診断結果次第で、直すべき場所(Jennifer側かシーン側か)が変わる。

**確認方法**

**【矛盾修正】以前の版では「Jenniferの実測身長がおおよそ165〜175cm程度の妥当な範囲に収まっているか」を判定基準としていたが、これは曖昧な目安に過ぎない。Jenniferの設定身長が仮に160cmや180cmだったとしても、それ自体はバグではない。判定の前に、まずプロジェクトとして「Jenniferの正しい基準身長」を1つ決めておく必要がある。**

0. **JenniferのCanonical Height(基準身長)を1つ決める**(例: `JenniferCanonicalHeight = 168cm`。実際の値はキャラクター設定・MetaHumanのベースBodyタイプ等から確認して決定する)。以降の判定はこの基準値との比較で行う

1. Jenniferの実測身長をUE上で測る
   - コンテンツブラウザでJenniferのSkeletal MeshアセットまたはBP上でBounding Boxの高さを確認する。ただし、髪・靴・アクセサリーやポーズの影響でBounding Boxだけでは正確な身長にならない場合があるため、可能であれば**直立のリファレンスポーズで、足裏から頭頂までの距離**を測ることを推奨する
   - UE Editorの距離計測機能(Editorのメジャーツール)、または100cmのReference Cube(一時的に配置する既知サイズのオブジェクト)と並べて目視比較する方法でもよい
2. 車内シーンで、シート・ステアリングホイール・ドア等とJenniferの比率が現状どう見えているか確認する(「車内では自然」という前提自体も、思い込みで済ませず今回改めて目視確認する)
3. Cigar Roomシーンで、ドア高・椅子の座面高・テーブル高など、実寸がある程度分かるオブジェクトとJenniferの比率を比較する
4. 判定する
   - Jenniferの実測身長が、0で決めたCanonical Heightとおおよそ一致している場合(数cm程度の誤差は許容) → **Cigar Room(または問題のあるシーン)側の実寸がズレている**と判断し、「1. シーン側のScale補正」に進む
   - Jenniferの実測身長がCanonical Heightから明らかに逸脱している場合(例: Canonical Heightが168cmなのに実測が215cmや110cm等) → **Jennifer側の実寸を先に直す**。MetaHuman化時のフィッティングScale、Blenderでの軽量メッシュ書き出し時のApply Scale設定、FBXインポート時のImport Uniform Scale設定を確認する。この場合、本仕様(シーン側のScale補正)は本来の問題を隠すだけの対症療法になるため、Jennifer側の修正を優先する

この診断結果を`PROGRESS.md`または開発記録に記録してから、以降のセクションへ進むこと。

## ステップ0.5(実装前に必ず行うこと): Level InstanceのランタイムScale変更が実際に機能するかのGo/No-Goテスト

**本仕様全体は「Level Instance Actorへ`SetActorScale3D()`すると、内部コンテンツ(壁・家具・床・Collision等)までPIE中に期待どおり追従する」という前提の上に成立している。この前提自体を実機で確認せずに実装を進めてはならない。**

Epic公式ドキュメント上、Level Instance Actorへ適用したTransformがそのLevel Instance内部のコンテンツへ適用されることは明記されているが、今回のプロジェクトが使っているLevel Instanceのストリーミング方式・Actor構成・Collision設定まで含めて、ランタイム中のScale変更が期待どおり動作するかはこの仕様書だけでは確認できていない。

**テスト手順**

**【矛盾修正】以前の版では、Scaleを絶対値で`1.0 → 0.8`に変更するとしていたが、これは本仕様がセクション3で導入した`BaseScale × CorrectionScale`という考え方と矛盾する。対象レベルインスタンスの元Scaleが既に`1.0`以外(例: `1.2`)だった場合、絶対値`0.8`に変更すると「0.8倍のテスト」ではなく「実質`0.8 / 1.2 ≈ 0.667`倍のテスト」になってしまい、テスト結果の意味が変わる。以下のとおり、現在のScaleに対する相対倍率でテストする。**

1. PIE中に、対象のCigar Room Level Instance Actorを選択し(Outliner等から)、**現在のScale(`CurrentScale`)を確認する**(例: `1.0`や`1.2`など、その時点で設定されている値)
2. 詳細パネルまたはコンソールから、Scaleを`TestScale = CurrentScale × 0.8`へ一時的に変更する(例: `CurrentScale = 1.2`なら`TestScale = 0.96`)
3. 以下を確認する
   - 壁・床・家具の見た目が実際に縮む(内部コンテンツへ反映される)
   - Collisionも見た目と一致してScaleされる(壁を通り抜けられたり、逆に見えない壁に当たったりしない)
   - 床の位置が意図せず浮いたり沈んだりしない
   - レベルインスタンスがストリーミング(非表示→表示)を経ても、変更したScaleが保持される

**この結果が「Go(期待どおり動く)」の場合のみ、以降のセクション(1〜7)の設計を実装する。「No-Go(期待どおり動かない)」の場合は、Level Instance全体をランタイムでScaleするという本仕様の設計自体を見直す必要がある**(例えば、レベルインスタンス化せず個別Static Meshアクター群として配置し直す、Scale変更を実行時ではなくエディタ時の一度きりの調整に限定する、等の代替案を検討する)。

このテスト結果も、ステップ0の診断結果と同様に`PROGRESS.md`または開発記録に記録すること。

## 1. 設計方針: 背景側にScale補正をかける、Jennifer側は変更しない

人の身長はどのシーンに入っても変わらないため、Jennifer(Skeletal Character)自身の`Actor Scale`は、シーンによらず常にCanonicalな基準値(通常`1,1,1`)のまま一定に保つ。基準値自体は将来変更されうるため、要件の本質は「特定の数値に固定すること」ではなく「シーンによって値が変化しないこと」である。

その代わり、実寸がズレていると判明したシーンの**レベルインスタンスActor側の`Scale3D`**を補正する。

理由:

- Skeletal CharacterのActor Scaleをシーンごとに変更すると、IK・AnimBP(特にFoot IK、Look At等)・コリジョンに副作用が出るリスクがある
- 一方、背景の静的メッシュ主体のレベルインスタンスをScaleしても、通常はそのような副作用は起きにくい
- 「Jenniferの身長は常に一定」という設計の方が、将来的な機能追加(服のフィッティング、他キャラクターとの身長比較演出等)との相性も良い

## 2. シーンスケール補正値の管理

表情システムの係数(FACIAL_EXPRESSION_SPEC参照)と同じ考え方で、シーンごとの補正値をC++へ直書きせず、`UDataTable`(またはシンプルな`TMap<FName, float>`)で管理する。

```
SceneName,   CorrectionScale
Vehicle,     1.00
CigarRoom,   0.84
Restaurant,  0.94
```

**【矛盾修正】以前の版では、列名が`Scale`のままだった。この値は最終Scaleではなく「元のScaleに掛ける補正倍率」であるにもかかわらず、列名が`Scale`だと、実装時に`SetActorScale3D(FVector(Row.Scale))`のように最終値として直接セットしてしまう(セクション3で既に修正した昔のバグを、DataTableの列名から誤って復活させる)リスクがある。列名を`CorrectionScale`に変更し、意味を明確にする。**

**【矛盾修正】以前の版では、Cigar Roomの補正倍率(`0.82`)がどう求まった数値なのかが未定義だった。以下のとおり算出式を明記する。**

```text
CorrectionScale = DesiredRealWorldSize / CurrentMeasuredSize
```

例えば、Cigar Roomのドア高がUE上で実測`250cm`(`CurrentMeasuredSize`)だったが、本来のドア高が`210cm`(`DesiredRealWorldSize`、一般的なドアの高さ)であるべきなら、

```text
CorrectionScale = 210 / 250 = 0.84
```

となる。家具1個だけでは、その元アセット自体のデザイン上の寸法が特殊な場合(装飾的に大きい椅子等)もあるため、**ドア高・椅子の座面高・テーブル高など2〜3個の基準オブジェクトを測り、算出される倍率がおおよそ一致することを確認**した上でDataTableへ登録する。大きくばらつく場合は、シーン全体が均一にScaleされていない(部分的に手動調整されたアセットが混在している等)可能性があるため、その原因を先に調べる。

```cpp
// このコード自体は補正倍率の具体的な数値を一切知らない。数値はすべてDataTableアセット側にある。
const float CorrectionScale = GetSceneScaleForScene(CurrentSceneName); // DataTable参照、見つからない場合は1.0を返す
```

新しい背景シーンを追加するたびに、ステップ0と同じ診断(実測比較)を行い、DataTableへ1行追加する運用とする。

## 3. シーン切り替え処理への統合(【必須】実装前に確認すること)

**実装前に必ず確認すること**

1. 現在Mキーでのシーン切り替え(車内↔Cigar Room)を担当しているクラス・関数を特定する(`PROGRESS.md`、または実際にコードを検索して確認する)
2. そのシーン切り替え処理が、レベルインスタンスの表示/非表示切り替えなのか、Actorの生成/破棄なのか、あるいは単純な可視性切り替えなのかを確認する
3. 切り替えのどのタイミングでScale補正を適用すべきかを、既存の切り替え処理の構造に合わせて決める(例: レベルインスタンスをロード・表示する直後)
4. **ゲーム開始時に既に表示されているシーン(例: 車内)側のレベルインスタンスが、Mキーでの切り替え処理と同じ「ロード・生成」イベントを経由するか確認する。**経由しない場合(最初から常に配置されているだけの場合)、そのレベルインスタンスは一度も`CaptureBaseScale()`が呼ばれないまま`ApplySceneScale()`が呼ばれることになる

Scale適用は、対象レベルインスタンスActorに対して行う。

**【矛盾修正】以前の版では`SetActorScale3D(FVector(SceneScale))`でDataTableの値をそのまま最終Scaleとして直接セットしていた。これは、対象レベルインスタンスが最初から`Scale = (1.2, 1.2, 1.2)`のように意図的なScaleを持っていた場合、その値ごとDataTableの補正値(例: `0.82`)で上書きしてしまう。以下のとおり、DataTableの値は「最終Scale」ではなく「元のScaleに掛ける補正倍率」として扱う。**

**【矛盾修正2】さらに、`BaseScale`をSceneManager側に1個(`FVector BaseScale`)だけ持つ実装は、複数シーンで破綻する。例えばVehicle用レベルインスタンスの`BaseScale = 1.0`とCigarRoom用レベルインスタンスの`BaseScale = 1.2`が別Actorだった場合、最後にキャプチャした方(例えばCigarRoomの`1.2`)で上書きされてしまい、Vehicleへ戻った時にも誤って`1.2`が使われる。`BaseScale`は単一の変数ではなく、レベルインスタンス(またはシーン名)ごとに`TMap`で保持する。**

**【矛盾修正3】以前の版では、このTMapのキーに`TWeakObjectPtr<ALevelInstance>`を使っていたが、Epic公式ドキュメント上、`TWeakObjectPtr`は`TMap`のキーとしてサポートされない。UObjectをキーにする場合は`TObjectKey`を使う。あわせて、「Actorが破棄されれば弱参照側から自然に無効化される」という説明も`TWeakObjectPtr`前提の誤りだったため、破棄時に明示的にエントリを`Remove`する設計に修正する。**

```cpp
// レベルインスタンス単位でBaseScaleを保持する。
// UObjectをTMapのキーにする場合はTObjectKeyを使う(TWeakObjectPtrはキーとして未サポート)。
UPROPERTY()
TMap<TObjectKey<ALevelInstance>, FVector> BaseScaleByInstance;

void UYourSceneManager::CaptureBaseScale(ALevelInstance* TargetLevelInstance)
{
    if (!TargetLevelInstance)
    {
        return;
    }

    // 【矛盾修正2】既にこのレベルインスタンスのBaseScaleをキャプチャ済みなら、
    // 再キャプチャしない(既に補正済みのScaleを「元のScale」として誤って
    // 記録してしまうと、次回以降の補正が二重にかかってしまう)。
    // ただし、レベルインスタンスが破棄→再生成される実装の場合、新しいActorの
    // ポインタは古いポインタと異なるため、TMapには自動的に新しいエントリとして
    // 追加される(＝新しいActorについては必ず再キャプチャが行われる)。
    const TObjectKey<ALevelInstance> Key(TargetLevelInstance);
    if (BaseScaleByInstance.Contains(Key))
    {
        return;
    }

    // レベルインスタンスが元々持っていた意図的なScaleを記録する。
    BaseScaleByInstance.Add(Key, TargetLevelInstance->GetActorScale3D());
}

void UYourSceneManager::ApplySceneScale(ALevelInstance* TargetLevelInstance, FName SceneName)
{
    if (!TargetLevelInstance)
    {
        return;
    }

    const TObjectKey<ALevelInstance> Key(TargetLevelInstance);
    const FVector* FoundBaseScale = BaseScaleByInstance.Find(Key);
    if (!FoundBaseScale)
    {
        // CaptureBaseScale()を先に呼び忘れている、または該当レベルインスタンスが
        // 破棄・再生成されて古いキーのエントリしか残っていない状態。
        UE_LOG(LogTemp, Warning, TEXT("[SCENE_SCALE] BaseScale未キャプチャのレベルインスタンスへ補正を試みました"));
        return;
    }

    const float CorrectionScale = GetSceneScaleForScene(SceneName); // DataTable参照、見つからない場合は1.0を返す
    const FVector CorrectedScale = (*FoundBaseScale) * CorrectionScale;
    TargetLevelInstance->SetActorScale3D(CorrectedScale);
}

// 【矛盾修正】「破棄直後」に呼ぶという表現は危険。Actorが実際に破棄された後では
// そのポインタ(DestroyedLevelInstance)は既に無効になっている可能性があり、
// TObjectKeyの構築や比較が正しく行えない/意味を失うリスクがある。
// 正しくは「破棄直前」または「対象Actorをまだ有効なポインタとして識別できる
// タイミング」(例: AActor::Destroyed()のオーバーライド内、EndPlay()、または
// レベルインスタンスをアンロードする直前の既存コード箇所)で呼ぶこと。
// 順序としては「BaseScaleByInstanceからRemove → その後にActorを実際に破棄する」
// が安全であり、「Actorを破棄 → 破棄後にRemoveを試みる」の順にしない。
void UYourSceneManager::OnLevelInstanceDestroyed(ALevelInstance* LevelInstanceAboutToBeDestroyed)
{
    if (!LevelInstanceAboutToBeDestroyed)
    {
        return;
    }
    BaseScaleByInstance.Remove(TObjectKey<ALevelInstance>(LevelInstanceAboutToBeDestroyed));
}

// SceneManager自体の終了時、またはLevel(マップ)切り替え時に、
// 前のLevelに属していたレベルインスタンスのエントリを一括で破棄する。
void UYourSceneManager::OnSceneManagerShutdownOrLevelChange()
{
    BaseScaleByInstance.Empty();
}
```

`BaseScaleByInstance`は必ずシーン切り替えロジック導入前(既存の意図的なScaleがまだ何もいじられていない状態)に、対象レベルインスタンスごとに一度だけキャプチャすること。これにより、Mキーで何度往復しても補正倍率が累積せず、常に「そのレベルインスタンス固有の元Scale × 診断で求めた補正倍率」に収束する(セクション6の合格条件3と対応)。

**レベルインスタンスが表示/非表示の切り替えではなく、シーン切り替えのたびに破棄→再生成される実装だった場合は特に注意する**(セクション3の確認事項2と対応)。再生成された新しいActorは古いActorとは別のポインタになるため、`BaseScaleByInstance`には新しいエントリとして扱われ、`CaptureBaseScale()`を再度呼ぶ必要がある。同時に、破棄された旧Actorのエントリは`TObjectKey`が弱参照ではない以上自動的には消えないため、`OnLevelInstanceDestroyed()`を**破棄直前(対象Actorをまだ有効なポインタとして識別できるタイミング。例: `AActor::Destroyed()`のオーバーライド内や`EndPlay()`)** に必ず呼び、古いエントリが`BaseScaleByInstance`に溜まり続けないようにする。「破棄した後にRemoveを試みる」の順にはしないこと。この呼び出し漏れがあると`ApplySceneScale()`が`BaseScale未キャプチャ`のWarningを出して補正をスキップしてしまうため、シーン切り替え処理の中で「レベルインスタンスをロード・生成した直後は必ず`CaptureBaseScale()`を呼ぶ」ことを徹底する。

**【矛盾修正4】ゲーム開始時に既に表示されているシーン(通常は車内)は、Mキーでの切り替え処理と同じ「ロード・生成」イベントを経由しない可能性がある(セクション3の確認事項4)。この場合、そのレベルインスタンスは一度も`CaptureBaseScale()`が呼ばれないまま補正対象になり得る。補正倍率が`1.0`のシーンでは「何もしない」結果と「正しく補正した」結果が偶然一致するため見た目上は問題が起きにくいが、将来そのシーンにも意図的な補正が必要になった場合にサイレントに効かなくなる。`BeginPlay`等のゲーム開始処理で、その時点で表示されている全てのシーン用レベルインスタンスに対し、明示的に`CaptureBaseScale()`を呼ぶこと。**

`GetSceneScaleForScene()`という名前は例であり、既存の設定管理・DataTable読み込みの共通処理があればそれを優先する。

## 3.5 Scale適用時のPivot/Anchor位置(【必須】実装前に確認すること)

Level InstanceのTransformは、Epicの仕様上、内部コンテンツ(壁・家具・床等)にもそのまま適用される。これが本仕様の前提(レベルインスタンス単位でScaleできる)を成立させている根拠でもあるが、同時にリスクでもある。

Level InstanceのScaleはそのActorのPivot基準で行われるため、例えば部屋のPivotが部屋の中央付近にある場合、`Scale 1.0 → 0.82`にすると壁や家具がPivot(中央)へ向かって縮む。これにより、Jenniferと部屋の大きさの比率は合っても、**床の高さ・入口の位置・車から部屋へ移動した際のSpawn位置**が、Scale変更前後でズレる可能性がある。

**実装前に必ず確認すること**

1. 対象レベルインスタンスのPivot位置が、床面や部屋の隅など、意味のある基準点にあるか確認する(部屋の中央にある場合、Scale変更で床面が浮く/沈む可能性が高い)
2. Scale適用後、床面・入口・Jennifer/プレイヤーのSpawn位置などの基準Anchorが、Scale変更前とWorld座標上で一致しているか実機で確認する
3. ズレが確認された場合、以下のいずれかで対応する
   - レベルインスタンスのPivotを、床面などScaleの影響を受けたくない基準点に設定し直す
   - または、Scale適用後に基準Anchor(例: 床の一点)のWorld座標が変わらないよう、Scale変更と同時にレベルインスタンスの位置(Location)を補正する

必要であれば、シーンごとに`SceneScaleAnchor`(基準点を表すデータ)を持たせ、Scale変更後にその基準点のWorld位置が変わらないよう位置補正する設計にする。

## 3.6 Level Instance内部のGameplay Actorの扱い(【必須】実装前に確認すること)

「背景の静的メッシュ主体のレベルインスタンスなら副作用は起きにくい」という前提(セクション1)自体は妥当だが、実際にCigar RoomのLevel Instance内に何が含まれているかは確認していない。Level Instanceには、Static Mesh・Lightだけでなく、Collision/Blocking Volume、Trigger、Audio、Niagara、Spawn Point、椅子等のInteraction Actorが混在している可能性がある。Level InstanceのTransformは内部コンテンツへそのまま反映されるため、「部屋の見た目だけを0.82倍にしたつもりが、トリガーや座る位置まで動いてしまう」ことが起こり得る。

**実装前に必ず確認すること**

1. 対象レベルインスタンス(Cigar Room等)を開き、内部のActor一覧を確認する
2. Static Mesh・家具・装飾以外に、Scaleしてはいけないゲームプレイ用Actor(Trigger、Spawn Point、Blocking Volume、Interaction Actor等)が含まれていないか確認する
3. 混在している場合、レベルインスタンス内を以下のように分離することを検討する

```text
EnvironmentRoot(Scale対象)
 ├─ Static Mesh
 ├─ Furniture
 └─ Decoration

Gameplay系(Scale対象外、別階層またはメインレベル側に配置)
 ├─ Trigger
 ├─ Spawn Point
 └─ Interaction Actor
```

分離が難しい場合は、レベルインスタンス全体をScaleする本仕様の方式ではなく、視覚的な家具・建材アセット単位でのScale調整など、別のアプローチを検討する必要がある(その場合は本仕様を見直すこと)。

## 4. 見た目の急激な変化を避ける(推奨)

シーン切り替え自体が既にフェードやカット演出を伴っている場合は、そのタイミングでScaleを一度に適用してしまって問題ない(切り替え演出中は見えないため、瞬間的なScale変化は気にならない)。

フェード等を伴わない即時切り替えの場合、Scale変化が視覚的に急に見える可能性がある。気になる場合は、表情の補間(FACIAL_EXPRESSION_SPEC セクション10)と同じ考え方で、数百ms〜1秒程度で`FMath::FInterpTo`によりScaleを緩やかに遷移させることを検討する。ただし、これは必須要件ではなく、実機で違和感が出た場合にのみ対応する。

## 5. 変更対象ファイルまとめ

| ファイル | 変更内容 |
|---|---|
| シーン切り替えを担当する既存クラス | Scale適用処理の呼び出しを追加(ステップ3で特定した箇所) |
| `SceneScaleDataTable`(新規DataTableアセット) | シーンごとの補正値を追加 |
| `SceneScale`関連のC++(新規、または既存Manager内) | `GetSceneScaleForScene()`、`CaptureBaseScale()`、`ApplySceneScale()`、`OnLevelInstanceDestroyed()`、`OnSceneManagerShutdownOrLevelChange()`、`BaseScaleByInstance`(レベルインスタンスごとのBaseScale管理)を追加 |

## 6. 動作確認・合格条件

1. 車内シーンでは、Scale補正が`1.0`のまま(見た目上変化なし)であることを確認する
2. Cigar Roomシーンに切り替えた際、診断(ステップ0)で判定した補正値がレベルインスタンスへ適用され、Jenniferと部屋の家具の比率が自然に見えることを確認する
3. Mキーで車内↔Cigar Roomを何度も往復しても、Scaleが正しく再適用され、意図しない値の蓄積(補正倍率が毎回掛け合わされて累積してしまう等の実装ミス)が起きないことを確認する。`BaseScale`が固定値として保持され、都度`BaseScale × CorrectionScale`で再計算されていることを確認する
4. Jennifer自身のActor Scaleが、どのシーンでも常にCanonicalな基準値のまま変化していないことを確認する(通常は`1,1,1`だが、将来Jenniferの基準Actor Scale自体が`0.98`等に変更された場合でも、「シーンによって変化しない」ことが本質であり、`1,1,1`固定という数値自体が要件ではない)
5. 新しい背景シーンをDataTableに追加していない状態(未登録シーン)でシーン切り替えが呼ばれても、クラッシュせず既定の補正倍率(`1.0`)にフォールバックすることを確認する
6. Scale補正後も、床面・入口・Spawn位置などの基準AnchorのWorld座標が、Scale変更前と一致している(ズレていない)ことを確認する
7. Cigar RoomのLevel Instance内にTrigger・Spawn Point・Blocking Volume等のGameplay Actorが含まれる場合、Scale適用によってそれらの位置・当たり判定が意図せず変化していないことを確認する
8. Vehicle用・CigarRoom用など複数のレベルインスタンスを行き来しても、それぞれ固有のBaseScaleが正しく使われ、片方のBaseScaleがもう片方に誤って流用されないことを確認する
9. レベルインスタンスが破棄→再生成される実装の場合、再生成のたびに新しいActorへ`CaptureBaseScale()`が呼ばれ、古いActorのBaseScaleが誤って再利用されないことを確認する
10. レベルインスタンスが破棄される際、破棄直前のタイミングで`OnLevelInstanceDestroyed()`(または同等の処理)が呼ばれ、`BaseScaleByInstance`から該当エントリが正しく削除される(破棄されたActorのエントリが残り続けない)ことを確認する
11. ゲーム開始直後、まだ一度もMキーを押していない状態でも、その時点で表示されているシーン(通常は車内)のレベルインスタンスに対して`CaptureBaseScale()`が呼ばれており、`BaseScaleByInstance`にエントリが存在することを確認する
12. Level/Map変更、またはSceneManager終了時に`OnSceneManagerShutdownOrLevelChange()`が呼ばれ、`BaseScaleByInstance`が`Empty()`される(前Levelに属していたエントリが新しいLevelへ残り続けない)ことを確認する

## 7. 実装順序

1. ステップ0の診断(JenniferのCanonical Heightを決めた上での実測比較)を実施し、結果を記録する(**この結果次第で以降の要否が変わる**)
2. 診断の結果、シーン側の実寸がズレていると判明した場合のみ、以下へ進む
3. **ステップ0.5のGo/No-Goテストを実施する**。Level InstanceのランタイムScale変更が期待どおり動作しない場合、本仕様の設計自体を見直す(以降のステップは進めない)
4. 対象レベルインスタンス(Cigar Room等)を開き、内部Actor一覧を確認する(セクション3.6)。Gameplay Actorが混在している場合は分離を検討する
5. 対象レベルインスタンスのPivot位置と、Scale変更が床面・入口等の基準Anchorに与える影響を確認する(セクション3.5)
6. ドア高・座面高・テーブル高など2〜3個の基準オブジェクトを実測し、`CorrectionScale = DesiredRealWorldSize / CurrentMeasuredSize`でCigar Roomの補正倍率を算出する(セクション2)。複数の基準で倍率がおおよそ一致することを確認する
7. `SceneScaleDataTable`を作成し、車内=1.0、Cigar Room=算出した補正倍率を登録する
8. 既存のシーン切り替え処理を確認し(セクション3)、レベルインスタンスのロード・生成直後に`CaptureBaseScale()`を呼ぶ処理、破棄直前に`OnLevelInstanceDestroyed()`を呼ぶ処理、`ApplySceneScale()`の呼び出し、ゲーム開始時に既に表示されているシーンへの`CaptureBaseScale()`呼び出し(セクション3の確認事項4)、およびLevel/Map変更時・SceneManager終了時に`OnSceneManagerShutdownOrLevelChange()`を呼ぶ処理を追加する
9. 実機で車内↔Cigar Roomを往復し、合格条件(セクション6)を確認する
10. 今後新しい背景シーンを追加する際は、追加のたびにステップ0の診断・3.6のActor確認・セクション2の実測によるCorrectionScale算出を行い、DataTableへ1行追加する運用をルール化する
