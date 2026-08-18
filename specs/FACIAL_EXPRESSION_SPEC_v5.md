# 表情制御仕様 (FACIAL_EXPRESSION_SPEC_v5.md)

> v5: (チャッピー追加) Jennifer側の顔制御確認から手動表情テスト、補間、Realtime API連携、LipSync共存、自動まばたき・視線・微細表情までの推奨実装順序(Phase1〜6)、およびセクション17.5(Face競合確認・まばたき・視線・左右非対称)を追加。
> (Claude指摘・反映) 新しいPhase構成のチェックリストから、ステップ0.5(RealtimeVoiceComponentのアタッチ先・Instructions全文・WebSocketコールバックスレッドの確認)への参照が漏れていたため、Phase1の1番とPhase3の7番に明示的に復元。
> - [Claude指摘→反映] `missing_argument`(必須フィールド欠落)エラーがセクション3.4のoutputカタログ・セクション11の異常系一覧・セクション17の合格条件のいずれにも載っていなかったため追加。
>   理由: セクション2のコードは既にこのケースを`status:error/reason:missing_argument`として処理しているが、他の異常系(`parse_failed`等)と同じ扱いでドキュメント化されていないと、実装・テストの両方で見落とされるリスクがあるため。
> - [チャッピー指摘→反映] 4b(Blender直接モーフ方式)がARKit方式(4.2〜4.4)のTarget/Current補間・未知emotion時の現状維持と矛盾していたため、同じ方式に揃えた。
>   理由: 4bは「前回のMorphを即0→新しいMorphを即設定」という古い実装のまま残っており、本文全体の「0.2〜0.5秒補間」「未知emotionでは現在の表情を維持」という方針と食い違っていたため。
> - [チャッピー指摘→反映] `call_id`が空でFunction Call outputを送れないケースが、Response完了管理(3.3.1)からどう扱われるか未定義だったため、Response完了カウントの対象として処理済み扱いにする方針を明記した。
>   理由: 未定義のままだと、そのResponseが永久に「未完了」扱いになり`response.create`が送られず会話が止まるリスクがあるため。
> - [チャッピー指摘→反映] `missing_argument`という名前が「フィールド欠落」だけでなく「型不正」も含むことを、コード・本文の両方に明記した。
>   理由: `TryGetStringField`/`TryGetNumberField`はフィールド欠落と型不正のどちらでもfalseを返すため、実装が名前から「欠落だけ」と誤解するリスクがあったため。
> - [チャッピー指摘→反映] セクション19の「Current/Targetを持ち越す」と「Actor再生成時はTargetのみ再適用」の間で、Currentの扱いが未定義だったため一本化した(持ち越す対象はTargetのみ、Actor再生成時はCurrent=0から通常補間で再開)。
>   理由: 補間途中(Current≠Target)で場所移動した場合の挙動が定義されておらず、実装者によって解釈が割れる余地があったため。
> - [チャッピー指摘→反映] セクション20の見出し・合格条件14番が「Phase 2の任意最適化」となっていたが、セクション18の実装順序ではカメラ外最適化はPhase 6(17番)のため、表記をPhase 6に統一した。
> - [Claude指摘→反映] セクション17.5.5(強い表情の長時間保持)が「Phase 2以降で検討する」となっていたが、同じ機能はセクション18の実装順序ではPhase 5の15番として位置づけられているため、表記をPhase 5以降に修正した。
>   理由: セクション20と同種の「旧フラットな実装優先順位リストの番号が、Phase1〜6構成への刷新時に更新されず残っていた」ケースのため。
> - [チャッピー指摘→反映] `ApplyExpression()`がARKit方式(`UARKitLiveLinkSubsystem`)専用の実装例のみで、4b(Direct Morph方式)の`ULipSyncComponent::SetExpressionTarget()`へどう分岐するか、失敗時の戻り値をどう区別するかが未定義だったため、`EExpressionApplyResult`に`TargetComponentUnavailable`を追加し、方式ごとの分岐と対応するFunction Call output(`target_component_unavailable`)を明記した。
>   理由: 記録Aだった場合の`express_emotion → ApplyExpression() → ？？？ → SetExpressionTarget()`の実際の経路が仕様化されておらず、Claude Codeが実装時に自己判断せざるを得ない状態だったため。
> - [チャッピー指摘→反映] `intensity`がNaN/±Infinity相当の非有限値だった場合の防御が無く、`FMath::Clamp()`だけに頼っていたため、`FMath::IsFinite()`による明示チェックと`status:error/reason:invalid_intensity`を追加した。
>   理由: `FMath::Clamp()`はNaNとの比較が常にfalseになるためNaNを正しく処理できない可能性があり、異常系をここまで厳密に仕様化している以上、この経路だけ手薄なのは一貫性を欠くため。
> - [チャッピー指摘→反映] `call_id`が空のFunction Callを完了扱いにする際、`FunctionName`だけをキーにしていたため、同一Response内に同名Function Call(例: `express_emotion`が2回、両方call_id空)が複数あった場合に一意識別できない問題を修正し、`item_id`(無ければ`response_id`+`output_index`)を使う方式に変更した。
>   理由: `FunctionName`だけのキーだと、複数の異常呼び出しが同じキーに集約されてしまい、片方の完了漏れに気づけないリスクがあったため。
> - [チャッピー指摘→反映] セクション2.1でARKit/Direct Morphの共通バックエンド分岐を正式化したにもかかわらず、冒頭「全体の流れ」とセクション4.4が依然としてARKit固定の記述のまま残っていたため、両方を2.1の共通構造に整合させた。4.4は独立した`ApplyExpression()`ではなく、2.1の`#if EXPRESSION_BACKEND_ARKIT`分岐内部の詳細という位置づけに書き直した。
>   理由: Claude Codeが4.4だけを読んだ場合、ARKit固定で実装してしまうリスクがあり、セクション2.1で解決したはずの矛盾が別の場所に残っていたため。
> - [チャッピー指摘→反映] Response単位のFunction Call管理(期待Call数、完了済みキー、未送信output、`response.create`送信済みフラグ)が、WebSocket切断や新規セッション開始をまたいで残ってしまう問題への対応として、セクション3.3.2(Pending Function Call状態クリア)を新設し、切断時・新規接続時・新規セッション開始時に必ず全クリアする仕様を追加した。
>   理由: `response_id`・`item_id`・`output_index`まで使ってFunction Callを厳密に一意管理する設計にした以上、その管理状態自体が古いセッションをまたいで残ると、再接続後のFunction Call完了判定を狂わせるリスクがあるため。
> - [Claude指摘→反映] 上記のセクションを新設した際に見出し番号を一時的に「3.7」としていたため、直前の3.3.1・直後の3.4との間で番号が前後する矛盾が生じていた。3.3.1に続くResponse管理関連の内容として適切な「3.3.2」に採番し直し、本文中の全参照箇所も揃えた。
>   理由: 見出し番号の前後は、目次的な参照(「セクション◯◯参照」等)を読む際に混乱を招くため。
> - [Claude Code実装時の確認結果→反映] ステップ0の確認作業により、実働バックエンドが記録B(ARKit)ではなく記録A(Direct Morph)であることが確定した。実際に表示されているCrimson顔は`Use ARKit Face = false`で、`CrimsonGazeMorphComponent`が`jawOpen`を直接操作していた(ARKit側にも値は送られているが画面には反映されていない)。この確定結果をステップ0に明記し、以降この仕様書を読む際にステップ0の確認をやり直す必要がないようにした。
>   理由: 仕様書自体が「実装前に必ず確認すること」としていた分岐が実際に機能した結果であり、確認結果を記録しないと次にこの仕様書を読む人が同じ調査をやり直すことになるため。
> - [Claude Code実装時の確認結果→反映] 現在のCrimsonメッシュに存在するモーフターゲットが`jawOpen`・`mouthSeal`・`eyeBlinkLeft`・`eyeBlinkRight`のみで、感情表情用モーフ(笑顔・眉・悲しみ等)が存在しないことが確定したため、4bにこの制約を明記した。Function Calling配線自体はこの制約と無関係に完成させられること、感情表情を見た目に出すには別途Blender側でのモーフ追加作業が必要なこと、`eyeBlinkLeft/Right`は既に存在するためまばたき(17.5.2)はモーフ追加を待たずに進められることを記載した。
>   理由: 「なぜ感情が見た目に出ないのか」を次に読む人がすぐ理解できるようにするため、また未実装の原因を仕様のロジック不備と誤解しないようにするため。

Realtime APIのFunction Calling(ツール呼び出し)を使い、AIが会話しながら自分の判断で表情を切り替える仕組みの仕様。
Claude Codeが実装に着手できるよう、既存コード(`ARKitLiveLinkSubsystem`/`ARKitLiveLinkSource`)を踏まえて具体化したもの。

## ステップ0(実装前に必ず行うこと): 現在どちらの口パク方式が動いているか確認する

開発記録上、口パクの実装方式について**矛盾する2つの記録**が残っている。

- 記録A: 「ARKit LiveLink/RigLogic経由の口パクは廃止し、Blenderでメッシュに直接`jawOpen`モーフターゲットを追加する方式に変更」
- 記録B: 「MetaHumanの『From Custom Mesh』でMeshyの顔をMetaHuman化し、標準のARKit LiveLink/RigLogic経由の口パクが正常に動作するようになった」

このドキュメントの「4. 表情の適用」は**記録B(ARKit LiveLink経由)を前提**に書かれている。実装を始める前に、必ず以下のどちらかで実際の状態を確認すること。

**確認方法**
1. 現在のキャラクターBP(`BP_Jennifer`等)の詳細パネルで、`Use ARKit Face`がオンで`ARKit Face Subj`が`LifeSimARKitFace`になっているか確認する
2. 実際にプレイして口パクをテストし、出力ログに`ARKitLiveLinkSubsystem`や`PushJawOpen`関連のログが出るか確認する
3. `ARKitLiveLinkSubsystem::PushJawOpen`の呼び出し元(`LipSyncComponent`等)が、実際にTickで呼ばれ続けているかコードを確認する

- **ARKit LiveLink経由(記録B)だと確認できた場合** → このドキュメントの「4. 表情の適用」をそのまま実装する
- **Blender直接モーフ方式(記録A)だと確認できた場合** → 「4b. 代替実装(Blender直接モーフ方式の場合)」を参照する

