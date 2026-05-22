#pragma once

#include "ExternalProcessLauncher.h"
#include "DataCatalog.h"
#include "DataGateway.h"
#include "FunctionBarModel.h"
#include "CustomGridCtrl.h"
#include "ContainerListCtrl.h"
#include "NavigationModel.h"
#include "ScheduleMutationModel.h"
#include "StationLayoutCtrl.h"
#include "BridgeFactory.h"
#include "ScreenModels.h"
#include "UpdateScheduler.h"
#include "resource.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>

#include <memory>
#include <optional>
#include <vector>
#include <array>

class CMainDialog final : public CDialogEx
{
public:
    /**
     * @brief 起動オプションを受け取り、主要依存を構築する。
     */
    explicit CMainDialog(BridgeFactoryOptions options);
    ~CMainDialog() override;

protected:
    /**
     * @brief コントロール生成・接続開始・タイマー起動を行う。
     */
    BOOL OnInitDialog() override;
    /**
     * @brief OK は業務上不要のため無効化する。
     */
    void OnOK() override;
    /**
     * @brief ダイアログ閉じる時に更新ループを停止する。
     */
    void OnCancel() override;
    /**
     * @brief キーボードF1-F8を画面下部ファンクション操作へ接続する。
     */
    BOOL PreTranslateMessage(MSG* message) override;

