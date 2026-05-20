# 検証仕様・確認結果

## 検証方針

この仕様書セットの根拠は、現行ソースコード、既存ドキュメント、既存テスト、既存スモーク実行に限定します。正式 COM サーバーや実バックエンド接続は外部依存のため、標準検証範囲外です。

## 実行対象

計画上の確認コマンドは次の通りです。

```powershell
msbuild .\MFCApplication7.sln /m /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\CoreTests.exe
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000
.\bin\x64\Debug\MFCApplication7.exe /SelfTest
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /WriteSmoke
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /HistorySmoke
```

## 確認観点

### Build

`msbuild` は、Core、App、BackendBridgeMock、CoreTests、PerformanceTest、GenerateDataCatalogSpec が Debug x64 でビルドできることを確認します。

### CoreTests

`CoreTests` は次の仕様を確認します。

- 既定カタログと JSON カタログの定義件数、critical key 件数、style 許可。
- 不正カタログ、不明 style の拒否。
- BridgeFactory による mock bridge 作成。
- MockBackendBridge の style 変換、無効 style 拒否。
- DataGateway の stale/error 付与。
- FunctionBarModel の画面別有効/無効。
- GridModel の cell kind 保持。
- Schedule grid の row binding。
- Write 後の readback。
- 履歴要求 validation と key 生成。
- UpdateCoordinator の Write メトリクス、ReadOnly エラー、履歴キャンセル、履歴500件上限、不正履歴要求拒否、履歴中 Write 応答性。
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

## 今回の実行結果

実行日: 2026-05-21

| コマンド | 終了コード | 結果 | 代表出力/理由 |
|---|---:|---|---|
| `msbuild .\MFCApplication7.sln /m /p:Configuration=Debug /p:Platform=x64` | 1 | 実行不能 | `msbuild` が現在の PowerShell PATH に存在しないため、コマンドとして認識されなかった。 |
| `.\bin\x64\Debug\CoreTests.exe` | 1 | 失敗 | `FAIL: write should start within 100ms` |
| `.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000` | 1 | 失敗 | `criticalCycles=87`, `criticalDeadlineMisses=0`, `normalCycles=6`, `lastWriteStartDelayMs=83`, `writeCompletedCount=1`, `lastWriteErrorCode=0`, `historyReadCount=0`, `critical refresh cadence too slow` |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /WriteSmoke` | 0 | 成功 | 標準出力なし。終了コード 0。 |
| `.\bin\x64\Debug\MFCApplication7.exe /SelfTest /HistorySmoke` | 0 | 成功 | 標準出力なし。終了コード 0。 |

### 結果の扱い

`msbuild` は Developer PowerShell または Visual Studio Build Tools の PATH 設定がないため実行できなかった。既存の `bin\x64\Debug` 配下のバイナリは実行可能だったため、テスト/スモークは既存ビルド成果物に対して実行した。

`CoreTests` と `PerformanceTest` は失敗しているため、この仕様書作成時点では「既存テストが全て成功している」とは記録しない。失敗は仕様書変更によるものではなく、実行時に観測された既存バイナリ/環境での結果として扱う。

## 標準検証範囲外

次は今回の標準検証範囲外です。

- 別部署提供予定の正式 COM アプリケーションとの接続。
- 実バックエンド/実ネットワーク通信での応答性確認。
- COM モックの HKCU 登録/解除を伴う環境依存確認。
- MFC GUI の手動操作確認。
- キーボード F1-F8 入力確認。

これらは現行コードから仕様を記述できる範囲を超えるため、外部環境が揃った段階で別途検証する必要があります。