**【確認結果・実装時に確定】Claude Codeによる実装時の調査で、実働バックエンドは記録A(Direct Morph方式)であることが確定した。** `LipSyncComponent → UARKitLiveLinkSubsystem::PushJawOpen()`は毎フレーム動いてARKit側にも値を送り続けているが、実際に画面へ表示されている新しいCrimson顔(BPのキャラクター)は`Use ARKit Face = false`になっており、代わりに`CrimsonGazeMorphComponent`が`jawOpen`モーフターゲットを直接操作することで口パクを実現している。つまり、ARKit LiveLink経由の値は実際には画面に反映されていない「配線されているが見えていない」状態だった。このため、本ドキュメントの「4. 表情の適用」(ARKit方式)ではなく「4b. 代替実装(Blender直接モーフ方式の場合)」の方針で実装を進めること。以降、新たにこの仕様書を読む場合はステップ0の確認作業を再度行う必要はなく、この結果を前提にしてよい(ただしCrimsonメッシュ自体が将来差し替えられた場合は、念のため再確認すること)。
## ステップ0.5: 補足確認事項

- `RealtimeVoiceComponent`が現在どのActor/シーンにアタッチされているか(教室・映画館・ジェニファーの部屋など複数の場所を移動する仕組みがあるため、表情制御もすべての場所で同じコンポーネント経由になっているか確認)
- `Instructions`(人格設定)の現在の全文を確認し、感情表現の指示を追記する際に既存の文言と自然に繋がるようにする
- **`HandleWebSocketMessage()`がどのスレッドで呼ばれているか確認する**。WebSocketのI/Oスレッド上で直接呼ばれている場合、`SetExpressionTarget`/`TickExpression`でTMapやUObjectを直接触るとクラッシュや競合の原因になる。既存の`jawOpen`受信処理(Whisper/ChatGPT/TTSループや`PushJawOpen`)がすでにゲームスレッドへのディスパッチ(例: `AsyncTask(ENamedThreads::GameThread, ...)`)を行っているかを確認し、行っているならその経路をそのまま流用する。行っていない場合は表情側の実装で新たに対応が必要になる

## 前提知識: 既存の口パク実装との関係

現在の口パク(`jawOpen`)は以下の経路で動いている。

```
LipSyncComponent → UARKitLiveLinkSubsystem::PushJawOpen(Value)
  → FARKitLiveLinkSource::PushCurveValue(TEXT("jawOpen"), Value)
  → LiveLink経由でFace_AnimBPへ配信(サブジェクト名 "LifeSimARKitFace")
```

重要な点: `FARKitLiveLinkSource`は**標準ARKit互換52カーブすべてを静的データとして既に登録済み**(未使用分は常に0を送っている状態)。つまり **`ARKitLiveLinkSource.cpp`/`.h`は変更不要**。`PushCurveValue(FName CurveName, float Value)`という汎用メソッドが既にあるので、表情用のカーブもこれをそのまま使える。

追加が必要なのは `UARKitLiveLinkSubsystem` に、`PushJawOpen`と同じパターンの薄いラッパーを増やすことと、それを呼び出す側(`RealtimeVoiceComponent`)のFunction Calling処理だけ。

## 全体の流れ

```text
1. Realtime接続時に使用モデルを確定する
2. session.update で既存toolsを維持したまま express_emotion を追加する
3. AIが感情変化時に express_emotion を呼ぶ
   → response.function_call_arguments.done を受信
4. RealtimeVoiceComponentが call_id / emotion / intensity を検証する
5. ApplyExpression() が適用可否を返す
6. 成功時:
     選択されたExpression Backend(ステップ0で確定、セクション2.1参照)
       ├ ARKit方式    → UARKitLiveLinkSubsystem::SetExpressionTarget()
       └ Direct Morph方式 → ULipSyncComponent::SetExpressionTarget()
     → Expression Tick
     → Current → Target補間
     → PushCurveValue() / SetMorphTarget()
   失敗時:
     現在の表情を維持
7. 各Function Callについて conversation.item.create(function_call_output) を送る
8. 同一Response内の必要なFunction Call outputをすべて返し終えてから、
   response.create を原則1回だけ送って会話を続行する
```

## 1. ツール定義(session.updateに追加するJSON)

既存の `URealtimeVoiceComponent::SendSessionUpdate()` が組み立てている `session` オブジェクトに、
**`tools`配列と`tool_choice`を追加**する。ツールは配列でラップする点に注意(単体オブジェクト直書きではない)。

```json
{
  "type": "session.update",
  "session": {
    "type": "realtime",
    "output_modalities": ["audio"],
    "instructions": "...",
    "audio": { "...": "(既存のまま)" },
    "tools": [
      {
        "type": "function",
        "name": "express_emotion",
        "description": "Call this whenever your emotional tone shifts during the conversation, so your facial expression can match what you're saying. Call it again whenever the emotion changes, including back to neutral.",
        "parameters": {
          "type": "object",
          "properties": {
            "emotion": {
              "type": "string",
              "enum": ["neutral", "happy", "surprised", "sad", "confused", "embarrassed"]
            },
            "intensity": {
              "type": "number",
              "description": "0.0 (barely noticeable) to 1.0 (very strong)"
            }
          },
          "required": ["emotion", "intensity"]
        }
      }
    ],
    "tool_choice": "auto"
  }
}
```


### 1.4 `model`の指定場所（重要）

Realtime APIのモデルは**接続時に確定**させる。`session.update`のJSONには`model`を含めない。

この仕様の`session.update`例では、`instructions`、`audio`、`tools`、`tool_choice`等のセッション設定だけを更新する。

Claude Codeは既存のRealtime接続コードを確認し、モデル指定が必要な場合は接続URL・接続初期化処理等、現在の実装方式に沿った接続時の設定を利用すること。`SendSessionUpdate()`へモデル指定を新規追加しない。

## 1.5. 既存の場所移動ツールとの共存(【必須】実装前に確認すること)

プロジェクトには既に「ジェニファーが合意したら教室・映画館・ジェニファーの部屋等へ移動する」という、
Function Callingベースと思われる仕組みが存在する(`PROGRESS.md`参照)。

`session.tools`配列は、`session.update`を送るたびに**その時点の内容で丸ごと上書き**される。
つまり、`express_emotion`だけを含む`tools`配列を送ってしまうと、**既存の場所移動用ツール定義が消える**危険性がある。

**実装前に必ず確認すること**

1. `RealtimeVoiceComponent::SendSessionUpdate()`(または同等の処理)を確認し、
   既に場所移動用のツール(例: `move_to_location`のような名前)が`tools`配列に含まれているか確認する
2. 含まれている場合、`express_emotion`は**その配列に追加する形**で実装する(既存のツール定義を消さないこと)
3. `session.update`が呼ばれるタイミング(セッション開始時に1回だけか、場所移動のたびに再送されるか)も確認し、
   `express_emotion`がどのタイミングから有効になるか、場所移動ツールと同じタイミングで有効であることを確認する

このステップを飛ばすと、表情機能を追加した結果、場所移動機能が動かなくなるという回帰が起きうる。

### 1.6 既存Function Callingの応答処理との共存（必須）

場所移動等の既存Function Callingについて、以下を実装前に確認する。

- `conversation.item.create`（`function_call_output`）をどこで送っているか
- `response.create` をどこで送っているか
- Function Callごとの個別処理か、Response単位の共通処理か
- 1つのResponse内に複数Function Callが含まれた場合の完了判定を既に持っているか

**重要: `response.create` はFunction Callごとに無条件送信しない。**

同一Response内に、例えば以下の2つが含まれる可能性を考慮する。

```text
express_emotion
move_to_location
```

この場合は:

```text
express_emotion の function_call_output を送る
move_to_location の function_call_output を送る
↓
必要なFunction Call outputをすべて返したことを確認
↓
response.create を1回だけ送る
```

既存コードにResponse単位のFunction Call管理がある場合は必ず再利用する。

存在しない場合は、少なくとも `response_id` または既存イベント情報を使って「現在処理中ResponseのFunction Call数 / 完了数」を追跡し、**同一Responseに対する`response.create`を原則1回に集約**する。

`express_emotion` 専用コードの中へ無条件の `response.create` を埋め込まない。

## 2. 呼び出しの受信(response.function_call_arguments.done)

`URealtimeVoiceComponent::HandleWebSocketMessage()` の既存Function Calling処理へ統合する。

受信イベント例:

```json
{
  "type": "response.function_call_arguments.done",
  "response_id": "resp_XXXXXXXX",
  "item_id": "item_XXXXXXXX",
  "output_index": 0,
  "call_id": "call_XXXXXXXX",
  "name": "express_emotion",
  "arguments": "{\"emotion\":\"happy\",\"intensity\":0.7}"
}
```

`response_id`・`item_id`・`output_index`は本仕様作成時点のOpenAI公式ドキュメントに基づく想定フィールドであり、実装時にログへ生JSONをダンプして実際の構造を確認すること(セクション9と同じ方針)。

`arguments` はJSON文字列なので、`FJsonSerializer`でもう一段パースする。

### 2.1 表情適用結果を戻り値で返す

`ApplyExpression()` は `void` にしない。少なくとも成功/失敗を呼び出し側が判定できるようにする。

**【矛盾修正】以前の版では、`ApplyExpression()`の具体例(セクション4.4)がARKit方式(`UARKitLiveLinkSubsystem`)に固定されていた一方、4bのBlender直接モーフ方式では`ULipSyncComponent::SetExpressionTarget()`が別途定義されており、`ApplyExpression()`からこの2つのどちらへ、どういう条件で分岐するのかが未定義だった。以下の通り、バックエンドを共通の型で抽象化する。**

推奨:

```cpp
enum class EExpressionApplyResult : uint8
{
    Applied,
    UnknownEmotion,
    SubsystemUnavailable,       // ARKit方式: UARKitLiveLinkSubsystemを取得できない
    TargetComponentUnavailable, // Direct Morph方式: ULipSyncComponent(またはFaceMeshComponent)を取得できない
    InvalidWorld
};

EExpressionApplyResult ApplyExpression(const FString& Emotion, float Intensity);
```

`ApplyExpression()`内部の分岐は、ステップ0で確定した口パク方式(記録A/記録B)に応じて、ビルド時または実行時のどちらか既存コードに合わせやすい方法で固定する。両方式が同時に有効になることは想定しないため、`#if`等のコンパイル時分岐でも、あるいは単純に「ステップ0で確定した方だけを呼ぶ1行」でもよい。

