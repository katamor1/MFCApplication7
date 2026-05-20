# MFCApplication7 現行仕様書

この仕様書セットは、MFCApplication7 の現行コードから確認できる実装仕様を、保守者向けに整理したものです。`docs/概要.md` に書かれている要求・構想のうち、未実装または部分実装の内容は現行仕様とは分けて扱います。

## 仕様書の読み方

- 全体構成と依存関係を把握する場合は [architecture.md](architecture.md) を読む。
- 起動、接続、タイマー開始、終了時の停止順序を確認する場合は [runtime-lifecycle.md](runtime-lifecycle.md) を読む。
- バックエンド API、COM 呼び出し、データ ID、画面モデルへのデータ流れを確認する場合は [data-api-flow.md](data-api-flow.md) を読む。
- スレッド、優先度、ロック、履歴取得、Write 応答性を確認する場合は [threading.md](threading.md) を読む。
- 画面、ナビゲーション、F1-F8、グリッド、現行 UI 制約を確認する場合は [ui-behavior.md](ui-behavior.md) を読む。
- 仕様書作成時点の確認結果と未確認事項は [verification.md](verification.md) を読む。

## 現行仕様として扱う範囲

この仕様書で「現行仕様」と呼ぶものは、次の根拠で確認できる動作だけです。

- `src/App`、`src/Core`、`src/BackendBridgeMock` の実装。
- `tests/CoreTests`、`tests/PerformanceTest` の期待動作。
- `config/data_catalog.json` と生成済みの `docs/data-catalog.md`。
- `docs/概要.md` と `docs/unimplemented-from-overview.md` に記録済みの要求・未実装差分。

`docs/概要.md` の将来要求であっても、コードで未実装のものは現行仕様ではありません。必要な箇所では「未実装/差分」と明記します。

## 現行システムの要約

MFCApplication7 は、MFC ダイアログアプリケーションを入口に、Core 層のデータ取得・画面モデル・更新スケジューラを組み合わせた業務向けフロントエンド検証アプリケーションです。バックエンド接続は `IBackendBridge` 抽象に閉じ込められており、現行ではインプロセスモックと COM 経由ブリッジを起動引数で切り替えます。

UI は `CMainDialog` が保持する MFC コントロールで構成され、画面更新は UI スレッドの 33ms タイマーで `UpdateCoordinator` のスナップショットを読む方式です。バックエンドの周期取得や Write 処理は `UpdateCoordinator` 内のワーカースレッドで実行されます。

## 主要な現行制約

- 正式な外部 COM サーバーおよび実バックエンド接続は、このコードだけでは確認できない。
- ステーション画面の物理配置風表示、3列コンテナ一覧、詳細ダイアログ本体、F1-F8 キーボード入力変換などは未実装または部分実装である。
- カスタムグリッドは `CellKind` をモデル上保持するが、現行描画は `CListCtrl` の文字列表現であり、セル内エディタは実装されていない。
- 重要更新 30fps、Write 開始 100ms 以内、履歴取得並行性はモック環境のテストで確認する設計であり、実 COM/実ネットワークでの保証は未確認である。
