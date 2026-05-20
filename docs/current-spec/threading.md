# スレッド/更新制御仕様

## 全体方針

現行実装では、UI スレッドをブロックしないために `UpdateCoordinator` がバックエンドアクセスを複数スレッドに分離します。UI は直接ループを回さず、33ms タイマーごとに `Snapshot()` と `Metrics()` をコピーして表示へ反映します。

`UpdateCoordinator` が管理する主な処理は次の4種類です。

| 処理 | スレッド | 周期/起動条件 | 目的 |
|---|---|---|---|
| 重要更新 | `criticalThread_` | 約33ms周期 | 重要監視キー20件を高頻度取得する。 |
| 通常更新 | `normalThread_` | 約500ms周期 | ステーション画面用の100コンテナスナップショットを作る。 |
| Write | `writeThread_` | キュー投入時 | ユーザー操作による設定 API を直列実行する。 |
| 履歴取得 | `historyThread_` | 要求時のみ | 指定日数分の履歴を低優先度で大量取得する。 |

## 開始と停止

`Start()` は `running_.exchange(true)` で二重起動を抑止し、critical、normal、write の3スレッドを開始します。履歴スレッドは `Start()` では起動せず、`StartHistoryLoad()` が成功した時だけ起動します。

`Stop()` は `running_` を false にし、履歴キャンセル要求を立て、Write 条件変数を通知してから各スレッドを join します。join 順序は critical、normal、write、history です。

## 重要更新スレッド

`CriticalLoop()` は Windows 環境では `THREAD_PRIORITY_HIGHEST` を設定します。

処理順序は次の通りです。

1. 次回起床時刻 `next` を持つ。
2. `catalog_.CriticalKeys()` の全キーを `gateway_.ReadMany()` で順次読む。
3. `snapshotMutex_` をロックし、`snapshot_.criticalValues` を更新する。
4. `criticalCycles_` を増やす。
5. 処理時間が33msを超えた場合は `criticalDeadlineMisses_` を増やす。
6. `next += 33ms` として `sleep_until(next)` する。

`ReadMany()` 自体は入力順に `Read()` を繰り返す単純な同期処理です。通信が遅い場合は critical ループの処理時間が延び、deadline miss として記録されます。

## 通常更新スレッド

`NormalLoop()` は Windows 環境では `THREAD_PRIORITY_BELOW_NORMAL` を設定します。

処理順序は次の通りです。

1. `selectedContainerNo_` を読み取る。
2. `BuildStationSnapshot(gateway_, selectedContainerNo)` を実行する。
3. `snapshotMutex_` をロックし、`snapshot_.station` を swap で差し替える。
4. `normalCycles_` を増やす。
5. 500ms sleep する。

現行では通常更新が常に100コンテナの一覧情報を作ります。UI の現在画面に関係なく station snapshot を更新します。

## Write スレッド

Write は `RequestWrite()` によりキューへ投入されます。

投入順序は次の通りです。

1. `writeMutex_` をロックする。
2. `DataKey`、値、投入時刻 `steady_clock::now()` を `writeQueue_` に push する。
3. `writeCv_.notify_one()` で Write スレッドを起こす。

`WriteLoop()` の処理順序は次の通りです。

1. `writeCv_` で `running_ == false` または `writeQueue_` 非空まで待つ。
2. 停止済みかつキュー空なら return する。
3. キュー先頭の Write 要求を取り出す。
4. 現在時刻と投入時刻の差を `lastWriteStartDelayMs_` に記録する。
5. `gateway_.Write(key, value)` を実行する。
6. 結果を `lastWriteErrorCode_` に記録する。
7. `writeCompletedCount_` を増やす。

現行の Write キューは FIFO です。`PrioritizedWorkQueue` という優先度付きキュー型も存在しますが、実際の `UpdateCoordinator` の Write 処理では `std::queue<WriteRequest>` が使われています。`PrioritizedWorkQueue` は CoreTests で優先度順序の仕様が検証されています。

