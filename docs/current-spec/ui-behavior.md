# UI 動作仕様

## 画面構成

現行 UI は `CMainDialog` 上に次のコントロールを配置します。

| 領域 | コントロール | 現行役割 |
|---|---|---|
| 上部 | `statusText_` | 日時、ユーザー名、画面名、業務状態、重要情報状態、更新回数、Write 結果、履歴状態などを表示する。 |
| 上部右 | `historyProgress_` | 履歴取得進捗を 0-100 で表示する。 |
| 左端 | `navButtons_` | 5画面を切り替える。 |
| 左端 | `navOverlay_` | ナビ展開時だけ、メイン画面上に重なる2列背景を表示する。 |
| 左端下 | `expandButton_` | ナビの1列/2列配置を切り替える。 |
| 中央 | `contentList_` | Schedule/System/Maintenance のグリッドを `CListCtrl` として表示する。 |
| 中央 | `stationLayout_` | Station 画面だけ表示される固定5列x20行のコンテナ配置図。 |
| 中央 | `containerListLayout_` | ContainerList 画面だけ表示される3列スクロール式コンテナカード一覧。 |
| 右側 | `detailText_` | ステーション画面だけ表示される選択コンテナ詳細欄。 |
| 下部 | `functionButtons_` | F1-F8 相当の操作ボタン。 |

現行実装はダイアログ起動時に 1280x800 の初期サイズへ移動し、サイズ変更時は `LayoutControls()` で再配置します。

## 画面一覧

`MainScreenId` は Core の `NavigationModel` で5画面を定義しています。

| 画面 | ナビ表示 | 現行表示内容 |
|---|---|---|
| `Station` | `ST` | 固定5列x20行のコンテナ配置図と右側の選択コンテナ詳細テキスト。 |
| `ContainerList` | `LIST` | コンテナ番号昇順の3列スクロール式コンテナカード一覧。 |
| `Schedule` | `SCH` | コンテナ、品目名、出庫開始予定、出庫終了予定、順序のスケジュールグリッド。 |
| `System` | `SYS` | 固定定義の外部アプリ行、履歴状態行、取得済み履歴レコード一覧。 |
| `Maintenance` | `MNT` | 重要情報1000-1019の保守ステータス一覧と異常行の詳細表示。 |

## ナビゲーション

左ナビは `BuildDefaultNavigationItems()` が返すナビ項目からボタンを生成します。通常時は `BuildNavigationCells(..., expanded=false)` により1列配置です。展開ボタンを押すと `navExpanded_` が反転し、`BuildNavigationCells(..., expanded=true)` により2列配置になります。

展開時は `navOverlay_` を左端から2列幅で表示し、メイン画面を横に押し広げずに中央コンテンツ上へ重ねます。背景の現在画面は更新継続しますが、左側の一部はオーバーレイに隠れます。画面を選択すると `SwitchScreen()` により `navExpanded_` は false に戻り、1列表示へ戻ります。

## ステータス表示

`RefreshStatus()` は Core の `BuildStatusSummary()` で2行の共通ステータス文字列を組み立て、`statusText_` へ表示します。

- 1行目: ローカル日時、Windowsログオンユーザー名、現在画面名、業務状態、重要情報状態。
- 2行目: critical 更新回数、critical 期限超過回数、normal 更新回数、Write/予定Write/履歴メトリクス。

業務状態はV1では `dataId=1000` の正常値を表示します。`dataId=1000` が未取得、空文字、エラー、stale の場合は `状態不明` と表示します。重要情報状態は `catalog.CriticalKeys()` と `UpdateSnapshot::criticalValues` を同じ順序で対応付け、エラー、stale、未取得を重要異常として数えます。正式な業務状態ID、警報種別、色/点滅/音などの異常表現は未実装です。

## F1-F8 ファンクションバー

現行の F1-F8 は画面下部の MFC ボタンです。`FunctionBarModel` が8枠の `FunctionAction` を返し、`RefreshFunctions()` がラベルと有効/無効を反映します。

