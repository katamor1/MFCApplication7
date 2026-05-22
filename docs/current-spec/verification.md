# 検証仕様・確認結果

## 検証方針

この仕様書セットの根拠は、現行ソースコード、既存ドキュメント、既存テスト、既存スモーク実行に限定します。正式 COM サーバーや実バックエンド接続は外部依存のため、標準検証範囲外です。

## 実行対象

計画上の確認コマンドは次の通りです。

```powershell
msbuild .\MFCApplication7.sln /m /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\CoreTests.exe
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 60000
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad /MockHistoryReadDelayMs:1 /MockWriteDelayMs:1
.\bin\x64\Debug\MFCApplication7.exe /SelfTest
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /WriteSmoke
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /HistorySmoke
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /GridEditSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleGridEditSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleUndoSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /NavigationSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /DetailSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaintenanceDetailSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaxLoadSmoke /Bridge:Mock /MockProfile:MaxLoad
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ExternalLaunchSmoke /Bridge:Mock
```

## 確認観点

### Build

`msbuild` は、Core、App、BackendBridgeMock、CoreTests、PerformanceTest、GenerateDataCatalogSpec が Debug x64 でビルドできることを確認します。

### CoreTests

`CoreTests` は次の仕様を確認します。

- 既定カタログと JSON カタログの定義件数、critical key 件数、style 許可。
- 不正カタログ、不明 style の拒否。
- BridgeFactory による mock bridge 作成。
- BridgeFactory による mock 最大負荷プロファイルと遅延注入オプション解析。
- MockBackendBridge の style 変換、無効 style 拒否。
- MockBackendBridge の `MaxLoad` プロファイルによる100コンテナ/1000品目相当の生成。
- DataGateway の stale/error 付与。
- FunctionBarModel の画面別有効/無効。
- NavigationModel の既定ナビ項目、画面名、1列/2列配置。
- GridModel の cell kind と combo/radio options 保持、グリッド編集値検証。
- ContainerSummary の総品目数と表示品目数の分離、コンテナ詳細/スケジュール詳細モデル。
- Schedule grid の row binding、品目名/出庫開始予定/出庫終了予定/出庫順序インセル編集から既存Write IDへ変換するヘルパ。
- Schedule mutation helper の10刻み再採番、同順序検出、Undo復元Write生成、Undo履歴最大20件/LIFO。
- Write 後の readback。
- 履歴要求 validation と key 生成。
- UpdateCoordinator の Write メトリクス、Write開始遅延最大値、100ms超過回数、ReadOnly エラー、履歴キャンセル、履歴500件上限、不正履歴要求拒否、履歴中 Write 応答性。
- PrioritizedWorkQueue の優先度順序。
- StationSnapshot の100コンテナ構築。

### PerformanceTest

`PerformanceTest --duration-ms 3000` は短時間の実行で次を確認します。

- critical 更新が実行されること。
- mock mode では critical cycle 数が期待下限を満たすこと。
- mock mode では critical deadline miss が 0 であること。
- Write 開始遅延が 0ms 以上 100ms 以下であること。
- Write が完了し、結果が `Ok` であること。
- 履歴取得が実行され、履歴エラーがないこと。

`PerformanceTest --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad /MockHistoryReadDelayMs:1 /MockWriteDelayMs:1` は、上記に加えて次を確認します。

- mock最大負荷プロファイルでスケジュールグリッドが1000行相当を生成すること。
- スケジュールグリッド再構築が実行され、最大所要時間が出力されること。
- 連続Writeの `maxWriteStartDelayMs` が100ms以内で、`writeStartDelayExceededCount` が0であること。

### `/SelfTest`

`MFCApplication7.exe /SelfTest` は GUI を開かず、次を確認して終了します。

- gateway 接続が成功すること。
- station snapshot が100コンテナを持ち、選択コンテナの品目が空でないこと。
- schedule grid に行があること。
- maintenance grid が20行であること。

`/SelfTest /WriteSmoke` は、Write 経路を追加で確認します。

- `UpdateCoordinator` が Write を完了すること。
- Write 開始遅延が100ms以内であること。
- Write 結果が `Ok` であること。
- 書き込んだ `2103` が readback できること。

`/SelfTest /HistorySmoke` は、履歴取得とキャンセル経路を追加で確認します。

- 3日分履歴取得が開始し、進捗が出ること。
- 履歴実行中に Write が完了すること。
- キャンセル後に履歴が停止すること。
- 履歴キャンセル数、履歴 read 数、Write 開始遅延、critical cycle が期待を満たすこと。