```cpp
// 概念コード。実際にはステップ0で確定した方式のみが有効な状態になっている想定。
// これが ApplyExpression() の正式な全体構造であり、セクション4.4はこの
// #if EXPRESSION_BACKEND_ARKIT 分岐内部の詳細を示したものにすぎない。
EExpressionApplyResult URealtimeVoiceComponent::ApplyExpression(
    const FString& Emotion,
    float Intensity)
{
    // 未知emotionチェック・GetWorld()/GetGameInstance()チェックは、
    // バックエンドによらない共通処理としてここで行う(セクション4.4・4bのARKit/Direct Morph
    // それぞれのSetExpressionTarget()側にも防御的な未知emotionチェックを重ねてよい)。
    if (!GetWorld())
    {
        return EExpressionApplyResult::InvalidWorld;
    }

    static const TSet<FString> KnownEmotions = {
        TEXT("neutral"), TEXT("happy"), TEXT("surprised"),
        TEXT("sad"), TEXT("confused"), TEXT("embarrassed")
    };

    if (!KnownEmotions.Contains(Emotion))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[EXPRESSION] 未知のemotion \"%s\"、現在の表情を維持"), *Emotion);
        return EExpressionApplyResult::UnknownEmotion;
    }

    UGameInstance* GI = GetWorld()->GetGameInstance();
    if (!GI)
    {
        return EExpressionApplyResult::InvalidWorld;
    }

#if EXPRESSION_BACKEND_ARKIT
    UARKitLiveLinkSubsystem* ARKitSubsystem = GI->GetSubsystem<UARKitLiveLinkSubsystem>();
    if (!ARKitSubsystem)
    {
        return EExpressionApplyResult::SubsystemUnavailable;
    }
    ARKitSubsystem->SetExpressionTarget(Emotion, Intensity);
#else // EXPRESSION_BACKEND_DIRECT_MORPH
    ULipSyncComponent* LipSyncComp = FindComponentByClass<ULipSyncComponent>(); // または既存の参照を使う
    if (!LipSyncComp)
    {
        return EExpressionApplyResult::TargetComponentUnavailable;
    }
    LipSyncComp->SetExpressionTarget(Emotion, Intensity);
#endif

    return EExpressionApplyResult::Applied;
}
```

セクション4.4は、上記コードの`#if EXPRESSION_BACKEND_ARKIT`分岐部分をより詳細に示したものであり、単体で完結する別の`ApplyExpression()`ではない。実装時はこの2.1の全体構造を正とする。

`TargetComponentUnavailable`のFunction Call output上の`reason`は`target_component_unavailable`とする(セクション3.4参照)。

概念コード:

```cpp
else if (EventType == TEXT("response.function_call_arguments.done"))
{
    FString FunctionName, CallId, ArgumentsJson, ResponseId, ItemId;
    int32 OutputIndex = 0;
    JsonObject->TryGetStringField(TEXT("name"), FunctionName);
    JsonObject->TryGetStringField(TEXT("call_id"), CallId);
    JsonObject->TryGetStringField(TEXT("arguments"), ArgumentsJson);
    JsonObject->TryGetStringField(TEXT("response_id"), ResponseId);
    JsonObject->TryGetStringField(TEXT("item_id"), ItemId);
    JsonObject->TryGetNumberField(TEXT("output_index"), OutputIndex);

    if (FunctionName == TEXT("express_emotion"))
    {
        if (CallId.IsEmpty())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[EXPRESSION] エラー: call_idが空のため適用をスキップ"));
            // 【矛盾修正】call_idが無いためfunction_call_outputを送りようがない。
            // このFunction Callはセクション3.3.1のResponse完了管理上「完了扱い」として扱う。
            //
            // 【追加修正】FunctionNameだけでは、同一Response内に同名Function Callが
            // 複数含まれる場合(例: express_emotionが2回呼ばれ、両方ともcall_idが空)に
            // どのCallが完了したのか一意に識別できない。ItemId(存在すれば)、それが
            // 無ければ ResponseId + OutputIndex の組で、Function Call単位に一意な
            // キーを構成してカウントすること。
            const FString CompletionKey = !ItemId.IsEmpty()
                ? ItemId
                : FString::Printf(TEXT("%s:%d"), *ResponseId, OutputIndex);

            MarkFunctionCallHandledWithoutOutput(CompletionKey);
            return;
        }

        TSharedPtr<FJsonObject> ArgsObject;
        TSharedRef<TJsonReader<>> ArgsReader =
            TJsonReaderFactory<>::Create(ArgumentsJson);

        if (!FJsonSerializer::Deserialize(ArgsReader, ArgsObject) ||
            !ArgsObject.IsValid())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[EXPRESSION] エラー: argumentsのJSONパースに失敗"));

            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"parse_failed\"}"));
            return;
        }

        FString Emotion;
        double Intensity = 0.0;

        // 【矛盾修正】TryGetStringField/TryGetNumberFieldは、フィールドが「存在しない」場合と
        // 「型が違う」場合(例: emotionが数値、intensityが文字列)の両方でfalseを返す。
        // このため"missing_argument"は「欠落」と「型不正」の両方をまとめて表す名前として扱う
        // (「欠落」のみを表す名前ではない)。区別が必要になった場合は、両者を判定して
        // 別のreason(例: "invalid_argument")を追加すること。
        if (!ArgsObject->TryGetStringField(TEXT("emotion"), Emotion) ||
            !ArgsObject->TryGetNumberField(TEXT("intensity"), Intensity))
        {
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"missing_argument\"}"));
            return;
        }

        // 【矛盾修正/追加】intensityがNaNや±Infinity相当の非有限値の場合、
        // FMath::Clamp()はNaNを正しく処理しない(NaNとの比較は常にfalseを返すため、
        // Clamp後もNaNが残る可能性がある)。Clampに頼る前に明示的にIsFinite()で弾く。
        if (!FMath::IsFinite(static_cast<float>(Intensity)))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[EXPRESSION] エラー: intensityが非有限値(NaN/Infinity)"));

            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"invalid_intensity\"}"));
            return;
        }

        const EExpressionApplyResult Result =
            ApplyExpression(Emotion, static_cast<float>(Intensity));

        switch (Result)
        {
        case EExpressionApplyResult::Applied:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"applied\"}"));
            break;

        case EExpressionApplyResult::UnknownEmotion:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"unknown_emotion\"}"));
            break;

        case EExpressionApplyResult::SubsystemUnavailable:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"subsystem_unavailable\"}"));
            break;

        case EExpressionApplyResult::TargetComponentUnavailable:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"target_component_unavailable\"}"));
            break;

        case EExpressionApplyResult::InvalidWorld:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"invalid_world\"}"));
            break;

        default:
            QueueFunctionCallOutput(
                CallId,
                TEXT("{\"status\":\"error\",\"reason\":\"apply_failed\"}"));
            break;
        }

        // response.create はここで無条件送信しない。
        // セクション3のResponse単位管理で、必要なFunction Call outputが揃った後に1回だけ送る。
    }
}
```

`QueueFunctionCallOutput()`という名前は例であり、既存のFunction Calling共通処理があればそれを優先する。

## 3. Function Call結果を返し、Responseを継続する（必須）

Function Callの結果送信と、次Responseの生成要求は**別の責務**として扱う。

### 3.1 function_call_output

各Function Callについて、まず `conversation.item.create` で `function_call_output` を送る。

```cpp
void URealtimeVoiceComponent::SendFunctionCallOutput(
    const FString& CallId,
    const FString& OutputJson)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("type"), TEXT("function_call_output"));
    Item->SetStringField(TEXT("call_id"), CallId);
    Item->SetStringField(TEXT("output"), OutputJson);

    TSharedRef<FJsonObject> ItemEvent = MakeShared<FJsonObject>();
    ItemEvent->SetStringField(TEXT("type"), TEXT("conversation.item.create"));
    ItemEvent->SetObjectField(TEXT("item"), Item);

    SendJson(ItemEvent);
}
```

### 3.2 response.create

`SendFunctionCallOutput()` の中から `response.create` を送ってはならない。

同一Responseに含まれる必要なFunction Callのoutputがすべてconversationへ追加された後に、Response単位の管理処理から1回だけ送る。

```cpp
void URealtimeVoiceComponent::SendResponseCreate()
{
    TSharedRef<FJsonObject> ResponseCreateEvent = MakeShared<FJsonObject>();
    ResponseCreateEvent->SetStringField(TEXT("type"), TEXT("response.create"));
    SendJson(ResponseCreateEvent);
}
```

### 3.3 複数Function Call時の原則

```text
Response A
 ├─ express_emotion
 └─ move_to_location

→ output(express_emotion)
→ output(move_to_location)
→ response.create 1回
```

Function Callごとに `response.create` を送る実装は禁止する。

既存コードにこの管理が既にある場合は、それを変更せず `express_emotion` を参加させる。

### 3.3.1 「Response内のFunction Callが揃った」をどう検知するか(【必須】実装前に確認すること)

`response.function_call_arguments.done` は個々のFunction Call完了時に1件ずつ届くだけで、そのイベント単体からは「同一Response内に他に何件のFunction Callが含まれているか」は分からない。**「Response単位のFunction Call数/完了数」を無根拠に自前でカウントし始めると、カウントの前提自体が誤っている可能性がある。**

実装前に必ず以下を確認する。

1. 既存の場所移動Function Calling実装が、Response単位の完了検知の仕組みを**既に持っているか**確認する(セクション1.6・3.3で既に述べた確認事項と同じ)。持っている場合はそれをそのまま再利用し、以下の2は不要
2. 持っていない場合、`response.done` イベント(Responseの完了を示すイベント。Function Call待ちでResponseが一旦終了する際にも送られてくる可能性が高い)を実際に受信し、そのペイロードをログに生JSONダンプして構造を確認する。特に、そのイベントの`response.output`(または同等のフィールド)にそのResponseに含まれる全Function Call(`function_call`タイプのアイテム)が列挙されているかを確認する
3. 上記が確認できたら、`response.done`受信時点で「このResponseに含まれるFunction Call一覧」が確定するので、そのすべてに`function_call_output`を送り終えた後に`response.create`を1回送る、という設計に沿わせる

Realtime APIのイベント名・構造は変更が比較的多い分野のため、上記2・3は本仕様作成時点の一般的な想定であり、実装時に実際のイベントと食い違いがあれば、まずログの生JSONを見て実際のフィールド名・タイミングを確認すること(セクション9と同じ方針)。

**【矛盾修正】`call_id`が空でoutputを送れないFunction Callの扱い**: セクション11の異常系にある通り、`call_id`が空の場合は`function_call_output`自体を送れない(送り先が無い)。しかしResponse完了管理が「必要なFunction Call outputがすべて揃ったら`response.create`を送る」という設計である以上、このケースを無視すると、そのResponseが永久に「未完了」のまま`response.create`が送られず会話が止まる可能性がある。**`call_id`が空のFunction Callも、outputを送らないままResponse完了カウントの対象として処理済み扱いにする**(実際に何を数えるかは3.3.1で確定した実装方式に従う。例えば`response.done`のFunction Call一覧を基準にする方式なら、`call_id`空のCallも一覧に含まれている前提でその他のoutputだけ送信し、`response.done`基準で完了判定する形になる)。