## 履歴取得スレッド

`StartHistoryLoad()` は日数が 1 から 365 の範囲でなければ失敗します。失敗時は通信を開始せず、snapshot のステータスを `履歴日数が不正です` にします。

正常な開始順序は次の通りです。

1. `historyRunning_.exchange(true)` で二重起動を拒否する。
2. 以前の `historyThread_` が joinable なら join する。
3. 進捗、キャンセル要求、最終エラーを初期化する。
4. `snapshot_.historyRecords` をクリアする。
5. `snapshot_.historyStatusText` を `履歴取得中` にする。
6. `snapshot_.historyRunning` を true にする。
7. `historyThread_` として `HistoryLoop(request)` を開始する。

`HistoryLoop()` は Windows 環境では `THREAD_PRIORITY_BELOW_NORMAL` を設定します。1日あたり1000件を読み、`request.days * 1000` 件を総読取数として進捗を計算します。

履歴取得の主な仕様は次の通りです。

- `MakeHistoryKey(dayOffset, recordIndex)` は `dataId=4000, subId1=dayOffset, subId2=recordIndex, style=raw` を返す。
- 10件ごとに `AppendHistoryRecords()` で snapshot に反映する。
- バッチ反映後に10ms sleep し、他処理を妨げにくくする。
- snapshot に保持する履歴は最新500件までで、超過分は先頭から削除する。
- 読取成功でも表示文字列が空なら `InternalError` 扱いにする。
- 連続50件エラーで `履歴エラー停止: <エラー>` になる。
- キャンセル要求または `running_ == false` で中断し、`履歴中断` になる。
- 正常完了時は進捗を100にし、`履歴取得完了` にする。

## キャンセル仕様

`CancelHistoryLoad()` は履歴実行中でない場合は何もしません。履歴実行中の場合は次の処理を行います。

1. `historyCancelRequested_ = true` にする。
2. `historyCancelCount_` を増やす。
3. `snapshot_.historyStatusText` を `履歴中断要求` にする。

実際に停止するのは `HistoryLoop()` が次のループで cancel flag を確認した時です。停止完了後、snapshot の最終表示は `履歴中断` になります。

## Snapshot 共有

UI は `UpdateCoordinator::Snapshot()` を呼び、内部 snapshot のコピーを取得します。

- `snapshot_` 本体は `snapshotMutex_` で保護される。
- `historyProgress_` と `historyRunning_` は atomic から読み、コピーへ反映する。
- `Metrics()` は atomic カウンタを直接集める。

この設計により、UI スレッドはワーカースレッドの内部データを直接参照せず、コピー済みの一貫した値を表示します。

## COM とスレッドの関係

COM mode の場合、critical、normal、write、history の各スレッドが `ComBackendBridge` を経由して COM を呼ぶ可能性があります。`ComBackendBridge` は thread_local な COM 状態を使うため、各スレッドで個別に `CoInitializeEx()` と `CoCreateInstance()` が行われます。

最初に UI 側の `ConnectAndStart()` で `Connect(ip)` が成功すると、`ComBackendBridge` の共有接続 IP が保存されます。その後、各ワーカースレッドの初回 `Read`/`Write` で、そのスレッドの COM オブジェクトに対して同じ IP で `Connect` が再実行されます。

## 応答性の実現方法

現行実装が要求を満たそうとしている仕組みは次の組み合わせです。

- 重要監視は専用スレッドで 33ms 周期にする。
- 通常更新は別スレッドで 500ms 周期にし、Windows では below normal 優先度にする。
- ユーザー Write は条件変数で即時に起こす専用スレッドに分ける。
- 履歴取得は要求時だけ別スレッドで動かし、10件ごとに snapshot 反映と短い sleep を入れる。
- UI はワーカースレッドの完了を待たず、snapshot コピーだけを読む。

ただし、現行検証は主にモック環境です。実 COM サーバーや実ネットワークで同じ応答性を満たすかは、外部依存のため未確認です。