| 画面 | Slot | ラベル | 有効条件 | 現行動作 |
|---|---:|---|---|---|
| Station / ContainerList | F1 | 詳細 | 選択あり、かつコンテナなしでない | 読み取り専用のコンテナ詳細ダイアログを開く。 |
| Schedule | F1 | 詳細 | 行選択あり | 読み取り専用のスケジュール詳細ダイアログを開く。 |
| Schedule | F2 | 順序変更 | 行選択あり、かつSchedule mutation pendingなし | `OrderEditDialog` を開き、`2103` へ Write 要求を積む。 |
| Schedule | F3 | 追加 | Schedule mutation pendingなし | `ScheduleAddDialog` を開き、`2104` へ Write 要求を積む。 |
| Schedule | F4 | 削除 | 行選択あり、かつSchedule mutation pendingなし | `ScheduleDeleteConfirmDialog` を開き、`2105` へ Write 要求を積む。 |
| Schedule | F5 | 繰上げ | 行選択あり、直前表示行と順序入れ替え可能、かつSchedule mutation pendingなし | 選択行と直前表示行の `2103` raw 値を2件 Write して入れ替える。 |
| Schedule | F6 | 再採番 | Schedule行あり、かつSchedule mutation pendingなし | 現在の表示順に `10,20,30...` を割り当て、変化がある行だけ `2103` raw Writeを積む。 |
| Schedule | F7 | Undo | Undo履歴あり、かつSchedule mutation pendingなし | 直近のSchedule操作を逆Writeで取り消す。redoはない。 |
| System | F1 | 履歴取得 / 取得中 | 履歴停止中 | `HistoryRequestDialog` を開き、履歴取得を開始する。 |
| System | F2 | 中断 | 履歴実行中 | 履歴取得キャンセルを要求する。 |
| System | F3 | 起動 | 外部アプリ行選択あり | 固定定義の外部アプリを `IExternalProcessLauncher` 経由で起動する。 |
| Maintenance | F1 | 詳細 | 異常行選択あり | 読み取り専用の保守詳細ダイアログを開く。 |

`CMainDialog::PreTranslateMessage()` は `VK_F1` から `VK_F8` を `FunctionSlotFromVirtualKey()` で F1-F8 のスロットへ変換し、有効な画面下部ボタンと同じ `OnFunctionCommand()` へ流します。無効なファンクション操作に対応するキー入力は何もしません。

## グリッド表示

画面表示の表データは Core の `GridModel` で表現されます。

- `GridModel` は列、行、セル、行バインディングを保持する。
- `GridCell` は `CellKind` と、combo/radio 用の候補 `options` を持つ。
- `CellKind` には read-only text、text、spin、combo、radio、checkbox などの種別がある。
- `CCustomGridCtrl::ApplyModel()` が `GridModel` を `CListCtrl` の列と行へ反映する。
- `IsEditableCellKind()` と `ValidateGridEditValue()` が Core 側の編集可否と値検証を担う。

`CCustomGridCtrl` は `SetEditingEnabled(true)` の場合だけセル編集UIを出します。`CMainDialog` は Schedule 画面だけ編集を有効化し、その他の業務画面では編集を無効にします。Schedule 画面では品目名、出庫開始予定、出庫終了予定、出庫順序だけを業務Writeへ接続しています。

編集有効時の動作は次の通りです。

- ダブルクリック、または選択行で Enter を押すと編集を開始する。
- `ReadOnlyText` は編集不可。
- `Text` と `Spin` はセル上に `CEdit` を重ねる。`Spin` は確定時に整数文字列だけ許可する。
- `ComboBox` は `CComboBox` を重ね、`GridCell::options` 内の値だけ許可する。
- `RadioButton` はV1では `ComboBox` と同じ候補選択UIとして扱う。
- `CheckBox` はクリックまたは編集開始で `true` / `false` を切り替える。
- Enter またはフォーカス喪失で確定し、Esc でキャンセルする。
- 確定時は内部 `GridModel` と `CListCtrl` 表示文字列を更新し、親へ `WM_APP + 40` を通知する。

現行V1のグリッド編集UI基盤は複数セル種別を扱えます。業務データへのWrite接続は Schedule 画面の主要編集列に限定され、`GridEditCommit` から `BuildScheduleCellEditWrites()` が1件のWriteを作り、`UpdateCoordinator::RequestWrite()` へ積みます。対象は `2100` 品目名、`2102` 出庫開始予定、`3000` 出庫終了予定、`2103` 出庫順序です。Text列は空文字を拒否し、順序列は `Spin` かつ 1-9999 の正整数だけを許可します。列違い、無効binding、セル種別不一致ではWriteしません。F2順序変更ダイアログは同じ `2103` Write経路として維持しています。Schedule mutation pending中はインセル編集の新規受付を拒否し、表示更新後に編集を再開します。