**【追加修正】完了扱いにする際の識別キーについて**: 上記の「処理済み扱いにする」処理を`FunctionName`だけで管理してはならない。同一Response内に同名のFunction Call(例えば`express_emotion`が2回呼ばれ、両方とも`call_id`が空だったケース)が複数含まれる場合、`FunctionName`だけでは一意に識別できず、片方の完了だけが2回分カウントされてしまう可能性がある。`item_id`(存在する場合はそれを使う)、無ければ`response_id`と`output_index`の組で、Function Call単位に一意なキーを構成してカウントすること(セクション2のコード例を参照)。

### 3.3.2 WebSocket切断・新規セッション開始時のPending Function Call状態クリア(【必須】)

**【追加修正】ここまでの仕様で、Response単位のFunction Call管理(`response_id`、`item_id`、期待されるCall数、完了済みCallの一意キー集合、未送信output、`response.create`送信済みフラグ等)をかなり厳密に持つ設計になっている。これらの状態がWebSocket切断や新規Realtimeセッション開始をまたいで残ってしまうと、再接続後に古いResponseの状態を誤って引き継ぎ、新しいセッションのFunction Call完了判定を狂わせる可能性がある。**

以下のタイミングで、Response単位のFunction Call管理状態を**必ず全クリア**する。

- WebSocket切断時(意図的な切断・エラーによる切断のいずれも含む)
- Realtime APIへ新規接続した時(セクション12の表情neutral初期化と同じタイミング)
- 新規会話セッションを開始した時

クリアすべき状態の例(実際の変数名・構造は既存のFunction Call管理実装に合わせる):

```cpp
void URealtimeVoiceComponent::ClearPendingFunctionCallState()
{
    // 例: 追跡中のResponseごとの期待Call数・完了済みCall一意キー集合
    ExpectedCallsPerResponse.Empty();
    CompletedCallKeysPerResponse.Empty();

    // 例: まだconversation.item.createで送っていないoutputのキュー
    PendingFunctionCallOutputs.Empty();

    // 例: そのResponseに対してresponse.createを送信済みかのフラグ
    ResponseCreateSentFlags.Empty();
}
```

この関数は、WebSocket切断ハンドラおよびセクション12の初期化処理(Realtime新規接続時・新規会話セッション開始時)の両方から呼ぶ。既存の場所移動Function Calling側が既に何らかのPending状態管理を持っている場合は、その既存クリア処理へ`express_emotion`関連の状態も統合する形にし、別々の場所でバラバラにクリアしないこと。

### 3.4 express_emotionのoutput内容


正常:

```json
{"status":"applied"}
```

未知emotion:

```json
{"status":"error","reason":"unknown_emotion"}
```

Subsystem取得失敗(ARKit方式):

```json
{"status":"error","reason":"subsystem_unavailable"}
```

対象Component取得失敗(Direct Morph方式):

```json
{"status":"error","reason":"target_component_unavailable"}
```

World/GameInstance取得失敗:

```json
{"status":"error","reason":"invalid_world"}
```

必須フィールド(`emotion`または`intensity`)の欠落または型不正:

```json
{"status":"error","reason":"missing_argument"}
```

intensityが非有限値(NaN/Infinity):

```json
{"status":"error","reason":"invalid_intensity"}
```

JSON不正:

```json
{"status":"error","reason":"parse_failed"}
```

## 4. 表情の適用(UARKitLiveLinkSubsystemへの追加)

### 4.1 設計方針

ARKit LiveLink経由の場合、`PushExpression()` はARKitカーブへ値を即時送信する関数にはしない。

Function Calling受信時には**表情のTarget値だけを更新**し、補間処理側がCurrent値をTarget値へ滑らかに近づけ、そのCurrent値を `FARKitLiveLinkSource::PushCurveValue()` へ送る。

処理経路は以下に統一する。

```text
express_emotion受信
  → URealtimeVoiceComponent::ApplyExpression()
  → UARKitLiveLinkSubsystem::SetExpressionTarget()
      （Target値だけ更新）
  → Expression Tick
      Current → Target を補間
  → FARKitLiveLinkSource::PushCurveValue()
  → LiveLink → Face_AnimBP
```

これにより、セクション10の「滑らかな表情遷移」と実装を一本化する。

### 4.2 ARKitLiveLinkSubsystem.hへの追加

```cpp
// AIから指定された表情の目標値を更新する。ここではLiveLinkへ即時Pushしない。
void SetExpressionTarget(const FString& Emotion, float Intensity);

// 補間処理。実際のTick呼び出し元はセクション10.5で確定する。
void TickExpression(float DeltaTime);
```

**修正(矛盾整理)**: 以前の版では`ResetExpressionToNeutral()`という関数が宣言されていたが、本文のどのコード例からも呼ばれておらず、セクション12の初期化処理は代わりに`ApplyExpression(TEXT("neutral"), 0.0f)`を直接呼ぶ形になっていた。死んだ宣言が実装者を混乱させるため削除し、neutralへの初期化・復帰は常に`SetExpressionTarget(TEXT("neutral"), 0.0f)`(またはそれを呼ぶ`ApplyExpression`)に一本化する。

内部状態として、少なくとも表情カーブごとのCurrent値とTarget値を保持する。

```cpp
TMap<FName, float> ExpressionCurrentValues;
TMap<FName, float> ExpressionTargetValues;

float ExpressionInterpSpeed = 6.0f;
```

### 4.3 Target値の設定

`SetExpressionTarget()` は、まず**前のTarget値をすべて0へ戻し**、その後、新しいemotionに対応するTarget値を設定する。

重要: Current値はここで0へ戻さない。Current値は補間処理によって自然に新しいTargetへ移動する。

**重要(矛盾修正)**: 下記コードは以前の版で「未知のemotionを受け取ると無条件に全Targetを0リセットしてから何もせず抜ける」実装になっており、結果的に未知値がneutral扱いになってしまっていた。これはセクション11・17(「未知のemotionでは現在の表情を維持し、neutralへ勝手に変更しない」)と矛盾する。**既知のemotionかどうかを先に判定し、既知の場合(neutral含む)のみリセット処理に進む**ように順序を修正する。

```cpp
void UARKitLiveLinkSubsystem::SetExpressionTarget(const FString& Emotion, float Intensity)
{
    static const TArray<FName> ExpressionCurves = {
        TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"),
        TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
        TEXT("browInnerUp"), TEXT("eyeWideLeft"), TEXT("eyeWideRight"),
        TEXT("browDownLeft"), TEXT("browDownRight"),
        TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
        TEXT("mouthLeft"), TEXT("mouthRight"),
        TEXT("cheekPuff"), TEXT("eyeSquintLeft"), TEXT("eyeSquintRight"),
        TEXT("mouthPressLeft"), TEXT("mouthPressRight"),
    };

    // 既知のemotionかどうかを先に判定する(neutralも既知として扱う)。
    static const TArray<FString> KnownEmotions = {
        TEXT("neutral"), TEXT("happy"), TEXT("surprised"),
        TEXT("sad"), TEXT("confused"), TEXT("embarrassed"),
    };

    if (!KnownEmotions.Contains(Emotion))
    {
        // 未知のemotion: Targetには一切触れず、現在の表情を維持する(セクション11・17)。
        UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: 未知のemotion \"%s\" を受信、現在の表情を維持"), *Emotion);
        return;
    }

    // ここに来た時点でEmotionは既知(neutral含む)と確定しているので、ここで初めて全Targetを0にリセットする。
    for (const FName& Curve : ExpressionCurves)
    {
        ExpressionTargetValues.FindOrAdd(Curve) = 0.0f;
        ExpressionCurrentValues.FindOrAdd(Curve);
    }

    // neutralではintensityを無視し、全Target=0のまま終了する。
    if (Emotion == TEXT("neutral"))
    {
        return;
    }

    const float I = FMath::Clamp(Intensity, 0.0f, 1.0f);

    if (Emotion == TEXT("happy"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("mouthSmileLeft")) = I * 0.7f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthSmileRight")) = I * 0.7f;
        ExpressionTargetValues.FindOrAdd(TEXT("cheekSquintLeft")) = I * 0.3f;
        ExpressionTargetValues.FindOrAdd(TEXT("cheekSquintRight")) = I * 0.3f;
    }
    else if (Emotion == TEXT("surprised"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("browInnerUp")) = I * 0.8f;
        ExpressionTargetValues.FindOrAdd(TEXT("eyeWideLeft")) = I * 0.6f;
        ExpressionTargetValues.FindOrAdd(TEXT("eyeWideRight")) = I * 0.6f;
    }
    else if (Emotion == TEXT("sad"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("browDownLeft")) = I * 0.5f;
        ExpressionTargetValues.FindOrAdd(TEXT("browDownRight")) = I * 0.5f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthFrownLeft")) = I * 0.6f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthFrownRight")) = I * 0.6f;
    }
    else if (Emotion == TEXT("confused"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("browDownLeft")) = I * 0.6f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthLeft")) = I * 0.4f;
    }
    else if (Emotion == TEXT("embarrassed"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("cheekPuff")) = I * 0.3f;
        ExpressionTargetValues.FindOrAdd(TEXT("eyeSquintLeft")) = I * 0.4f;
        ExpressionTargetValues.FindOrAdd(TEXT("eyeSquintRight")) = I * 0.4f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthPressLeft")) = I * 0.3f;
        ExpressionTargetValues.FindOrAdd(TEXT("mouthPressRight")) = I * 0.3f;
    }
}
```

`embarrassed` の `cheekPuff` 等は仮マッピングであり、セクション14の単体確認結果を見て係数・カーブを調整する。

### 4.4 RealtimeVoiceComponent側(ARKitバックエンドの内部実装)

**【矛盾修正】以前の版では、ここに独立した`ApplyExpression()`の実装(ARKit専用)がそのまま掲載されており、セクション2.1で正式化した「ARKit/Direct Morphのバックエンド分岐を持つ共通`ApplyExpression()`」と矛盾していた。Claude Codeが4.4だけを読むと、再びARKit固定で実装してしまうリスクがあったため、4.4はセクション2.1の`ApplyExpression()`(共通の骨格)における`#if EXPRESSION_BACKEND_ARKIT`分岐の中身の詳細、という位置づけに書き直す。独立した`ApplyExpression()`としては読まないこと。全体の構造は必ずセクション2.1を正とする。**

