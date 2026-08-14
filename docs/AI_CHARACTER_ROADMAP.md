# AIキャラクター開発ロードマップ

LifeSimulationプロジェクトにおける、女性キャラクター（会話AI）実装の開発方針。

## 方針

- 会話エンジンは **ChatGPT API直結** で構築する（Convai / Inworld AIなどの既製プラグインは採用しない）
  - 理由：好感度・所持金・予定・車・仕事といったゲーム状態をプロンプトに埋め込み、ゲーム世界を理解した会話をさせたいため。既製プラグインだとバックエンドがブラックボックス化し、状態管理との統合の自由度が下がる。
- 見た目（MetaHumanの顔）は **KeenTools FaceBuilder** で自分の写真から3Dメッシュを作り、Mesh to MetaHumanでUE5に取り込む方針
  - 料金：Freelancerライセンス 月$18 / 年$179（2026年時点）
  - ただし着手はPhase3以降。会話が成立することを先に確認する。

## フェーズ

### Phase1：会話の土台を作る
UE5 + MetaHuman（デフォルトキャラでOK）+ ChatGPT API + OpenAI TTSを組み合わせ、「話しかけると自然に返事が返ってくる」状態を作る。見た目や記憶機能は後回しにし、まず会話が成立するかを確認する。

### Phase2：ゲーム状態を会話に反映させる
好感度・所持金・今日の予定・車・仕事などのゲームデータを管理する仕組みを作り、ChatGPT APIへのプロンプトに埋め込む。「今日は筑波行こうよ」のようにゲーム世界を理解した発言ができるようにする。

### Phase3：見た目を作り込む
KeenTools FaceBuilderで写真から3Dメッシュを作成し、Mesh to MetaHumanでUE5に取り込む。会話の土台が固まってから着手することで、見た目の作り直しリスクを減らす。
最初MeshyのようなWebサービスで3Dメッシュを作ったこっちがよさそう。
https://www.meshy.ai/ja/workspace?model-tab=image-to-3d

### Phase4：演出を磨く
表情・目線・手の動き・車への乗り降りといった演出面を追加し、キャラクターの存在感を高める。

## 技術メモ

- 音声認識：Whisper API（自作予定）
- 対話生成：ChatGPT API
- 音声合成：OpenAI TTS（予定）
- リップシンク：Runtime MetaHuman Lip Sync（予定）
