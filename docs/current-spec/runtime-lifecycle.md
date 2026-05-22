# 起動・終了ライフサイクル仕様

## 通常起動順序

通常起動時の現行処理は次の順序です。

1. `CMFCApplication7App::InitInstance()` が呼ばれる。
2. `CWinApp::InitInstance()` を実行する。
3. `InitCommonControlsEx()` で MFC 共通コントロールを初期化する。
4. `m_lpCmdLine` から起動引数文字列を作る。
5. `ParseBridgeFactoryOptions()` で bridge mode、ProgID、IP、カタログパスを解釈する。
6. `/SelfTest` が指定されていなければ `CMainDialog` を構築する。
7. `m_pMainWnd` にメインダイアログを設定する。
8. `dialog.DoModal()` でモーダルダイアログを開始する。
9. ダイアログ終了後、`InitInstance()` は `FALSE` を返し、アプリケーションを終了する。

## 起動引数

現行の起動引数は `BridgeFactory` と self-test 分岐で解釈されます。

| 引数 | 現行動作 |
|---|---|
| `/Bridge:mock` | インプロセス `MockBackendBridge` を使う。既定値。 |
| `/Bridge:com` | `ComBackendBridge` を使い、COM ProgID から外部 COM サーバーへ接続する。 |
| `/ProgId:<value>` | COM mode 時に使う ProgID を上書きする。既定値は `MFCApplication7.BackendBridgeMock`。 |
| `/Ip:<value>` | 初期接続先 IP を上書きする。既定値は `127.0.0.1`。 |
| `/Catalog:<path>` | データカタログ JSON のパスを上書きする。既定値は `config/data_catalog.json`。 |
| `/MockProfile:MaxLoad` | mock mode 時に100コンテナ/1000品目相当の負荷プロファイルを使う。COM mode では無視される。 |
| `/MockCriticalReadDelayMs:<N>` | mock mode の重要情報ReadにN msの遅延を注入する。 |
| `/MockNormalReadDelayMs:<N>` | mock mode の通常/スケジュールReadにN msの遅延を注入する。 |
| `/MockHistoryReadDelayMs:<N>` | mock mode の履歴ReadにN msの遅延を注入する。 |
| `/MockWriteDelayMs:<N>` | mock mode のWriteにN msの遅延を注入する。 |
| `/SelfTest` | GUI を開かず、自己診断を実行して `ExitProcess()` で終了する。 |
| `/WriteSmoke` | `/SelfTest` と組み合わせると Write 経路スモークを実行する。 |
| `/HistorySmoke` | `/SelfTest` と組み合わせると履歴取得スモークを実行する。 |
| `/MaxLoadSmoke` | `/SelfTest` と組み合わせると最大負荷モックプロファイルで履歴、通常更新、重要更新、複数Writeの併走を確認する。 |
| `/GridEditSmoke` | `/SelfTest` と組み合わせると `CCustomGridCtrl` の編集開始、確定、キャンセル、セル種別別UIを確認する。 |
| `/DetailSmoke` | `/SelfTest` と組み合わせるとコンテナ詳細/スケジュール詳細モデルとコンテナなし除外を確認する。 |
| `/ExternalLaunchSmoke` | `/SelfTest` と組み合わせるとFake起動器で外部アプリ起動、二重起動抑止、失敗表示を確認する。 |

`/WriteSmoke` と `/HistorySmoke` は `RunSelfTest()` 内で順に判定されます。現行コードでは Write smoke が先に評価されるため、両方を同時指定した場合は Write smoke 側の戻り値が優先されます。

## メインダイアログ初期化順序

`CMainDialog::OnInitDialog()` の現行順序は次の通りです。

1. `CDialogEx::OnInitDialog()` を呼ぶ。
2. ウィンドウタイトルを `MFCApplication7 縦切り基盤` に設定する。
3. 初期位置とサイズを `50, 50, 1280, 800` に設定する。
4. `CreateControls()` でステータス、プログレス、詳細欄、リスト、ナビ、展開ボタン、F1-F8 ボタンを生成する。
5. `LayoutControls()` で現在のクライアントサイズに合わせて配置する。
6. `ConnectAndStart()` でバックエンド接続と `UpdateCoordinator` 起動を行う。
7. `SetTimer()` で 33ms 周期の UI 更新タイマーを開始する。
8. `RefreshUi(true)` で初回描画を強制する。

## 接続開始順序

`ConnectAndStart()` の現行順序は次の通りです。

1. 既に構築済みの `bridge_` から `DataGateway` を作る。
2. `gateway.Connect(bridgeOptions_.ipAddress)` を呼ぶ。
3. 接続に失敗した場合はステータス欄へ `接続失敗: <エラー表示>` を出し、`UpdateCoordinator` は作らない。
4. 接続に成功した場合は `UpdateCoordinator(catalog_, gateway)` を作る。
5. `coordinator_->Start()` で critical、normal、write の3スレッドを開始する。

## UI 更新順序

UI 更新は MFC UI スレッドの `OnTimer()` から駆動されます。

1. タイマー ID が `kRefreshTimerId` の場合だけ処理する。
2. `timerTicks_` をインクリメントする。
3. `RefreshUi(timerTicks_ % 15 == 0)` を呼ぶ。
4. `RefreshUi()` は `Snapshot()` と `Metrics()` を取得する。
5. Write 完了数が前回から変化していれば、グリッド再構築を強制する。
6. `RefreshStatus()` で上部ステータスと履歴プログレスを更新する。
7. `RefreshFunctions()` で F1-F8 ボタンのラベルと有効/無効を更新する。
8. `forceGrid` が true の場合だけ現在画面のグリッドを再構築する。

33ms タイマーは UI 表示の再描画トリガーです。バックエンドの重要更新自体は `UpdateCoordinator::CriticalLoop()` の別スレッドで実行されます。

## 画面切替順序

ナビゲーションボタンが押されると `OnNavCommand()` が呼ばれます。

1. ボタン ID から `MainScreenId` を算出する。
2. `SwitchScreen()` に渡す。
3. `currentScreen_` を更新する。
4. `navExpanded_` を false に戻す。
5. `LayoutControls()` で配置を更新する。
6. `RefreshUi(true)` でグリッドを含めて再描画する。

## 通常終了順序

終了経路は主に `OnCancel()` とデストラクタです。

`OnCancel()` の順序は次の通りです。

1. `coordinator_` が存在すれば `coordinator_->Stop()` を呼ぶ。
2. `CDialogEx::OnCancel()` に処理を委譲し、モーダルダイアログを閉じる。

`CMainDialog::~CMainDialog()` でも `coordinator_->Stop()` を呼びます。`UpdateCoordinator::Stop()` は `running_` の `exchange(false)` により二重停止を無害化するため、`OnCancel()` とデストラクタの両方から呼ばれても実処理は一度だけです。

## `UpdateCoordinator` 停止順序

`UpdateCoordinator::Stop()` の現行順序は次の通りです。

1. `running_.exchange(false)` を実行し、既に停止済みなら即 return する。
2. `historyCancelRequested_ = true` にして履歴取得へ中断要求を出す。
3. `writeCv_.notify_all()` で Write スレッドの待機を解除する。
4. `criticalThread_` が joinable なら join する。
5. `normalThread_` が joinable なら join する。
6. `writeThread_` が joinable なら join する。
7. `historyThread_` が joinable なら join する。

この順序により、UI から見た終了時には更新スレッド、Write スレッド、履歴スレッドが全て合流済みになります。