未知emotionチェック・`GetWorld()`/`GetGameInstance()`チェックは、セクション2.1の共通`ApplyExpression()`内で既に行われている。以下はそれらを通過した後、ARKitバックエンドが担当する`#if EXPRESSION_BACKEND_ARKIT`分岐の中身のみを示す。

```cpp
// セクション2.1の ApplyExpression() 内、#if EXPRESSION_BACKEND_ARKIT 分岐の中身。
// (未知emotionチェック・GetWorld/GIチェックは、この手前で既に共通処理として完了している前提)

UARKitLiveLinkSubsystem* ARKitSubsystem =
    GI->GetSubsystem<UARKitLiveLinkSubsystem>();

if (!ARKitSubsystem)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[EXPRESSION] ARKitLiveLinkSubsystemを取得できません"));

    return EExpressionApplyResult::SubsystemUnavailable;
}

ARKitSubsystem->SetExpressionTarget(Emotion, Intensity);

UE_LOG(LogTemp, Log,
    TEXT("[EXPRESSION] Target更新: %s, intensity=%.2f"),
    *Emotion,
    Intensity);

return EExpressionApplyResult::Applied;
```

Subsystem側にも防御的な未知emotionチェックを残してよい。RealtimeVoiceComponent側で弾くことで、Function Call結果を正しく`status:error`として返せる。

`ARKitLiveLinkSource.cpp` / `.h` は変更不要。既存の `PushCurveValue()` を補間処理から利用する。

## 4b. 代替実装(Blender直接モーフ方式の場合)

ステップ0の確認で「記録A(Blenderで直接メッシュにモーフターゲットを追加)」が実際に動いている方式だと判明した場合、
`ARKitLiveLinkSubsystem`経由ではFace_AnimBPが値を見ていないため機能しない。代わりに、口パク(`jawOpen`)が
実際にどうやってメッシュに反映されているかを担当しているコンポーネント(`LipSyncComponent`等、`SetMorphTarget`を
直接呼んでいる箇所)と同じ経路を使う。

**事前確認が必要な情報**
- 対象のSkeletalMeshComponentの変数名・取得方法(`jawOpen`を動かしている既存コードと同じもの)
- 表情用に使えそうなモーフターゲットが、そのメッシュに実際に存在するか。存在しなければ、Blender側で追加作業が必要になる可能性がある
  - コンテンツブラウザでFaceのSkeletal Meshアセットを開き、「モーフターゲット」パネルで一覧を確認する
  - `jawOpen`以外に、`mouthSmile`系・`browDown`系などがあれば流用できる。無ければ、まずは`happy`(口角を上げる方向のモーフがあれば)だけ対応させる、といった段階的な実装が現実的

**【確認結果・実装時に確定】現在のCrimsonメッシュ(新しい表示顔)を実装時に調査した結果、存在するモーフターゲットは`jawOpen`・`mouthSeal`・`eyeBlinkLeft`・`eyeBlinkRight`の4つのみで、笑顔・眉・悲しみ等の感情表情用モーフは存在しないことが確定した(旧表示顔でも同様)。** これはこの仕様のロジックの不備ではなく、現状のアセットに起因する既知の制約である。

- `express_emotion`のFunction Calling配線(受信・引数検証・`ApplyExpression()`・Function Call output返却)自体は、この制約と無関係に完成させられる
- ただし、現状のモーフターゲットでは`happy`等の感情表情を実際に見た目へ反映することはできない。この場合、`ApplyExpression()`は`EExpressionApplyResult::TargetComponentUnavailable`(またはモーフが存在しないことを示す適切な結果)を返し、Function Call outputとして`status:error / reason:target_component_unavailable`のように正直に返す。「適用できた」と偽らないこと(セクション2.1・3.4を参照)
- 感情表情を実際に見た目へ出すには、Blender側で笑顔・眉・悲しみ等のモーフターゲットを追加する作業が別途必要になる。この作業は本仕様(Function Calling配線)の範囲外であり、UE5側の実装だけでは解決しない
- 一方、`eyeBlinkLeft`/`eyeBlinkRight`は既に存在するため、セクション17.5.2(自動まばたき)は表情モーフの追加を待たずに先に実装を進めてよい

**実装イメージ(モーフターゲット名は要確認・要調整)**

**修正(矛盾修正)**: 以前の版では「前回のMorphを即0にリセットしてから新しいMorphを即設定する」実装になっており、本文全体の方針(セクション10: Current→Targetで0.2〜0.5秒補間、セクション11・17: 未知のemotionでは現在の表情を維持しneutralへ勝手に変更しない)と食い違っていた。ARKit方式(4.2〜4.4)と同じTarget/Current方式・未知emotionチェックに揃える。

```cpp
// ULipSyncComponent.h への追加イメージ
TMap<FName, float> ExpressionCurrentValues;
TMap<FName, float> ExpressionTargetValues;
float ExpressionInterpSpeed = 6.0f;

void SetExpressionTarget(const FString& Emotion, float Intensity);
void TickExpression(float DeltaTime);
```

```cpp
// LipSyncComponent、または同等の権限を持つコンポーネント側に追加するイメージ
void ULipSyncComponent::SetExpressionTarget(const FString& Emotion, float Intensity)
{
    if (!FaceMeshComponent)
    {
        return;
    }

    // 実際に存在するモーフターゲット名に置き換えること
    static const TArray<FName> ExpressionMorphs = {
        TEXT("mouthSmile"), TEXT("browDown"), TEXT("mouthFrown") /* 要・実名確認 */
    };

    // ARKit方式(4.3)と同じく、既知のemotionかどうかを先に判定する(neutralも既知として扱う)。
    static const TArray<FString> KnownEmotions = {
        TEXT("neutral"), TEXT("happy"), TEXT("surprised"),
        TEXT("sad"), TEXT("confused"), TEXT("embarrassed"),
    };

    if (!KnownEmotions.Contains(Emotion))
    {
        // 未知のemotion: Targetには一切触れず、現在の表情を維持する。
        UE_LOG(LogTemp, Warning, TEXT("[EXPRESSION] エラー: 未知のemotion \"%s\" を受信、現在の表情を維持"), *Emotion);
        return;
    }

    // ここに来た時点でEmotionは既知(neutral含む)と確定しているので、ここで初めて全Targetを0にリセットする。
    for (const FName& Morph : ExpressionMorphs)
    {
        ExpressionTargetValues.FindOrAdd(Morph) = 0.0f;
        ExpressionCurrentValues.FindOrAdd(Morph);
    }

    if (Emotion == TEXT("neutral"))
    {
        return;
    }

    const float I = FMath::Clamp(Intensity, 0.0f, 1.0f);
    if (Emotion == TEXT("happy"))
    {
        ExpressionTargetValues.FindOrAdd(TEXT("mouthSmile")) = I * 0.7f; // 要・実名確認
    }
    // ... 他の感情も同様に、実際に存在するモーフターゲット名で追加
}

void ULipSyncComponent::TickExpression(float DeltaTime)
{
    if (!FaceMeshComponent)
    {
        return;
    }

    for (auto& Pair : ExpressionCurrentValues)
    {
        const FName Morph = Pair.Key;
        float& Current = Pair.Value;
        const float Target = ExpressionTargetValues.FindRef(Morph);

        Current = FMath::FInterpTo(Current, Target, DeltaTime, ExpressionInterpSpeed);
        FaceMeshComponent->SetMorphTarget(Morph, Current);
    }
}
```

`SetExpressionTarget()`はFunction Call受信時に、`TickExpression()`はセクション10.5と同じ考え方でTick経路を確定した上で毎フレーム呼ぶ。`jawOpen`モーフターゲットはこの処理の対象に含めない(セクション13と同じ所有権の考え方)。

`RealtimeVoiceComponent`からこの関数を呼ぶには、`LipSyncComponent`への参照を`RealtimeVoiceComponent`に持たせる(または、
所有Actor経由で`FindComponentByClass<ULipSyncComponent>()`する)必要がある。既存の`jawOpen`呼び出し経路をそのまま踏襲すること。

## 5. 表情の減衰・リセット仕様

- 表情は**次に`express_emotion`が呼ばれるまで持続**させる(自動タイムアウトでの減衰は入れない、初期実装では)
- ただし`jawOpen`(口パク)の値と表情用カーブは**別の値として独立に管理**されており、干渉しない
  - 唯一の例外: `happy`の`mouthSmileLeft/Right`と口パクの`jawOpen`は同時に動くため、口が開きながら微笑む見た目になる。これは許容範囲とする(自然な発話中の表情として妥当)
- AIが `neutral` を明示的に呼ばない限り、前の表情が残り続ける点はプロンプト(Instructions)側で「感情が変わるたびに(neutralに戻る時も含めて)呼ぶこと」と明記することでカバーする(上記ツール定義のdescriptionに記載済み)

## 6. 割り込み時の扱い

- ユーザーが話し始めてAIの発話が中断された場合(`input_audio_buffer.speech_started`受信時、`StopPlaybackImmediately()`が呼ばれる箇所)、**表情は明示的にリセットしない**
  - 理由: 表情はAIの発話内容そのものより「その場の空気」に近く、話者が変わっただけで即座に真顔に戻すと不自然なため
  - 将来的に不自然さが気になれば、`HandleWebSocketMessage`内の`input_audio_buffer.speech_started`ハンドラで`ApplyExpression(TEXT("neutral"), 0.0f)`を呼ぶ形に変更する

## 7. 変更対象ファイルまとめ

**ARKit LiveLink経由(記録B)の場合:**

| ファイル | 変更内容 |
|---|---|
| `ARKitLiveLinkSubsystem.h` | `SetExpressionTarget(const FString&, float)`、`TickExpression(float)`、Current/Target状態を追加 |
| `ARKitLiveLinkSubsystem.cpp` | 感情→Target変換、Current→Target補間、`PushCurveValue()`呼び出しを追加 |
| `ARKitLiveLinkSource.h`/`.cpp` | **変更不要** |
| `RealtimeVoiceComponent.h` | `EExpressionApplyResult`、`ApplyExpression(...)`、Function Call output/Response管理用宣言を追加 |
| `RealtimeVoiceComponent.cpp` | `SendSessionUpdate()`へ既存toolsを維持したまま`express_emotion`追加。`HandleWebSocketMessage()`へ受信処理、結果判定、既存Function Calling共存処理を追加 |
| 既存Function Call管理部 | 必要に応じて、同一Response内のFunction Call output完了後に`response.create`を1回だけ送る管理へ`express_emotion`を参加させる。`ClearPendingFunctionCallState()`(セクション3.3.2)をWebSocket切断ハンドラおよびセクション12の初期化処理から呼ぶ |