`/SelfTest /GridEditSmoke` は、カスタムグリッド編集UI基盤を追加で確認します。

- 通常既定では `BeginEditCell()` が開始されないこと。
- `Text`、`Spin`、`ComboBox`、`RadioButton`、`CheckBox` の編集確定が `GridModel` と `LastEditCommit()` に反映されること。
- Esc キャンセルではセル値が変わらないこと。

`/SelfTest /ScheduleGridEditSmoke` は、Schedule 画面相当の主要編集列インセル編集から Write キューまでを追加で確認します。

- `BuildScheduleGrid()` の `2100` 品目名、`2102` 出庫開始予定、`3000` 出庫終了予定、`2103` 出庫順序セルを `CCustomGridCtrl` 上で編集確定できること。
- `GridEditCommit` の binding、列、セル種別、旧値/新値が期待どおりであること。
- `BuildScheduleCellEditWrites()` が対象列ごとに `2100` / `2102` / `3000` / `2103` の raw Writeを1件作ること。
- `UpdateCoordinator` のWriteキュー経由で readback が更新され、Write開始遅延が100ms以内であること。

`/SelfTest /ScheduleUndoSmoke` は、Schedule mutation のUndo相当経路を追加で確認します。

- 再採番Writeで表示順に `10,20,30` がreadbackでき、逆Writeで元の順序へ戻ること。
- 追加 `2104` 後に `2105` 逆Writeで追加行が消えること。
- 削除 `2105` 後に `2104` と書込可能列の逆Writeで行が戻ること。
- 順序変更 `2103` 後に旧値の逆Writeで元へ戻ること。
- すべて `UpdateCoordinator::RequestWrite()` のWriteキュー経由で実行され、Schedule mutation error count が増えないこと。

`/SelfTest /NavigationSmoke` は、左ナビモデルを追加で確認します。

- 既定ナビ項目が5件で現行画面順に並ぶこと。
- 1列配置と2列配置の `column` / `row` が期待どおりであること。
- current screen に一致するセルだけが selected になること。
- ステータス表示用の画面名が解決できること。

`/SelfTest /DetailSmoke` は、詳細ダイアログに渡す読み取り専用モデルを追加で確認します。

- コンテナ詳細モデルが品目数、表示品目数、最大5件の品目詳細を持つこと。
- コンテナなし選択では F1 詳細対象外になること。
- スケジュール詳細モデルが binding から品目名、予定、順序、作業時間を読み直すこと。

`/SelfTest /MaintenanceDetailSmoke` は、保守画面の異常行詳細を追加で確認します。

- 重要情報20行から保守ステータスグリッドを構築できること。
- 異常行だけF1詳細相当の操作可になること。
- 詳細モデルが従来の診断行に加え、原因分類、確認優先度、推奨確認、管理者メモを持つこと。
- 管理者メモで、本画面から復旧Writeを行わない境界を明示すること。

`/SelfTest /MaxLoadSmoke` は、最大負荷モックプロファイルを使って次を確認します。

- `BuildScheduleGrid()` が1000行相当を生成すること。
- 履歴取得、重要更新、通常更新、複数Writeが併走すること。
- Write開始遅延最大値が100ms以内で、100ms超過回数が0であること。

`/SelfTest /ExternalLaunchSmoke` は、Fake起動器を使って次を確認します。

- 固定定義 `container-controller` が存在すること。
- 初回起動成功がSystemグリッドへ「起動済み」として反映されること。
- 二重起動抑止時も「起動済み」として反映されること。
- 失敗注入時にSystemグリッドへ「起動失敗」と失敗メッセージが反映されること。

## 今回の実行結果

実行日: 2026-05-22

