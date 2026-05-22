# アーキテクチャ仕様

## ソリューション構成

MFCApplication7.sln は、次のプロジェクトで構成されます。

| プロジェクト | 種別 | 役割 |
|---|---|---|
| `MFCApplication7Core` | Static Library | データ型、データカタログ、バックエンド抽象、モック、画面モデル、更新スケジューラを提供する中核ライブラリ。 |
| `MFCApplication7` | Windows Application | MFC ダイアログ UI。Core のモデルとスケジューラを使って画面を構築する。 |
| `BackendBridgeMock` | Windows Application / COM Server | COM 経由バックエンドのモック。ProgID 登録、`IDispatch` 呼び出し、簡易スモーク確認を提供する。 |
| `CoreTests` | Console Application | Core 層のカタログ、モック、画面モデル、スケジューラ動作を検証する。 |
| `PerformanceTest` | Console Application | 更新周期、Write 応答性、履歴取得並行性、mock最大負荷シナリオを短時間実行で検証する。 |
| `GenerateDataCatalogSpec` | Console Application | `config/data_catalog.json` からデータカタログ仕様書を生成する補助ツール。 |

依存関係は Core を中心にしています。App、BackendBridgeMock、tests、tools は Core に依存しますが、Core は App には依存しません。

## モジュール別の役割

### `src/App`

`src/App` は MFC の画面実装を持つ層です。主な責務は次の通りです。

- `MFCApplication7App.cpp`: アプリケーション起動、共通コントロール初期化、起動引数解析、`/SelfTest` 系スモーク分岐、メインダイアログ起動。
- `MainDialog.cpp/h`: メイン画面、ナビゲーション、ステータス、履歴プログレス、グリッド、F1-F8 ボタン、UI タイマー、画面切替を統括。
- `CustomGridCtrl.cpp/h`: `GridModel` を `CListCtrl` へ反映し、編集有効時だけ `CellKind` に応じたインプレース編集UIを出す表示/編集アダプタ。
- `ExternalProcessLauncher.cpp/h`: System画面の外部アプリ起動境界。実行時は `CreateProcessW`、自己診断はFake起動器を使う。
- `HistoryRequestDialog.cpp/h`: 履歴取得日数の入力ダイアログ。
- `OrderEditDialog.cpp/h`: スケジュール順序変更の入力ダイアログ。

App 層は画面表示とユーザー操作の入口であり、バックエンド通信の詳細には直接依存しません。通信は `DataGateway` と `UpdateCoordinator` 経由で行います。

### `src/Core`

`src/Core` は現行アプリの中核です。主な責務は次の通りです。

- `DataTypes`: `DataKey`、`DataValue`、`DataStyle`、`BridgeError` などの公開データ型。
- `DataCatalog`: データ ID 定義、サブ ID 範囲、許可スタイル、重要監視キーの保持と検証。
- `BackendBridge`: バックエンド接続の抽象インターフェイスと COM 互換インターフェイス定義。
- `MockBackendBridge`: カタログ定義に従って読取・書込・スタイル変換を行うインプロセスモック。検証用に最大負荷プロファイルと遅延注入を持つ。
- `ComBackendBridge`: COM `IDispatch` 経由で外部ブリッジの `Connect`、`Read`、`Write` を呼び出す実装。
- `BridgeFactory`: 起動引数から bridge mode、ProgID、IP、カタログパスを解釈し、ブリッジ実装を生成する。
- `DataGateway`: `IBackendBridge` の薄いラッパー。読取結果に更新時刻、エラー、stale を付与する。
- `ScreenModels`: バックエンドデータからステーション、一覧、スケジュール、保守画面用のモデルを作る。
- `NavigationModel`: 画面ID、左ナビ項目、1列/2列配置、ステータス表示用画面名を定義する。
- `FunctionBarModel`: 画面状態に応じて F1-F8 のラベルと有効/無効を決める。
- `GridModel`: 表示用グリッドの列、行、セル種別、候補値、行バインディングを保持し、グリッド編集値の検証ヘルパを提供する。
- `UpdateScheduler`: 重要更新、通常更新、Write、履歴取得をスレッドで分離し、UI が読むスナップショットとメトリクスを提供する。

### `src/BackendBridgeMock`

`BackendBridgeMock` は COM 経由の検証用モックです。Core の `MockBackendBridge` を内部で使い、COM クラスとして外部から `Connect`、`Read`、`Write` を呼べるようにします。

現行実装では次の機能を持ちます。

- `IBackendBridge` の IDL 定義。
- HKCU への COM クラス/ProgID 登録と登録解除。
- `IDispatch::GetIDsOfNames` と `IDispatch::Invoke` による late binding 対応。
- COM 呼び出し引数を Core の `DataKey` と文字列値へ変換する処理。
- 登録後の簡易 COM 呼び出しスモーク。

### `tests` と `tools`

`CoreTests` は Core の振る舞いを仕様として固定するテスト群です。`PerformanceTest` は 30fps 相当の重要更新、Write 開始遅延、履歴取得並行性に加え、`--max-load` で1000行スケジュール再構築と連続Write併走をメトリクスで確認します。

`GenerateDataCatalogSpec` は `config/data_catalog.json` の内容を Markdown 化する補助ツールです。生成済み仕様は `docs/data-catalog.md` にあります。

## 結合方針

現行コードは、UI と通信方式を直接結合しない構成です。

1. `CMainDialog` は `BridgeFactoryOptions` を受け取る。
2. `BridgeFactory` が `IBackendBridge` 実装を生成する。
3. `DataGateway` が `IBackendBridge` を包む。
4. `UpdateCoordinator` が `DataGateway` と `DataCatalog` を所有し、周期取得と Write を担う。
5. UI は `UpdateCoordinator::Snapshot()` と `Metrics()` を定期的に読み、画面へ反映する。

この結合により、UI はインプロセスモック、COM モック、正式 COM の違いを意識せずに動作します。
