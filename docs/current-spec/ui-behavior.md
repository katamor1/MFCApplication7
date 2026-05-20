# UI 動作仕様

## 画面構成

現行 UI は `CMainDialog` 上に次のコントロールを配置します。

| 領域 | コントロール | 現行役割 |
|---|---|---|
| 上部 | `statusText_` | 画面名、重要更新回数、期限超過、通常更新回数、Write 結果、履歴状態などを表示する。 |
| 上部右 | `historyProgress_` | 履歴取得進捗を 0-100 で表示する。 |
| 左端 | `navButtons_` | 5画面を切り替える。 |
| 左端下 | `expandButton_` | ナビの1列/2列配置を切り替える。 |
| 中央 | `contentList_` | 現在画面のグリッドを `CListCtrl` として表示する。 |
| 右側 | `detailText_` | ステーション画面だけ表示される選択コンテナ詳細欄。 |
| 下部 | `functionButtons_` | F1-F8 相当の操作ボタン。 |

現行実装はダイアログ起動時に 1280x800 の初期サイズへ移動し、サイズ変更時は `LayoutControls()` で再配置します。

## 画面一覧

`MainScreenId` は5画面を定義しています。

| 画面 | ナビ表示 | 現行表示内容 |
|---|---|---|
| `Station` | `ST` | コンテナ一覧グリッドと右側の選択コンテナ詳細テキスト。 |
| `ContainerList` | `LIST` | コンテナ一覧グリッド。 |
| `Schedule` | `SCH` | コンテナ、品目名、出庫終了予定、順序のスケジュールグリッド。 |
| `System` | `SYS` | 履歴状態行と取得済み履歴レコード一覧。 |
| `Maintenance` | `MNT` | 重要情報1000-1019の保守用一覧。 |

## ナビゲーション

左ナビは通常1列です。展開ボタンを押すと `navExpanded_` が反転し、5個のナビボタンを2列配置にします。画面を選択すると `SwitchScreen()` により `navExpanded_` は false に戻ります。

現行実装では、ナビ展開は画面上に重なるオーバーレイではなく、左ナビ幅を広げるレイアウト変更です。将来要求の「メイン画面上に一時的に2列表示」とは完全一致していません。

## ステータス表示

`RefreshStatus()` は次の情報を1行文字列として組み立てます。

- 現在画面名。
- critical 更新回数。
- critical 期限超過回数。
- normal 更新回数。
- 最終 Write 開始遅延、Write 完了数、最終 Write 結果。
- 履歴状態、履歴進捗、履歴 Read 件数、履歴エラー件数。
- 先頭 critical value がエラーの場合、そのエラー表示。

日時、時刻、ユーザー名、業務ステータスの意味づけ表示は現行未実装です。

## F1-F8 ファンクションバー

現行の F1-F8 は画面下部の MFC ボタンです。`FunctionBarModel` が8枠の `FunctionAction` を返し、`RefreshFunctions()` がラベルと有効/無効を反映します。

| 画面 | Slot | ラベル | 有効条件 | 現行動作 |
|---|---:|---|---|---|
| Station / ContainerList | F1 | 詳細 | 選択あり、かつコンテナなしでない | メッセージボックスで詳細表示予定を通知する。 |
| Schedule | F1 | 詳細 | 行選択あり | メッセージボックスで詳細表示予定を通知する。 |
| Schedule | F2 | 順序変更 | 行選択あり | `OrderEditDialog` を開き、`2103` へ Write 要求を積む。 |
| Schedule | F3 | 追加 | 常に有効 | `ScheduleAddDialog` を開き、`2104` へ Write 要求を積む。 |
| Schedule | F4 | 削除 | 行選択あり | `ScheduleDeleteConfirmDialog` を開き、`2105` へ Write 要求を積む。 |
| Schedule | F5 | 繰上げ | 行選択あり、かつ直前表示行と順序入れ替え可能 | 選択行と直前表示行の `2103` raw 値を2件 Write して入れ替える。 |
| System | F1 | 履歴取得 / 取得中 | 履歴停止中 | `HistoryRequestDialog` を開き、履歴取得を開始する。 |
| System | F2 | 中断 | 履歴実行中 | 履歴取得キャンセルを要求する。 |
| Maintenance | 全枠 | 空 | 無効 | 現行操作なし。 |

`CMainDialog::PreTranslateMessage()` は `VK_F1` から `VK_F8` を `FunctionSlotFromVirtualKey()` で F1-F8 のスロットへ変換し、有効な画面下部ボタンと同じ `OnFunctionCommand()` へ流します。無効なファンクション操作に対応するキー入力は何もしません。

## グリッド表示

画面表示の表データは Core の `GridModel` で表現されます。

- `GridModel` は列、行、セル、行バインディングを保持する。
- `GridCell` は `CellKind` を持つ。
- `CellKind` には read-only text、text、spin、combo、radio、checkbox などの種別がある。
- `CCustomGridCtrl::ApplyModel()` が `GridModel` を `CListCtrl` の列と行へ反映する。

現行の `CCustomGridCtrl` はセル種別をエディタ表示へは使っていません。`CListCtrl` 上の文字列表現として表示します。スピン、コンボ、ラジオ、チェックボックスのセル内編集は未実装です。

## 画面別の現行動作

### ステーション画面

`PopulateStation()` は `BuildContainerListGrid(snapshot)` を中央グリッドへ表示し、右側に選択コンテナの詳細テキストを表示します。

右側詳細には次を表示します。

- 選択コンテナ番号。
- 名称。
- 状態。
- 最大5品目の品目名、入庫日、出庫開始、順序、作業時間。

現行では物理配置を模した半円/直線レイアウトや専用コンテナコントロールは未実装です。

### コンテナ一覧画面

`BuildContainerListGrid()` により、番号、名称、状態の3列で100コンテナを表示します。現行は単一の `CListCtrl` レポート表示であり、要求上の3列カード配置ではありません。

### スケジュール画面

`BuildScheduleGrid()` により、全コンテナを走査して品目行を作ります。出庫順序列は `CellKind::Spin` としてモデル化されます。

表示行は `2103` の raw 値を正の整数として解釈し、出庫順序の昇順に並べます。同じ順序の場合は containerNo、itemNo の昇順で安定表示します。非数値または0以下の順序は末尾へ送ります。

F2 順序変更では、選択行の `GridRowBinding` から containerNo と itemNo を取り出し、`2103` へ raw style で Write 要求を積みます。Write 完了数が変わると、次回 UI 更新でグリッド再構築が強制されます。

F5 繰上げでは、選択行と直前表示行の `2103` raw 値を入れ替える2件の Write 要求を積みます。先頭行、無効 binding、非数値順序ではF5は無効です。

### システム画面

システム画面は履歴取得の状態と履歴レコードを表示します。

最初の行は履歴ステータスです。その後に `UpdateSnapshot::historyRecords` の内容を、日オフセット、番号、値、状態として表示します。履歴レコードは最大500件です。

外部アプリ起動機能は現行未実装です。

### メンテナンス画面

メンテナンス画面は `1000-1019` の重要情報を、項目、値、操作可の3列で表示します。異常コンテナ単位の詳細ステータス表示や管理者向け手動操作支援は現行未実装です。