**Blender直接モーフ方式(記録A)の場合:**

| ファイル | 変更内容 |
|---|---|
| `LipSyncComponent.h`等 | `SetExpressionTarget(const FString&, float)`、`TickExpression(float)`、Current/Target状態を追加(4.2〜4.3と同構成) |
| `LipSyncComponent.cpp`等 | 実在Morph Targetを使った表情適用・補間を追加(4b参照) |
| `RealtimeVoiceComponent.h/.cpp` | `ApplyExpression()`をDirect Morph方式向けに実装(`ULipSyncComponent`取得、`TargetComponentUnavailable`判定を含む。セクション2.1参照)。Function Calling受信、結果判定、既存Function Call管理との共存を追加 |

## 8. 動作確認の手順・期待するログ

1. ビルド後、`RealtimeTestActor`(または本体の会話Actor)でプレイし、Realtime APIに接続
2. 何か感情の動きそうな話題で会話する(例: 「I got promoted today!」のような明確に嬉しい話)
3. 出力ログで以下が順番に出れば成功:
   ```
   [EXPRESSION] Target更新: happy, intensity=0.XX
   ```
4. 画面上、Jenniferの口元・頬のあたりが変化するのを目視確認
5. うまく呼ばれない場合、`session.tools`が正しく送信されているか(`SendSessionUpdate`のログで`tools`を含むJSON文字列をダンプして確認するのが手軽)をまず疑う

## 9. 未確定・検証が必要な点(それでも残るリスク)

- `tool_choice: "auto"`にしても、AIが実際にどれくらいの頻度で`express_emotion`を呼んでくれるかは、実際に動かしてみないと分からない(呼ばなすぎる/呼びすぎる、いずれも起こりうる。Instructionsの文言調整で様子を見る必要がある)
- 上記のイベント名・JSON構造は本ドキュメント作成時点のOpenAI公式ドキュメント・コミュニティ報告に基づく。Realtime APIは変更が比較的多い分野のため、実装時に実際のレスポンスと食い違いがあれば、まずログに生JSONをダンプして実際のフィールド名を確認すること

## 10. 表情遷移の補間（必須）

表情カーブは目標値へ1フレームで即時切り替えず、**0.2〜0.5秒程度を目安に滑らかに補間**する。即時切替では `happy → sad` などの遷移時に顔が不自然に跳ねるためである。

Function Callingを受信した時点ではセクション4の `SetExpressionTarget()` によりTarget値だけを更新する。Current値は補間TickでTargetへ近づける。

```cpp
CurrentValue = FMath::FInterpTo(CurrentValue, TargetValue, DeltaTime, ExpressionInterpSpeed);
```

初期値は `ExpressionInterpSpeed = 6.0f` 程度とし、実機で調整する。

### 10.1 クロスフェード

新しい表情を適用するとき、前表情のTarget値を0へ戻してから新表情のTarget値を設定する。

**Current値を即座に0へ戻してはならない。**

これにより、例えば `happy → sad` は現在の笑顔Current値からsadのTarget値へ自然にクロスフェードする。

### 10.2 補間途中に新しいemotionを受信した場合

表情イベントはキューイングしない。

補間途中に新しい `express_emotion` を受信した場合は、**その時点のCurrent値を始点として、最新のTargetへ向かって補間を継続する。古いTargetは破棄する。**

例:

```text
happy 0.6
  ↓ 補間途中
surprised 0.8
  ↓
現在のCurrent値 → surprisedの最新Targetへ遷移
```

短時間に複数のFunction Callが来ても、古い表情を順番に再生して遅延を蓄積させない。

### 10.3 neutralの扱い

`emotion == "neutral"` の場合、`intensity` は意味を持たない。

- 受信側ではintensity値に関係なく、すべてのExpression Targetを0にする
- Instructionsでは `neutral` を呼ぶ場合は `intensity: 0` を使うよう指示する
- Current値は即時0にせず、通常と同じ補間でneutralへ戻す

### 10.4 TickExpressionの処理

概念コード:

```cpp
void UARKitLiveLinkSubsystem::TickExpression(float DeltaTime)
{
    if (!Source.IsValid())
    {
        return;
    }

    for (auto& Pair : ExpressionCurrentValues)
    {
        const FName Curve = Pair.Key;
        float& Current = Pair.Value;
        const float Target = ExpressionTargetValues.FindRef(Curve);

        Current = FMath::FInterpTo(
            Current,
            Target,
            DeltaTime,
            ExpressionInterpSpeed);

        Source->PushCurveValue(Curve, Current);
    }
}
```

`jawOpen` はこの処理の対象に含めない。

### 10.5 Tickを担当するクラス（実装前に確定すること）

`UGameInstanceSubsystem` 自体には通常のActorComponentのようなTickが自動的に存在するとは限らないため、**Claude Codeは既存コードを確認して、現在のプロジェクトで最も自然なTick経路を選ぶこと。**

優先順位:

1. `ARKitLiveLinkSubsystem` に既にTick可能な仕組みがある場合はそれを利用する
2. `RealtimeVoiceComponent` が既に `TickComponent()` を持ち、会話中継続してTickしている場合は、そこから `ARKitSubsystem->TickExpression(DeltaTime)` を呼ぶ
3. どちらも無い場合は、既存設計への影響が最小になるTick可能な仕組みを追加する

**新しい独立Actorを表情補間だけのために追加することは原則避ける。**

実装後、どのクラスがTick所有者になったかを `PROGRESS.md` または開発記録へ明記する。

**追加確認事項**: `RealtimeVoiceComponent::TickComponent()`を利用する場合、それが「会話していない時間帯(接続前・セッション終了後)」も動き続けているか、それとも会話中だけ有効化される設計かを確認する。会話していない間もTickが有効なコンポーネントであれば表情補間もそのまま同居できるが、会話中だけ有効/無効を切り替える設計であれば、その切り替えタイミングと`TickExpression`の呼び出しタイミングがずれないよう合わせて設計する。

## 11. Function Callingの異常系（必須）

`express_emotion`の受信処理では正常系だけでなく、以下を処理する。

- `call_id` が空の場合: 適用せずWarningログを出す
- `arguments` のJSONパース失敗(JSON構文自体が不正): 適用せずWarningログを出し、`call_id`があれば`status:error / reason:parse_failed`を返す
- JSONとしては正しくパースできたが、`emotion`または`intensity`フィールドが欠けている、または型が不正(`emotion`が数値、`intensity`が文字列など)な場合: `TryGetStringField`/`TryGetNumberField`はどちらのケースもfalseを返すため区別しない。適用せずWarningログを出し、`call_id`があれば`status:error / reason:missing_argument`を返す(`parse_failed`とは別の状態として区別する。「欠落」と「型不正」を別のreasonに分けたい場合は、その旨を明示的に決めてから実装すること)
- `emotion` がenum外の未知値: **現在の表情を維持し、neutralへ勝手に変更しない**
- `intensity` が範囲外(数値だが0〜1の外): `FMath::Clamp(Intensity, 0.0f, 1.0f)` で補正
- `intensity` が非有限値(NaN/±Infinity): `FMath::Clamp()`はNaNを正しく処理しない可能性があるため、Clampに頼らず`FMath::IsFinite()`で明示的に弾く。適用せず`status:error / reason:invalid_intensity`を返す
- `UARKitLiveLinkSubsystem`（ARKit方式）が取得できない: `status:error / reason:subsystem_unavailable`を返す
- `ULipSyncComponent`等の対象Component（Direct Morph方式）が取得できない: `status:error / reason:target_component_unavailable`を返す
- WebSocketが切断済みでFunction Call outputを返せない場合: クラッシュさせずログを残す(あわせて、切断時にはセクション3.3.2のPending Function Call状態クリアを必ず実行する)

未知のemotion等、ツール呼び出し自体は受信できたが適用できなかった場合は、**必ず適用結果に対応したerror outputを返す**（call_idが取得できない場合を除く）。

```json
{"status":"error","reason":"unknown_emotion"}
```

正常時は従来どおり:

```json
{"status":"applied"}
```

## 12. 表情状態の初期化（必須）

前セッションの表情が残らないよう、以下ではneutralへ初期化する。

**必須:**
- Realtime APIへ新規接続した時
- 新規会話セッションを開始した時

```cpp
ApplyExpression(TEXT("neutral"), 0.0f);
ClearPendingFunctionCallState(); // セクション3.3.2参照。Function Call管理側の状態もあわせてクリアする
```

neutralへの復帰もCurrent値の即時0クリアではなく、通常のTarget更新として扱う。ただし、まだ画面表示前で自然な遷移が不要な初期化段階では、Current/Targetの両方を0へ初期化してよい。

**場所・Scene移動ではneutralへリセットしない。**

教室・映画館・ジェニファーの部屋・ドライブ・レストラン等への移動は同一会話の連続性を優先し、前の表情状態を維持する。詳細はセクション19。

Realtime API切断時のneutral化は必須としない。切断後に同じ顔が画面に残る演出がある場合のみ、必要性を実機で判断する。ただし、**切断時のPending Function Call状態クリア(セクション3.3.2)は、neutral化の要否とは独立して必須とする**(表情の見た目を変えるかどうかと、内部のResponse管理状態を破棄するかどうかは別の話であるため)。

ユーザーの割り込み（`input_audio_buffer.speech_started`）でも従来仕様どおり即座にneutralへ戻さない。

## 13. jawOpenの所有権（必須）

**表情制御側から `jawOpen` を変更してはならない。**

`jawOpen` は既存のLipSync処理だけが所有する。`surprised` 等で口を開けた表情を作りたくても、Expression側から `jawOpen` をPushしない。

理由:

- 音声LipSyncとExpressionが同じカーブを書き換える競合を防ぐ
- 発話中の口の開閉を壊さない
- LipSyncと表情制御の責務を分離する

必要であれば `mouthFunnel`、`mouthPucker` 等の別カーブを将来的に検討するが、導入前にJenniferのFaceで単体動作確認を行う。

## 14. ARKit表情カーブの単体動作確認（実装前に強く推奨）

`jawOpen` が動作していても、表情用ARKitカーブが現在のJenniferのFace/RigLogicで期待どおり反映される保証はない。そのためFunction Calling実装の前、または並行して、主要カーブを1つずつ固定値でPushして目視確認する。

最低限確認するカーブ:

```text
mouthSmileLeft / mouthSmileRight
cheekSquintLeft / cheekSquintRight
browInnerUp
browDownLeft / browDownRight
eyeWideLeft / eyeWideRight
mouthFrownLeft / mouthFrownRight
mouthLeft / mouthRight
cheekPuff
eyeSquintLeft / eyeSquintRight
mouthPressLeft / mouthPressRight
```