## 画面別の現行動作

### ステーション画面

`PopulateStation()` は `BuildStationLayoutModel(snapshot, selectedContainerNo_)` を `CStationLayoutCtrl` へ反映し、右側に選択コンテナの詳細テキストを表示します。Station 画面では `contentList_` と `containerListLayout_` は非表示です。

配置図V1は正式な配置データIDを使わず、コンテナ番号 1-100 を番号昇順で5列x20行へ固定配置します。`column=(containerNo-1)/20`、`row=(containerNo-1)%20` で、列0から順に左半円、直線、下半円、直線、右半円のガイド種別を表示します。

`CStationLayoutCtrl` は列ガイド、コンテナセル、コンテナ番号、状態短縮文字を `CPaintDC` で描画します。セル色はV1固定で、通常は白、選択は薄青、コンテナなしは薄灰、異常検知は薄赤、満載は薄黄、追加可能は薄緑です。セルクリックで `selectedContainerNo_` と `UpdateCoordinator::SetSelectedContainer()` を更新し、選択セルを再描画します。コンテナなしセルも選択できますが、F1詳細は無効になります。

右側詳細には次を表示します。

- 選択コンテナ番号。
- 名称。
- 状態。
- 最大5品目の品目名、入庫日、出庫開始、順序、作業時間。

F1 詳細は、選択コンテナが `コンテナなし` でない場合だけ有効です。`BuildContainerSummary(gateway, selectedContainerNo_, 5)` の結果から `BuildContainerDetailModel()` を作り、`CReadOnlyDetailDialog` に「項目 / 値」形式で表示します。詳細行にはコンテナ番号、名称、状態、バックエンドが返した品目数、表示品目数、最大5件の品目名/入庫日/出庫開始/出庫順序/作業時間が含まれます。

正式な物理座標、バックエンドから取得する配置タイプ、半円/直線の実設備再現、スクロール/ズーム、色以外のアラーム表現は未実装です。

### コンテナ一覧画面

`BuildContainerListLayoutModel(snapshot, selectedContainerNo_)` により、100コンテナを番号昇順かつ行優先で3列に配置します。`column=(containerNo-1)%3`、`row=(containerNo-1)/3` で、100件の場合は34行になります。

`CContainerListCtrl` は各コンテナをカードとして描画し、カード内に番号、名称、状態を表示します。カード色は Station 配置図と同じ方針で、通常は白、選択は薄青、コンテナなしは薄灰、異常検知は薄赤、満載は薄黄、追加可能は薄緑です。表示領域に収まらない行は縦スクロールで確認します。

カードクリックで `selectedContainerNo_` と `UpdateCoordinator::SetSelectedContainer()` を更新します。ContainerList 画面には右側詳細欄を表示しません。F1詳細は Station と同じ `BuildContainerDetailModel()` と `CReadOnlyDetailDialog` を使います。カード内編集、複数選択、検索/フィルタ、列幅変更は未実装です。

### スケジュール画面

`BuildScheduleGrid()` により、全コンテナを走査して品目行を作ります。グリッド列は、コンテナ、品目名、出庫開始予定、出庫終了予定、順序です。品目名、出庫開始予定、出庫終了予定は `CellKind::Text`、出庫順序列は `CellKind::Spin` としてモデル化されます。

表示行は `2103` の raw 値を正の整数として解釈し、出庫順序の昇順に並べます。同じ順序の場合は containerNo、itemNo の昇順で安定表示します。非数値または0以下の順序は末尾へ送ります。

F2 順序変更では、選択行の `GridRowBinding` から containerNo と itemNo を取り出し、`2103` へ raw style で Write 要求を積みます。同じ出庫順序を持つ別行が既に表示されている場合、V1では警告して受付を拒否します。Write 完了数が変わると、次回 UI 更新でグリッド再構築が強制されます。

