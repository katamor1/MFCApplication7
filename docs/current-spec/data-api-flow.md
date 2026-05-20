# データ/API/フロー仕様

## データアクセスの全体像

現行実装のデータアクセスは、次の層で分離されています。

1. UI または画面モデルが必要な情報を要求する。
2. `DataGateway` が `IBackendBridge` に `Read` または `Write` を委譲する。
3. `IBackendBridge` の実体は、インプロセスモックまたは COM ブリッジである。
4. 読取結果は `DataValue` に変換され、表示文字列、エラーコード、更新時刻、stale 状態として返る。
5. 画面モデルは `GridModel`、`StationSnapshot`、`UpdateSnapshot` などに変換される。
6. UI はスナップショットまたはグリッドモデルを MFC コントロールへ反映する。

## 公開データ型

### `DataStyle`

`DataStyle` はバックエンドから取得する文字列表現の形式を指定します。

| 値 | 意味 |
|---:|---|
| `0 Raw` | 生データ文字列をそのまま扱う。 |
| `1 ThousandsSeparated` | 数値を3桁区切り文字列として扱う。 |
| `2 SecondsToHhMmSs` | 秒数を `hh:mm:ss` 形式に変換する。 |
| `3 MillimetersToInches` | ミリメートル値をインチ表記へ変換する。 |

許可される style はデータ ID ごとに `DataCatalog` で定義されます。未許可 style は `BridgeError::InvalidStyle` です。

### `BridgeError`

`BridgeError` はバックエンド API の戻り値として扱われます。

| 値 | 意味 |
|---:|---|
| `0 Ok` | 正常終了。 |
| `1 NotConnected` | COM 接続未実施または切断。 |
| `2 InvalidDataId` | データ ID が未登録。 |
| `3 InvalidSubDataId` | サブ ID が範囲外。 |
| `4 InvalidStyle` | style がデータ定義と不一致。 |
| `5 ReadOnly` | 参照専用項目への設定要求。 |
| `6 Timeout` | 通信タイムアウト。 |
| `7 InvalidIpAddress` | IP アドレス検証失敗。 |
| `100 InternalError` | 予期しない内部エラー。 |

画面表示では `ToDisplayText()` により日本語文字列へ変換されます。

### `DataKey`

`DataKey` は1件のデータ参照/設定対象を表します。

| フィールド | 意味 |
|---|---|
| `dataId` | データ ID。 |
| `subId1` | サブデータ ID 1。 |
| `subId2` | サブデータ ID 2。 |
| `style` | 取得/設定時の表示形式。 |

`DataKey` は等価比較と辞書式比較を持ち、map/set で利用できます。

### `DataValue`

`DataValue` は読取結果です。

| フィールド | 意味 |
|---|---|
| `displayText` | 表示用文字列。エラー時は空になる場合がある。 |
| `errorCode` | 読取結果のエラーコード。 |
| `updatedAt` | `DataGateway::Read()` 実行時の `steady_clock` 時刻。 |
| `stale` | 読取結果が正常でない場合に true。 |

## `IBackendBridge` API

Core 内のバックエンド抽象は `IBackendBridge` です。

| メソッド | 役割 |
|---|---|
| `Connect(const std::wstring& ipAddress)` | 指定 IP へ接続する。 |
| `Read(const DataKey& key, std::wstring& value)` | データキーに対する文字列値を1件取得する。 |
| `Write(const DataKey& key, const std::wstring& value)` | データキーへ文字列値を設定する。 |

UI とスケジューラは、この抽象だけを見ます。具体的な通信方式は `BridgeFactory` が選択します。

## COM API 仕様

COM 互換インターフェイスは `IBackendBridgeCom` として定義されています。

| メソッド | COM 署名 |
|---|---|
| `Connect` | `HRESULT Connect(BSTR ipAddress, LONG* errorCode)` |
| `Read` | `HRESULT Read(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR* value, LONG* errorCode)` |
| `Write` | `HRESULT Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode)` |

`ComBackendBridge` は late binding の `IDispatch` として `Connect`、`Read`、`Write` の DISPID を名前解決し、`Invoke()` で呼び出します。戻り値の `VARIANT` は `VT_I4` に変換され、`BridgeError` として扱われます。

### COM 呼び出し時の引数順序