| コマンド | 終了コード | 結果 | 代表出力/理由 |
|---|---:|---|---|
| `MSBuild.exe MFCApplication7.sln /p:Configuration=Debug /p:Platform=x64 /m` | 0 | 成功 | `ビルドに成功しました。0 個の警告 0 エラー` |
| `.\bin\x64\Debug\CoreTests.exe` | 0 | 成功 | `PASS: 57 core tests` |
| `.\bin\x64\Debug\BackendBridgeMock.exe /SelfTest` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\BackendBridgeMock.exe /ComSelfTest` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /WriteSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /HistorySmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleMutationSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleOrderSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /StatusSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /StationLayoutSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ContainerListLayoutSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaintenanceDetailSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /DetailSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /GridEditSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleGridEditSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleUndoSmoke /Bridge:Mock` | 0 | 成功 | `Start-Process -Wait -PassThru` で終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /NavigationSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaxLoadSmoke /Bridge:Mock /MockProfile:MaxLoad` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ExternalLaunchSmoke /Bridge:Mock` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ExternalLaunchSmoke /Bridge:Com /ProgId:MFCApplication7.BackendBridgeMock` | 0 | 成功 | HKCU登録後に実行し、最後に登録解除。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleGridEditSmoke /Bridge:Com /ProgId:MFCApplication7.BackendBridgeMock` | 0 | 成功 | HKCU登録後に実行し、最後に登録解除。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleUndoSmoke /Bridge:Com /ProgId:MFCApplication7.BackendBridgeMock` | 0 | 成功 | HKCU登録後に実行し、最後に登録解除。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /NavigationSmoke /Bridge:Com /ProgId:MFCApplication7.BackendBridgeMock` | 0 | 成功 | HKCU登録後に実行し、最後に登録解除。終了コード 0。 |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000` | 0 | 成功 | `criticalCycles=95`, `criticalDeadlineMisses=0`, `criticalMaxCycleMs=1`, `maxWriteStartDelayMs=0`, `historyReadCount=1900` |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 60000` | 0 | 成功 | `criticalCycles=1822`, `criticalDeadlineMisses=0`, `criticalMaxCycleMs=7`, `criticalMaxSnapshotLockMs=0`, `maxWriteStartDelayMs=0`, `historyReadCount=3000`。 |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad` | 0 | 成功 | `criticalCycles=99`, `criticalDeadlineMisses=0`, `writeCompletedCount=12`, `scheduleGridLastRows=1000`, `scheduleGridMaxMs=115` |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad /MockHistoryReadDelayMs:1 /MockWriteDelayMs:1` | 1 | 失敗 | `criticalDeadlineMisses=1`, `scheduleGridLastRows=1000`, `scheduleGridMaxMs=563`, `writeStartDelayExceededCount=0`。 |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 60000 --max-load /Bridge:Mock /MockProfile:MaxLoad /MockHistoryReadDelayMs:1 /MockWriteDelayMs:1` | 1 | 失敗 | 2回再実行して `criticalDeadlineMisses=1` / `4`。2回目は `scheduleGridMaxMs=4819`, `writeStartDelayExceededCount=0`。 |
| `git diff --check` | 0 | 成功 | 空白エラーなし。改行コード変換警告のみ。 |

### U-013 追加確認

実行日: 2026-05-23

| コマンド | 終了コード | 結果 | 代表出力/理由 |
|---|---:|---|---|
| `MSBuild.exe .\MFCApplication7.sln /p:Configuration=Debug /p:Platform=x64 /m` | 0 | 成功 | `ビルドに成功しました。0 個の警告 0 エラー` |
| `.\bin\x64\Debug\CoreTests.exe` | 0 | 成功 | `PASS: 57 core tests` |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaintenanceDetailSmoke /Bridge:Mock` | 0 | 成功 | `Start-Process -Wait -PassThru` で `exit=0`。 |
| `git diff --check` | 0 | 成功 | 空白エラーなし。改行コード変換警告のみ。 |

### 結果の扱い

MSBuild は Visual Studio 2022 の `MSBuild.exe` を直接指定して実行した。上記の標準検証と最大負荷mock検証は Debug x64 の再ビルド後バイナリに対する結果です。

通常 `PerformanceTest --duration-ms 60000` は `criticalDeadlineMisses=0` で成功しました。`criticalMaxCycleMs=7`、`criticalMaxSnapshotLockMs=0` で、Write 開始遅延、Write 結果、履歴エラーも正常です。一方、遅延注入付き max-load 性能ゲートは過去結果として critical deadline miss を記録しており、正式COMや遅延注入条件での長時間安定性は別途調査対象です。

## 標準検証範囲外

次は今回の標準検証範囲外です。

- 別部署提供予定の正式 COM アプリケーションとの接続。
- 実バックエンド/実ネットワーク通信での応答性確認。
- 別部署提供の正式 COM サーバーの HKCU 登録/解除を伴う環境依存確認。COM モックは一部自己診断で確認済み。
- MFC GUI の手動操作確認。
- キーボード F1-F8 入力確認。

これらは現行コードから仕様を記述できる範囲を超えるため、外部環境が揃った段階で別途検証する必要があります。