例:

```cpp
Source->PushCurveValue(TEXT("mouthSmileLeft"), 1.0f);
```

各カーブについて「実際に動いた／ほぼ変化なし／不自然」を記録する。効かないカーブがあれば、感情マッピングを実際に有効なカーブだけで再構成する。

## 15. express_emotionの呼び出し頻度（Instructions追加）

ツール定義のdescriptionだけではモデルが表情を頻繁に変更する可能性があるため、既存の人格Instructionsと自然に繋がる位置へ、以下と同等の方針を追加する。

```text
Use express_emotion only when your emotional tone meaningfully changes.
Do not call it for every sentence or minor nuance.
In normal conversation, prefer subtle intensity values around 0.2 to 0.6.
Use intensity above 0.7 only for clearly strong emotional reactions.
Call express_emotion with neutral when you genuinely return to a neutral emotional state.
For neutral, always use intensity 0.
```

実際の呼び出し頻度をログで確認し、少なすぎる／多すぎる場合はInstructionsを調整する。

## 16. 音声再生と表情変更のタイミング（初期実装の許容事項）

Realtime APIのFunction Callイベント到着時刻と、対応する音声がユーザーに聞こえる時刻は完全には一致しない可能性がある。

初期実装では、`response.function_call_arguments.done` を受信した時点で表情Targetを更新し、**音声との数百ms程度のずれは許容する**。まずはFunction Callingによる表情制御が安定して動くことを優先する。

動作確認で「音声より表情が明らかに遅れる／早すぎる」問題が目立つ場合は第2段階として、以下を検討する。

- 音声再生キューとExpression変更を同じタイムラインで管理する
- Function Call受信時刻とaudio delta再生位置をログに記録する
- Expressionイベントを短時間キューして音声開始に合わせる

**初期実装ではこの厳密同期機構までは必須としない。**

## 17. 追加の動作確認・合格条件

既存の「8. 動作確認」に加えて以下を確認する。

1. セッション開始時にneutralである
2. `happy 0.7` を呼ぶと、瞬間移動ではなく滑らかに笑顔へ遷移する
3. `happy → sad` で自然にクロスフェードする
4. 発話中に表情を変えても `jawOpen` のLipSyncが継続する
5. `surprised` を適用してもExpression側が `jawOpen` を上書きしない
6. 未知のemotionをテスト入力しても現在の表情が突然neutralにならない
7. intensityが `-1.0` / `2.0` でもクラッシュせず0〜1へClampされる
8. ユーザー割り込み後も表情が即座に真顔へ戻らない
9. 新規セッション開始時には前セッションの表情が残らない
10. 通常会話で `express_emotion` が毎文呼ばれるような過剰動作をしていない
11. 補間途中に別emotionが来た場合、古い表情をキュー再生せず最新Targetへ遷移する
12. `neutral` はintensityに関係なく全Expression Target=0として扱われる
13. 既存の場所移動Function Callingと併用しても `response.create` の二重送信や二重音声応答が発生しない
14. Phase 6のカメラ外最適化を導入した場合、画面外でemotionが変わっても再表示時に古い表情へ戻らない
15. 未知emotionで `ApplyExpression()` が成功扱いにならず、`status:error / unknown_emotion` が返る
16. Subsystem取得失敗で `status:applied` を返さない
17. 同一Response内に複数Function Callがあっても `response.create` は原則1回だけ送信される
18. `session.update`に`model`フィールドを含めず、既存Realtime接続時のモデル指定を壊さない
19. `emotion`または`intensity`が欠けている、または型が不正なargumentsを受け取っても、適用せず`status:error / reason:missing_argument`が返る(パース自体は成功しているケースなので`parse_failed`とは区別される)
20. `intensity`にNaN / ±Infinity相当の不正値を受けても表情状態を破壊せず、`status:error / reason:invalid_intensity`が返る
21. Direct Morph方式(記録A)の場合、対象Componentが取得できないと`status:error / reason:target_component_unavailable`が返り、ARKit方式の`subsystem_unavailable`と区別される
22. `call_id`が空のFunction Callが同一Response内に複数(同名含む)あっても、`item_id`または`response_id`+`output_index`で個別に完了扱いとなり、取りこぼしや二重カウントが起きない
23. WebSocket切断後に再接続した場合、切断前のResponseに関するPending Function Call状態(期待Call数、完了済みキー、未送信output、`response.create`送信済みフラグ)が残っておらず、新しいセッションのFunction Call完了判定に影響しない

## 17.5 Jennifer側で追加確認する自然表情要素

本仕様の`express_emotion`は主に感情表情を担当する。Jenniferを自然に見せるためには、以下の要素も別レイヤーとして確認する。

### 17.5.1 Face AnimBP / Post Process / RigLogic競合確認

JenniferのFaceを同時に駆動する仕組みを確認する。

- Face Anim Class
- Post Process AnimBP
- RigLogic / DNA
- LiveLink
- Control Rig
- Sequencer
- Idle Face Animation

同じカーブやFace Controlを複数経路から書き換えている場合、本仕様のExpression値が上書きされる可能性がある。

### 17.5.2 自動まばたき

Blinkは`express_emotion`とは独立管理する。

- `eyeBlinkLeft`
- `eyeBlinkRight`

がJenniferで有効かを単体確認する。

通常会話中のBlinkはemotion変更のたびにAIへ呼ばせず、Jennifer側の自律アニメーションとして扱う。

### 17.5.3 視線 / Head Look At

Jenniferが会話相手を自然に見る仕組みを確認する。

既存Look At制御がある場合はそれを優先して再利用し、Expression側で目線を直接所有しない。

### 17.5.4 左右非対称

初期実装では左右同値でよい。

自然さが不足する場合のみ、emotion開始時に数%程度の左右差を加えることを検討する。毎フレームランダム値を加える方式は避ける。

### 17.5.5 強い表情の長時間保持

初期仕様ではAIがneutralを呼ぶまで表情を維持する。

実機でsurprised等の強い表情が長時間残る問題が目立つ場合のみ、一定時間後にTarget強度を弱める安全弁をPhase 5以降で検討する。

## 18. 推奨実装順序（重要）

実装は、**「Jennifer側の顔が手動入力で確実に動くことを先に確認し、その後にRealtime APIを接続する」**順序で進める。

AI連携を先に実装すると、表情が動かない原因が「ARKitカーブ」「AnimBP/RigLogic」「補間」「Function Calling」「APIイベント」のどこにあるか切り分けにくくなるため、以下の順序を推奨する。

### Phase 1: Jennifer側の顔制御を確定する

1. **現在のLipSync経路を確定する**
   - ステップ0の確認を実施する
   - ARKit LiveLink経由か、Blender直接Morph方式かを確定する
   - `jawOpen`が実際にどこからFaceへ届いているか確認する
   - **ステップ0.5の確認もあわせて実施する**: `RealtimeVoiceComponent`のアタッチ先、現在のInstructions全文、そして特に`HandleWebSocketMessage()`がどのスレッドで呼ばれているか(WebSocketのI/Oスレッドで直接UObject/TMapを触るとクラッシュ・競合の原因になるため)を確認する

2. **JenniferのFace制御構成を確認する**
   - Face Anim Class
   - Post Process AnimBP
   - RigLogic / DNAの有無
   - LiveLink Subject設定
   - Face Skeletal Mesh
   - 既存Idle / Control Rig / Sequencer等がFaceを上書きしていないか

3. **主要ARKitカーブを手動で1個ずつPushする**
   - `mouthSmileLeft / Right`
   - `browInnerUp`
   - `browDownLeft / Right`
   - `eyeWideLeft / Right`
   - `mouthFrownLeft / Right`
   - その他セクション14の主要カーブ
   - Jenniferで実際に「動く / ほぼ効かない / 不自然」を記録する

このPhaseでは**Realtime APIをまだ使わない**。

### Phase 2: 表情エンジン単体を完成させる

4. **Target / Current方式の表情制御を実装する**
   - `SetExpressionTarget()`
   - `TickExpression()`
   - `Current → Target`補間
   - `jawOpen`はExpression側から触らない
   - 補間途中の新emotionは最新Targetで上書きする

5. **手動テスト経路を実装する**
   - `TestExpression happy 0.7`
   - `TestExpression sad 0.5`
   - `TestExpression neutral 0`
   - AIを待たずに任意のemotion/intensityを直接試せるようにする

6. **表情マッピングと補間速度を調整する**
   - happy / sad / surprised / confused / embarrassed / neutral
   - 必要に応じてDataTable化
   - `ExpressionInterpSpeed`をCVar等で調整可能にする
   - この時点で「手動入力ならJenniferの表情が自然に変わる」状態を完成させる

### Phase 3: Realtime API / Function Callingを接続する

7. **Realtime APIの`express_emotion`ツールを追加する**
   - `session.update`へ既存toolsを維持したまま追加
   - `model`は`session.update`へ追加しない
   - Instructionsへemotion呼び出し頻度の指示を追加する
   - Phase1で確認済みのWebSocketコールバックスレッドの扱いに沿って、`SetExpressionTarget`/`TickExpression`の呼び出しがゲームスレッド上で行われることを再確認する

8. **Function Call受信と適用結果処理を実装する**
   - `response.function_call_arguments.done`
   - arguments JSONパース
   - `EExpressionApplyResult`
   - `status:applied / error`
   - unknown emotion / invalid world / subsystem unavailable等を正しく返す

9. **既存Function Callingとの共存を確認する**
   - 場所移動Function Call等を壊さない
   - `function_call_output`を既存共通処理へ統合する
   - 同一Response内の複数Function Callを確認する
   - `response.done`等の実イベントでFunction Call一覧を確認する
   - `response.create`を同一Responseにつき原則1回だけ送る

### Phase 4: LipSyncと実会話の統合確認

10. **LipSyncと表情を同時動作させる**
    - happyのまま会話する
    - sad / surprised中に会話する
    - 発話途中にemotionが変化する
    - `jawOpen`がExpression側に上書きされないことを確認する

11. **ユーザー割り込み・場所移動・新規セッションを確認する**
    - ユーザー割り込みで即neutralにしない
    - 場所移動で表情を保持する
    - 新規Realtime接続 / 新規会話セッションではneutral初期化する
    - Actor / Face再生成時は最新Targetを再適用する

ここまでを**表情制御本体の完成条件**とする。

### Phase 5: Jenniferの自然さを改善する

12. **自動まばたきを追加する**
    - 感情Function Callingとは独立したレイヤーで実装する
    - `eyeBlinkLeft / Right`を使用できるか単体確認する
    - 数秒間隔の自然なランダムBlinkを基本とする
    - Emotion側からBlinkカーブを恒常的に占有しない