`IDispatch::Invoke` の `DISPPARAMS` は引数配列が逆順に並ぶため、現行 `ComBackendBridge` は次の順序で `args` を構築します。

| API | `args[0]` | `args[1]` | `args[2]` | `args[3]` | `args[4]` |
|---|---|---|---|---|---|
| `Connect` | `ipAddress` | - | - | - | - |
| `Read` | `BSTR* value` | `style` | `subId2` | `subId1` | `dataId` |
| `Write` | `value` | `style` | `subId2` | `subId1` | `dataId` |

`BackendBridgeMock` 側もこの reversed order を前提に `DISPPARAMS` から論理引数を取り出します。

## COM 接続状態

`ComBackendBridge` はスレッドごとに `ThreadComState` を持ちます。

- `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` を各スレッドで必要時に実行する。
- `IDispatch` オブジェクトもスレッドローカルに保持する。
- UI または別スレッドで `Connect()` が成功すると、共有状態として接続済み IP を保存する。
- 各スレッドの初回 `Read`/`Write` 時に、保存済み IP でそのスレッドの COM オブジェクトへ再接続する。
- スレッド終了時は thread_local のデストラクタで `bridge.Release()` と `CoUninitialize()` を行う。

この設計により、`UpdateCoordinator` の複数スレッドから COM を呼ぶ場合も、各スレッドの COM 初期化と COM オブジェクトを独立させます。

## データカタログ

`DataCatalog` はデータ ID 定義と重要監視キーを保持します。既定では `config/data_catalog.json` を読み込み、失敗時は組み込み既定カタログへフォールバックします。

現行カタログの大枠は次の通りです。

| 範囲 | 用途 |
|---|---|
| `1000-1019` | 重要情報。20件。critical 更新対象。 |
| `2000-2003` | コンテナ番号、名称、状態、品目数。 |
| `2100-2106` | 品目名、入庫日付、出庫開始予定、出庫順序、出庫予定追加、出庫予定削除、作業時間。 |
| `3000` | 出庫終了予定日時。 |
| `4000` | 出庫履歴。`subId1` が日オフセット、`subId2` がレコード番号。 |

各定義は writable、subId1 範囲、subId2 範囲、allowedStyles を持ちます。`MockBackendBridge` は `ValidateKey()` と `IsWritable()` を使い、無効 ID、範囲外 sub ID、未許可 style、参照専用 Write を拒否します。

## 画面モデルへのデータ流れ

### ステーション/コンテナ一覧

`BuildStationSnapshot()` は 1 から 100 までのコンテナを読みます。各コンテナは `BuildContainerSummary()` で作られます。

- コンテナ名: `2001, containerNo, 0, raw`
- コンテナ状態: `2002, containerNo, 0, raw`
- 品目数: `2003, containerNo, 0, raw`
- 選択コンテナの品目詳細: `2100-2106, containerNo, itemNo`

全コンテナ一覧では品目詳細は読まず、選択コンテナだけ最大5品目を読む設計です。

### スケジュール

`BuildScheduleGrid()` は 1 から 100 までのコンテナを走査し、各コンテナの品目数に応じて最大1000品目の行を作ります。

- コンテナ番号。
- 品目名 `2100`。
- 出庫終了予定 `3000`。
- 出庫順序 `2103`。

行には `GridRowBinding{containerNo, itemNo}` が付与されます。順序変更ではこの binding を使って `2103` へ Write 要求を積みます。

表示順は `2103` raw 値を正の整数として解釈した昇順です。同じ順序は containerNo、itemNo の昇順で並べます。非数値または0以下の順序は末尾に置きます。F5 繰上げでは、選択行と直前表示行の `2103` raw 値を2件の Write として入れ替えます。

### システム/履歴

履歴取得は `UpdateCoordinator::StartHistoryLoad()` で開始し、`HistoryLoop()` が `MakeHistoryKey(dayOffset, recordIndex)` を使って `dataId=4000` を読みます。

履歴結果は `UpdateSnapshot::historyRecords` に保持されます。保持件数は最新500件に制限されます。

### メンテナンス

`BuildMaintenanceGrid()` は `1000-1019` の重要情報を固定範囲で読み、項目名、値、操作可の3列にします。現行の操作可は文字列 `false` のチェックボックス種別として表現されますが、セル内編集は未実装です。