Schedule 画面では `CCustomGridCtrl` の編集を有効化し、確定通知 `WM_APP + 40` を受けた `CMainDialog::HandleScheduleGridEdit()` が、選択行binding、列、セル種別、編集後文字列から `2100` / `2102` / `3000` / `2103` の raw Writeを作成します。UIスレッドはCOMを直接呼ばず、既存のWriteスレッドだけが通信します。

F5 繰上げでは、選択行と直前表示行の `2103` raw 値を入れ替える2件の Write 要求を積みます。先頭行、無効 binding、非数値順序ではF5は無効です。

F6 再採番では、現在のSchedule表示順を正として `10,20,30...` を割り当てます。既に同じ値の行にはWriteしません。F7 Undoでは、成功済みのSchedule操作を最大20件のLIFO履歴から取り出し、逆Writeで復元します。順序変更、繰上げ、再採番は変更前の `2103` 値、追加は `2105` 削除、削除は `2104` 追加と `2102` / `3000` 復元、インセル編集は同じ列の旧値を復元します。redoはありません。

Schedule mutation中はF2-F6とインセル編集由来Writeを新規受付しません。F1詳細は継続して利用できます。F7はUndo履歴があり、かつpending中でない場合だけ有効です。Write batchが成功するとUndo履歴へ積まれ、失敗すると履歴へ積まず、ステータス表示に失敗理由と再実行/状態確認を促す文言を出します。Undo失敗時は取り出したUndo項目を履歴へ戻します。F3追加で追加先の containerNo/itemNo が既存行の場合、および追加/順序変更/順序インセル編集で同じ出庫順序になる場合は、V1では警告して受付を拒否します。

F1 詳細では、選択行の `GridRowBinding{containerNo,itemNo}` を使って `BuildScheduleDetailModel()` を呼び、`2100` 品目名、`2101` 入庫日、`2102` 出庫開始、`3000` 出庫終了、`2103` 出庫順序、`2106` 作業時間を読み直して `CReadOnlyDetailDialog` に表示します。V1では詳細ダイアログからの編集、Write、品目別の深掘り遷移はありません。

### システム画面

システム画面は固定定義の外部アプリ、履歴取得の状態、履歴レコードを表示します。

列は「種別」「名称」「状態」「詳細」です。先頭行は外部アプリ `container-controller` で、名称は「コンテナコントローラ」、暫定起動パスは `ContainerController.exe` です。この行を選択すると F3「起動」が有効になります。

F3 は `Win32ExternalProcessLauncher` から `CreateProcessW` を呼びます。`allowMultiple=false` のため、同じアプリの起動済みプロセスが生存している間は再起動せず、状態を「起動済み」と表示します。起動失敗時は状態を「起動失敗」とし、Win32エラーメッセージを詳細列へ表示します。正式な起動対象パス、引数、権限、二重起動ポリシーは未確定です。

外部アプリ行の後に履歴ステータス行を表示します。その後に `UpdateSnapshot::historyRecords` の内容を、日オフセット/番号を含む名称、値、状態として表示します。履歴レコードは最大500件です。

### メンテナンス画面

メンテナンス画面は `catalog.CriticalKeys()` の順に `1000-1019` の重要情報を表示します。V1の列は ID、項目、値、状態、操作可です。

`BuildMaintenanceStatusModel()` は `UpdateSnapshot::criticalValues` と重要キーを同じ順序で対応付けます。値が不足、空文字、`stale=true`、エラー、または表示文字列に `異常` を含む行は異常扱いになり、操作可が true になります。正常行の操作可は false です。

異常行には `BuildMaintenanceSupportHint()` が読み取り専用の判断支援を付与します。原因分類は `ReadError`、`Stale`、`MissingValue`、`AbnormalText`、`Unknown`、正常時 `None` です。確認優先度、推奨確認、管理者メモは現行値とRead状態から分かる範囲に限定し、正式な復旧操作IDが未定義であることを明示します。

F1 詳細は異常行を選択している場合だけ有効です。`BuildMaintenanceDetailModel()` が作る読み取り専用モデルを `CReadOnlyDetailDialog` に表示し、データID、名称、値、状態、エラー、stale、操作可、原因分類、確認優先度、推奨確認、管理者メモを確認できます。現行V1では詳細ダイアログから手動操作や設定Writeは行いません。

正式な保守対象コンテナの特定、管理者権限制御、復旧/操作Write、正式な異常判定ルール、警報色/点滅/音は未実装です。