    /**
     * @brief 33ms 周期の再描画トリガーを受け取り UI を更新する。
     */
    afx_msg void OnTimer(UINT_PTR eventId);
    /**
     * @brief ダイアログサイズ変更時にレイアウト再計算する。
     */
    afx_msg void OnSize(UINT type, int cx, int cy);
    /**
     * @brief 左ナビゲーションボタンの画面切り替えを処理する。
     */
    afx_msg void OnNavCommand(UINT id);
    /**
     * @brief F1-F8 相当の操作を画面種別ごとに分岐実行する。
     */
    afx_msg void OnFunctionCommand(UINT id);
    /**
     * @brief 左ナビの 1/2 列化を切り替える。
     */
    afx_msg void OnNavExpand();
    /**
     * @brief 一覧選択変更時に selectedContainerNo_ を更新する。
     */
    afx_msg void OnListItemChanged(NMHDR* notify, LRESULT* result);
    /**
     * @brief ステーション配置図クリック時に selectedContainerNo_ を更新する。
     */
    afx_msg void OnStationLayoutClicked();
    /**
     * @brief コンテナ一覧カードクリック時に selectedContainerNo_ を更新する。
     */
    afx_msg void OnContainerListLayoutClicked();
    /**
     * @brief グリッド編集確定通知を処理する。
     */
    afx_msg LRESULT OnGridEditCommitted(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    /**
     * @brief 画面共通コンポーネントを構築する。
     */
    void CreateControls();
    /**
     * @brief クライアントサイズに合わせて各コントロール位置を再配置する。
     */
    void LayoutControls();
    /**
     * @brief バックエンド接続と更新コーディネータ起動をまとめて行う。
     */
    void ConnectAndStart();
    /**
     * @brief 必要に応じてグリッド更新を含めた再描画を行う。
     */
    void RefreshUi(bool forceGrid);
    /**
     * @brief ステータス欄と進捗を最新値で更新する。
     */
    void RefreshStatus(const UpdateSnapshot& snapshot);
    /**
     * @brief 画面種別に応じたファンクションボタン定義を描画する。
     */
    void RefreshFunctions(const UpdateSnapshot& snapshot);
    /**
     * @brief 画面切替後の状態同期を行う。
     */
    void SwitchScreen(MainScreenId screen);
    /**
     * @brief 現在画面を構築し、グリッドへ反映する。
     */
    void PopulateCurrentScreen(const UpdateSnapshot& snapshot);
    /**
     * @brief `GridModel` を画面表示に適用する。
     */
    void PopulateGrid(const GridModel& grid);
    /**
     * @brief ステーション画面の右詳細パネルを更新する。
     */
    void PopulateStation(const StationSnapshot& snapshot);
    /**
     * @brief 選択中コンテナの詳細を読み取り専用で表示する。
     */
    void ShowContainerDetails();
    /**
     * @brief スケジュール行の詳細閲覧操作。
     */
    void ShowScheduleDetails(int row);
    /**
     * @brief スケジュール行の順序更新を要求する。
     */
    void ChangeScheduleOrder(int row);
    /**
     * @brief 選択行を直前表示行へ繰り上げるWriteを要求する。
     */
    void MoveScheduleItemUp(int row);
    /**
     * @brief 選択中の保守異常行の詳細を読み取り専用で表示する。
     */
    void ShowMaintenanceDetails();
    /**
     * @brief 出庫予定追加を要求する。
     */
    void AddScheduleItem();
    /**
     * @brief 選択行の出庫予定削除を要求する。
     */
    void DeleteScheduleItem(int row);
    /**
     * @brief スケジュール順序セルの編集確定をWriteキューへ反映する。
     */
    void HandleScheduleGridEdit(const GridEditCommit& commit);
    /**
     * @brief 現在表示順でSchedule順序を10刻みに再採番する。
     */
    void RenumberScheduleRows();
    /**
     * @brief 直近のSchedule操作をUndoする。
     */
    void UndoScheduleMutation();
    /**
     * @brief 選択中の外部アプリ行を起動する。
     */
    void LaunchSelectedExternalApp();
    /**
     * @brief 現在のスケジュール選択が繰上げ可能か判定する。
     */
    bool CanMoveScheduleSelectionUp() const;
    /**
     * @brief Schedule mutation batch が完了待ちかを返す。
     */
    bool HasPendingScheduleMutation() const noexcept;
    /**
     * @brief Schedule mutation Write 群を一括投入し、完了後のUndo制御情報を保持する。
     */
    void EnqueueScheduleMutation(std::wstring label,
                                 const std::vector<ScheduleCellWrite>& writes,
                                 ScheduleUndoEntry undoEntry,
                                 bool restoreUndoOnFailure = false);
    /**
     * @brief pending中のSchedule mutationが完了した場合に履歴/失敗表示を更新する。
     */
    void CompletePendingScheduleMutation(const SchedulerMetrics& metrics);
    /**
     * @brief 同じ出庫順序が既に存在する場合に警告して true を返す。
     */
    bool WarnIfDuplicateScheduleOrder(int order, GridRowBinding excludedBinding);
    /**
     * @brief システム画面で選択中の外部アプリIDを取得する。
     */
    std::wstring SelectedExternalAppId() const;
    /**
     * @brief 現在選択中の保守行を取得する。
     */
    const MaintenanceStatusRow* SelectedMaintenanceRow() const;
    /**
     * @brief 現在画面の表示名を取得する。
     */
    CString ScreenTitle() const;

    DataCatalog catalog_;
    BridgeFactoryOptions bridgeOptions_;
    std::shared_ptr<IBackendBridge> bridge_;
    std::unique_ptr<UpdateCoordinator> coordinator_;
    std::vector<ExternalAppDefinition> externalApps_;
    std::unique_ptr<IExternalProcessLauncher> processLauncher_;
    ExternalLaunchResult lastExternalLaunchResult_;
    bool hasExternalLaunchResult_{false};

    struct PendingScheduleMutation
    {
        std::wstring label;
        int expectedWriteCompletedCount{};
        int baseScheduleMutationErrorCount{};
        ScheduleUndoEntry undoEntry;
        bool restoreUndoOnFailure{false};
    };

    CStatic statusText_;
    CProgressCtrl historyProgress_;
    CStatic detailText_;
    CCustomGridCtrl contentList_;
    CContainerListCtrl containerListLayout_;
    CStationLayoutCtrl stationLayout_;
    CStatic navOverlay_;
    CButton expandButton_;
    std::vector<NavigationItem> navItems_;
    std::vector<std::unique_ptr<CButton>> navButtons_;
    std::array<CButton, 8> functionButtons_;

    MaintenanceStatusModel currentMaintenanceStatus_;
    ScheduleUndoStack scheduleUndoStack_{20};
    std::optional<PendingScheduleMutation> pendingScheduleMutation_;
    std::wstring scheduleOperationMessage_;
    MainScreenId currentScreen_{MainScreenId::Station};
    bool navExpanded_{false};
    int selectedContainerNo_{1};
    int selectedMaintenanceDataId_{0};
    int lastSeenWriteCompletedCount_{0};
    unsigned int timerTicks_{0};
};
