# 概要.md 未実装管理表

この表は `概要.md` の要件と `Git HEAD` のソースコードを比較し、未実装または部分実装の内容を管理するためのものです。

- 比較対象: `概要.md` と `Git HEAD` (`260ed74`) に対する現在の作業ツリー
- 判定日: 2026-05-21
- 判定対象: 作業ツリー上の未コミット変更を含む
- 状態: `未実装`, `部分実装`, `外部依存`

| ID | 状態 | 概要.mdの要件 | HEADで確認できる現状 | 未実装・不足内容 | 根拠 | 優先度 | 次アクション |
|---|---|---|---|---|---|---|---|
| U-001 | 外部依存 | 正式なCOMアプリケーションを仲介し、IP指定後にデータ参照/設定APIを呼び出す。 | COM接続用の `ComBackendBridge`、COMモック、インプロセスモック、起動オプションはある。 | 別部署提供の正式COM仕様、実COM登録、実バックエンド接続、実API戻り値との互換確認が未完了。 | `src/Core/ComBackendBridge.cpp`, `src/BackendBridgeMock/BackendBridgeMock.cpp`, `src/Core/BridgeFactory.cpp` | P1 | 正式COM仕様を入手し、Read/Write/Connectの引数順、戻り値、スレッド要件、エラーコード互換を検証する。 |
| U-002 | 部分実装 | 画面上部の共通部に異常発生など各種ステータス、日時、時刻、ユーザー名などを表示する。 | `BuildStatusSummary` で日時、Windowsユーザー名、画面名、`dataId=1000` の暫定業務状態、重要情報の正常/異常件数、Write/予定Write/履歴状態を2行表示する。 | 正式な業務状態ID、重要情報の警報種別/単位/表示優先度、色/点滅/音などの異常表現は未実装。 | `src/Core/StatusSummary.cpp`, `src/App/MainDialog.cpp` の `RefreshStatus`, `tests/CoreTests/main.cpp` の status summary tests | P2 | 正式データIDと警報表示仕様を受領後、ステータスビルダーのマッピングと表示表現を更新する。 |
| U-003 | 部分実装 | F1-F8のショートカットは、ボタン押下とキーボードのファンクションキー押下で同じ効果にする。 | `PreTranslateMessage` で `VK_F1` から `VK_F8` を `FunctionSlotFromVirtualKey()` 経由で有効な画面下部ボタンと同じ `OnFunctionCommand` へ流す。 | GUI自動操作テストは未導入。無効ボタン相当のキー入力は何もしない仕様で実装。 | `src/App/MainDialog.cpp` の `PreTranslateMessage`, `OnFunctionCommand`; `src/Core/FunctionBarModel.cpp` の `FunctionSlotFromVirtualKey`; `tests/CoreTests/main.cpp` | P3 | 必要になった時点で手動UIスモークまたはGUI自動操作なしの追加自己診断対象を検討する。 |
| U-004 | 部分実装 | 表内に編集不可、テキスト、スピン、コンボボックス、ラジオボタン、チェックボックスが混在するカスタムグリッドを用意する。 | `GridModel` は `CellKind` を保持し、`CCustomGridCtrl` は `CListCtrl` に列と文字列を表示する。 | 各 `CellKind` に応じたセル内エディタ、入力確定、値検証、Write連携が未実装。 | `src/Core/GridModel.h`, `src/App/CustomGridCtrl.cpp`, `src/App/OrderEditDialog.cpp` | P1 | `CCustomGridCtrl` の編集開始/終了イベントと、セル種別ごとの編集コントロール表示方針を設計する。 |
| U-005 | 部分実装 | コンテナステーション画面の左2/3に、半円状/直線状など現実配置を模したコンテナコントロールを表示する。 | Station 画面は `BuildStationLayoutModel` の固定5列x20行モデルを `CStationLayoutCtrl` で描画し、列ごとに左半円/直線/下半円/直線/右半円のガイド種別、コンテナセル、状態色、クリック選択を表示する。右側に選択コンテナ詳細テキストを出す。 | 正式な配置データID、物理座標、設備形状の完全再現、スクロール/ズーム、バックエンド由来の配置タイプは未実装。 | `src/Core/ScreenModels.cpp` の `BuildStationLayoutModel`; `src/App/StationLayoutCtrl.cpp`; `src/App/MainDialog.cpp` の `PopulateStation`, `OnStationLayoutClicked`; `tests/CoreTests/main.cpp` | P2 | 正式配置仕様を受領後、固定配置モデルをカタログ/COM由来の配置定義へ差し替える。 |
| U-006 | 部分実装 | コンテナなしでないコンテナや有効なスケジュール行から詳細表示画面をポップアップする。 | コンテナ詳細とスケジュール詳細はメッセージボックスで通知する。ステーション右側には簡易詳細をテキスト表示する。 | 詳細画面本体、品目ごとの詳細遷移、詳細データの表示/更新、コンテナなし判定の画面遷移制御が未実装。 | `src/App/MainDialog.cpp` の `OnFunctionCommand`, `ShowScheduleDetails`, `PopulateStation`; `src/Core/FunctionBarModel.cpp` | P2 | 詳細画面で扱うデータ項目を確定し、読み取り専用の詳細ダイアログを先に実装する。 |
| U-007 | 部分実装 | コンテナ一覧画面はコンテナ簡易情報を3列でコンテナ番号昇順に配置し、収まらない部分はスクロールする。 | コンテナ番号昇順の単一レポートリストを表示する。`CListCtrl` のスクロールは利用できる。 | 3列カード/表配置、列間の選択制御、3列配置でのスクロール見え方が未実装。 | `src/Core/ScreenModels.cpp` の `BuildContainerListGrid`; `src/App/MainDialog.cpp` の `PopulateCurrentScreen` | P2 | 3列表示用のモデルまたは専用描画を追加し、選択コンテナ番号の算出を3列配置に合わせる。 |
| U-008 | 部分実装 | コンテナスケジュール画面は全出庫予定を出庫順序で並べる。 | `BuildScheduleGrid` は `2103` raw 値を正の整数として昇順ソートし、同順序は containerNo/itemNo 昇順、非数値/0以下は末尾に送る。 | 正式仕様上の予定有無判定、同順位の業務警告、無効順序値の扱いが未確定。 | `src/Core/ScreenModels.cpp` の `BuildScheduleGrid`; `tests/CoreTests/main.cpp` の schedule sort tests | P2 | 正式COM仕様で予定有無と同順位/無効順序の業務ルールを確定する。 |
| U-009 | 部分実装 | スケジュールの有効行で、順序繰り上げ、出庫予定の追加・削除をファンクションキーから実行する。 | 順序変更は `2103`、繰上げは選択行と直前表示行の `2103` 2件Write、追加は `2104`、削除は `2105` のWriteキュー経由で実行できる。Mock/COM MockでRead反映を自己診断する。 | 複数行の連続再採番、Undo、同順序警告、追加/削除失敗時の業務復旧表示が未実装。 | `src/Core/FunctionBarModel.cpp` の `BuildScheduleFunctionActions`; `src/Core/ScreenModels.cpp` の `BuildScheduleMoveUpWrites`; `src/App/MainDialog.cpp` の schedule command handlers; `tests/CoreTests/main.cpp` | P2 | 複数行再採番と失敗時復旧UIを次フェーズで設計する。 |
| U-010 | 未実装 | システム画面からコンテナコントローラなど別アプリを起動する。 | システム画面には「COMモック」「出庫履歴取得」の行が表示される。外部プロセス起動はない。 | 起動対象、権限、起動失敗表示、二重起動制御が未実装。 | `src/App/MainDialog.cpp` の `PopulateCurrentScreen`, `OnFunctionCommand` | P2 | 起動対象のパス/引数/権限を決め、システム画面の操作として追加する。 |
| U-011 | 部分実装 | システム画面で期間を指定し、出庫履歴一覧を表示し、進捗を共通部に表示する。 | F1で過去日数入力ダイアログを開き、`HistoryRequest{days}` で履歴を読み、最新500件をシステム画面へ表示し、共通部に進捗/状態を表示する。 | 開始日/終了日のカレンダー指定、全件保持/エクスポート、正式履歴API粒度への対応が未実装。 | `src/App/HistoryRequestDialog.cpp`; `src/App/MainDialog.cpp` の `PopulateCurrentScreen`, `RefreshStatus`; `src/Core/UpdateScheduler.cpp` の `HistoryLoop` | P2 | 正式履歴API受領後にキー生成と表示項目を更新し、全件出力が必要か判断する。 |
| U-012 | 部分実装 | 出庫履歴取得は長時間でも重要更新、入力Write、通常更新を妨げず、必要に応じて中断/エラー管理できる。 | 履歴処理は低優先度スレッドでReadし、F2中断、Read/エラー/中断metrics、連続エラー停止、500件表示上限、Write優先性能テストを持つ。 | 実COM/実ネットワークでの長時間・最大負荷検証、正式エラー分類、業務向け中断後復旧フローが未実装。 | `src/Core/UpdateScheduler.h`, `src/Core/UpdateScheduler.cpp`, `tests/CoreTests/main.cpp`, `tests/PerformanceTest/main.cpp` | P2 | 実COMまたは遅延注入モックで最大負荷を測り、中断後の再開/再取得ルールを決める。 |
| U-013 | 部分実装 | コンテナ保守画面で、異常検知時にコンテナの詳細ステータスを閲覧し、管理者が手動操作の判断に使えるようにする。 | 保守画面は20件の重要情報を「項目/値/操作可」で一覧表示する。 | 異常コンテナ選択、詳細ステータス項目、手動操作支援情報、管理者向け表示制御が未実装。 | `src/Core/ScreenModels.cpp` の `BuildMaintenanceGrid`; `src/App/MainDialog.cpp` の `Maintenance` 分岐 | P2 | 保守対象データIDと異常状態の表示仕様を確定し、異常コンテナ単位の詳細モデルを追加する。 |
| U-014 | 外部依存 | 20個程度の重要情報は緊急停止判断につながるため、正式な意味とデータIDで30fps更新する。 | 仮カタログに `1000` から `1019` の重要情報とスタイルを定義し、33ms周期でReadする。 | 正式データID、名称、単位、警報/正常判定、表示優先度、確認すべきエラー扱いが未確定。 | `config/data_catalog.json`, `docs/data-catalog.md`, `src/Core/DataCatalog.cpp`, `src/Core/UpdateScheduler.cpp` の `CriticalLoop` | P1 | 別部署仕様を取り込み、仮IDから正式IDへ置換するカタログ更新ルールを作る。 |
| U-015 | 部分実装 | 最大100コンテナ/1000品目の通常更新や出庫履歴取得が、30fps重要更新と100ms以内Write開始を妨げないようにする。 | モックバックエンドと `PerformanceTest` で短時間の周期/Write遅延を検証できる。 | 実COM経由、実ネットワーク、最大データ量、最大履歴期間、3分程度の履歴取得での実測が未完了。 | `tests/PerformanceTest/main.cpp`, `src/Core/UpdateScheduler.cpp` | P1 | 実COMまたは遅延注入モックで最大負荷シナリオを作り、継続時間と許容基準をテスト化する。 |
| U-016 | 部分実装 | 左端ナビは通常1列、拡張時はメイン画面上に一時的に2列表示し、選択後1列へ戻る。 | 拡張ボタンでナビ幅を広げ、5ボタンを2列に配置する。画面選択後は `navExpanded_ = false` に戻す。 | メイン画面上へ重ねるオーバーレイ表示、一時表示の見た目、将来10-15画面への拡張データ構造が未実装。 | `src/App/MainDialog.cpp` の `LayoutControls`, `OnNavExpand`, `SwitchScreen`; `src/App/MainDialog.h` の固定5ボタン配列 | P3 | 画面数拡張を見据え、ナビ項目モデルとオーバーレイ表示方式を分離する。 |

## 更新ルール

- 実装が進んだら、該当行の `状態`, `HEADで確認できる現状`, `未実装・不足内容`, `根拠`, `次アクション` を更新する。
- 判定対象のコミットが変わったら、冒頭の `Git HEAD` 短縮ハッシュを更新する。
- 作業ツリー上の未コミット変更を根拠にする場合は、先にその扱いを明記する。