13. **Eye / Head Look Atを確認・追加する**
    - 会話相手を見る
    - カメラ真正面固定にならないようにする
    - 既存Look At制御がある場合は再利用する
    - 表情制御とは別責務にする

14. **微細表情・左右非対称を必要に応じて追加する**
    - 左右完全対称の表情を少し崩す
    - emotion開始時に小さな左右差を設定する程度に留める
    - 毎フレームランダムに揺らさない

15. **必要な場合のみ強い表情の自然減衰を検討する**
    - AIがneutralを呼び忘れた場合に強い表情が長時間残る問題が実機で目立つ場合のみ導入
    - 初期実装では自動タイムアウトは入れない

### Phase 6: 必要な場合のみ高度化する

16. **音声と表情の厳密同期**
    - 表情が音声より明らかに早い / 遅い場合のみ対応する
    - audio delta再生位置とExpression Event時刻をログ比較する
    - 必要ならExpressionイベントを短時間キューする

17. **カメラ外最適化**
    - 実機プロファイリングで負荷が問題になった場合のみ実装する
    - Target更新はカメラ外でも必ず保持する
    - 再表示時に古い表情へ戻らないことを保証する

### 実装順序の原則

```text
JenniferのFaceが手動で動く
    ↓
表情補間が自然に動く
    ↓
AIを使わずemotion切替が完成
    ↓
Realtime APIを接続
    ↓
LipSync / 場所移動と共存
    ↓
Blink / Look At / 微細表情
    ↓
必要な場合のみ同期・最適化
```

**重要: 最初からAI経由で表情テストを始めない。**

まず `TestExpression` 等の手動経路でJenniferの顔制御単体を完成させてからRealtime APIへ接続すること。これにより、問題発生時の原因切り分けを容易にする。

## 19. 場所(シーン)移動時の表情の扱い

教室・映画館・ジェニファーの部屋・ドライブ・レストランなど、複数の場所を移動する仕組みが既に存在する。

場所を移動しても、**表情は明示的にリセットしない**。同一Realtime会話セッションが継続している限り、最新のCurrent/Target表情状態を持ち越す。

理由: 場所が変わっただけでキャラクターの感情が強制的にneutralへ戻るのは不自然なため。会話の連続性を優先する。

**【矛盾修正】以前の版では「Current/Targetを持ち越す」と述べた直後、Actor再生成時は「最新Target状態を再適用」とだけ書かれており、その場合Currentをどう扱うかが未定義だった(例えば移動直前がCurrent=happy 0.5、Target=sad 0.8だった場合、新しいFaceをCurrent=0から始めるのか、旧Currentから再開するのかが決まっていなかった)。以下のとおり一本化する。**

- **感情状態として保持・持ち越す対象はTargetのみとする。** `RealtimeVoiceComponent` / `ARKitLiveLinkSubsystem`側が保持する最新のExpressionTargetValuesが、その時点での「Jenniferが今どんな感情でいるか」の唯一の正とする
- Jennifer ActorまたはFace Componentが破棄・再生成されない通常の場所移動では、CurrentもTargetも既存のTMapがそのまま生き続けるため、特別な処理は不要(補間もそのまま継続する)
- Jennifer ActorまたはFace Component自体が破棄・再生成される実装の場合、**新しいFaceのCurrent値はすべて0から開始し、保持している最新Targetへ向けて通常の補間(セクション10)で遷移させる**。旧Currentの値(補間途中の状態)は再生成をまたいで引き継がない
  - 理由: 新しいFace Componentは内部的に別のTMap(またはゼロ初期化された状態)を持つことになるため、旧Currentを技術的に引き継ぐには追加の受け渡し処理が必要になり複雑化する。Targetさえ正しく引き継げば、数百ms程度の補間で見た目上は違和感なく収束するため、実装をシンプルに保つ

つまり「感情そのもの(Target)は場所移動をまたいで持続するが、補間の途中経過(Current)は場所移動でActorが再生成された場合はリセットされ、そこから通常の速度で目標値へ近づく」という扱いになる。

## 20. カメラに映っていない時の表情計算（Phase 6の任意最適化）

**初期実装ではこの最適化を行わない。**

Expressionカーブ数は限定的であり、まずは常時補間・LiveLink送信する単純な実装で正しさと自然さを優先する。実機プロファイリングで表情処理が無視できない負荷だと確認された場合のみ、Phase 6として最適化を検討する。

最適化する場合も、Jenniferが画面外だからといってFunction Callingによる表情状態そのものを捨ててはならない。

必須ルール:

1. `express_emotion` 受信時の**Target更新は、カメラ表示状態に関係なく必ず行う**
2. 省略してよいのは、画面外での毎フレーム補間／LiveLink Push等の表示用処理だけ
3. Jenniferが再び表示された時は、保持している最新Targetへ正しく追従する
4. 古い表情のまま再登場しないことをテストする

単純な実装では、画面外でCurrent補間も停止し、再表示時にその時点のCurrentから最新Targetへ補間を再開してよい。ただし、再登場直後の古い表情が目立つ場合は、非表示中もCurrentだけ時間経過に合わせて更新する、または再表示時に最新Targetへ同期する方式を検討する。

口パク(`jawOpen`)の最適化は既存LipSync実装への変更になるため、本仕様の対象外とする。

## 21. 表情専用のログタグ

口パクの`[TIMING]`ログ(録音停止からの経過時間を計測するもの)と同じ考え方で、表情用にも専用のログタグを設ける。

- タグ例: `[EXPRESSION]`
- 記録するタイミング・内容の例:
  ```
  [EXPRESSION] Function Call受信: emotion=happy, intensity=0.70, call_id=call_XXXX
  [EXPRESSION] 適用開始: happy (target補間開始)
  [EXPRESSION] 適用完了: happy (補間収束)
  [EXPRESSION] スキップ: カメラ非アクティブのため計算省略
  [EXPRESSION] エラー: 未知のemotion "excited" を受信、現在の表情を維持
  ```
- 目的: 「表情が変わらない」「表情がずれている」といった不具合が起きた際に、
  出力ログを`[EXPRESSION]`で検索するだけで、Function Call受信〜適用までの流れを追えるようにする
- 既存の`[TIMING]`ログとタグを分けることで、口パクと表情のどちらの問題か切り分けやすくする
- `[EXPRESSION]` と音声再生開始・`[TIMING]` ログは、可能な限り同じ時刻基準（同じ経過時間計測元）を使う。これによりセクション16の音声と表情のずれをms単位で比較できる

## 22. 容易にチューニングするための仕組み（推奨・必須ではない）

セクション4.3の係数（`happy`の`mouthSmileLeft = I * 0.7f`等）は全て仮値であり、セクション14のカーブ単体確認を含め、実機で繰り返し調整する前提になっている。「係数を変える→C++再ビルド→PIE再生→AIに話しかけて`express_emotion`が呼ばれるのを待つ→目視確認」という往復はコストが高いため、以下の仕組みをあわせて実装することを推奨する。必須の機能要件ではないため、実装の優先度はセクション18の本体実装より低く置いてよい。

### 22.1 感情→カーブ係数のマッピングをDataTable化する

セクション4.3のようにC++へ係数を直書きすると、係数を1つ変えるだけでも再コンパイルが必要になる。代わりに`UDataTable`（行=感情、列=カーブ名+係数）に切り出し、`SetExpressionTarget`側は「Emotion名で該当行を検索し、そこに書かれたカーブ名・係数を使ってTargetを設定する」という汎用ループにする。

```
Emotion,   CurveName,        Coefficient
happy,     mouthSmileLeft,   0.7
happy,     mouthSmileRight,  0.7
happy,     cheekSquintLeft,  0.3
...
```

```cpp
// このコード自体は係数の具体的な数値を一切知らない。数値はすべてDataTableアセット側にある。
for (const FExpressionCurveRow& Row : GetRowsForEmotion(Emotion))
{
    ExpressionTargetValues.FindOrAdd(Row.CurveName) = Intensity * Row.Coefficient;
}
```

これにより、係数の調整はUE5エディタ上でDataTableアセットを開いて数値を書き換えて保存するだけで完結し、C++の再ビルドが不要になる。新しい感情を追加する場合も、コード変更なしでDataTableに行を足すだけで対応できる。

### 22.2 AIとの会話を待たずに任意の感情を手動発火できるコマンドを用意する

セクション14の単体カーブ確認や係数調整のたびに「AIに話しかけてexpress_emotionが呼ばれるのを待つ」のは非効率なため、`express_emotion`のAI経由の入口をバイパスして直接表情を適用できる`Exec`関数を用意する。

```cpp
UFUNCTION(Exec)
void TestExpression(FString Emotion, float Intensity);
```

PIE中に、`Window > Developer Tools > Output Log`で開いたOutput Logウィンドウ下部のコマンド入力欄（コンソールキーの配置がキーボードにより異なるため、こちらの方が確実）から

```
TestExpression happy 0.7
```

のように打つことで、狙った感情・強さをその場で何度でも即座に試せる。22.1のDataTable化と組み合わせると、「DataTableの数値を変える→保存→`TestExpression`で打ち直す」というループのみで、AIの発話待ちや再ビルドを挟まずに調整できる。

### 22.3 補間速度をコンソール変数化する

`ExpressionInterpSpeed`（セクション4.2、初期値6.0f）も固定値のままだと調整のたびに再ビルドが要る。`TAutoConsoleVariable`としてCVar化する。

```cpp
static TAutoConsoleVariable<float> CVarExpressionInterpSpeed(
    TEXT("expression.InterpSpeed"), 6.0f, TEXT("表情補間速度"));
```

PIE中にOutput Logから`expression.InterpSpeed 3.0`のように打てば、ビルドなしで遷移の速さを比較できる。

### 22.4 主要カーブのCurrent値を画面にオーバーレイ表示する

`TickExpression`のループ内で、主要な表情カーブのCurrent値を`GEngine->AddOnScreenDebugMessage`等でリアルタイム表示する。「係数を変えたつもりが実は反映されていない」「補間が終わる前に次のemotionが来て意図通りクロスフェードしているか」を目視の印象ではなく数値で確認できる。セクション14の「動いた／ほぼ変化なし／不自然」の判定も、このオーバーレイを見ながら行うと主観のブレを減らせる。

### 22.5 位置づけ

22.1〜22.4はいずれも本番の会話フローには関与しない、開発中のみ使うデバッグ経路である。`TestExpression`やCVarはリリースビルドで無効化する、または`WITH_EDITOR`等のマクロで囲うことを検討してよい（本仕様では必須要件としない）。

