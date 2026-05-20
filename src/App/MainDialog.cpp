#include "MainDialog.h"

#include "HistoryRequestDialog.h"
#include "OrderEditDialog.h"

#include <algorithm>
#include <sstream>

/**
 * @file MainDialog.cpp
 * @brief Main screen orchestration: timer updates, screen switching, and rendering.
 */

namespace {

constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 33;

/**
 * @brief Resolve current screen title for status output.
 */
const wchar_t* ScreenName(MainScreenId screen)
{
    switch (screen) {
    case MainScreenId::Station:
        return L"コンテナステーション";
    case MainScreenId::ContainerList:
        return L"コンテナ一覧";
    case MainScreenId::Schedule:
        return L"コンテナスケジュール";
    case MainScreenId::System:
        return L"システム";
    case MainScreenId::Maintenance:
        return L"コンテナ保守";
    default:
        return L"";
    }
}

CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

} // namespace

BEGIN_MESSAGE_MAP(CMainDialog, CDialogEx)
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_COMMAND_RANGE(IDC_NAV_BASE, IDC_NAV_BASE + 4, &CMainDialog::OnNavCommand)
    ON_COMMAND_RANGE(IDC_FUNCTION_BASE, IDC_FUNCTION_BASE + 7, &CMainDialog::OnFunctionCommand)
    ON_BN_CLICKED(IDC_NAV_EXPAND, &CMainDialog::OnNavExpand)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_CONTENT_LIST, &CMainDialog::OnListItemChanged)
END_MESSAGE_MAP()

/**
 * @brief Construct main dialog with startup bridge options.
 */
CMainDialog::CMainDialog(BridgeFactoryOptions options)
    : CDialogEx(IDD_MFCAPPLICATION7_DIALOG)
    , catalog_(LoadConfiguredCatalogOrDefault(options.catalogPath))
    , bridgeOptions_(std::move(options))
    , bridge_(CreateBackendBridge(bridgeOptions_))
{
}

/**
 * @brief Stop scheduler when dialog is destroyed.
 */
CMainDialog::~CMainDialog()
{
    if (coordinator_) {
        coordinator_->Stop();
    }
}

/**
 * @brief Build controls and start periodic refresh on initialization.
 */
BOOL CMainDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(L"MFCApplication7 縦切り基盤");
    MoveWindow(50, 50, 1280, 800);

    CreateControls();
    LayoutControls();
    ConnectAndStart();
    SetTimer(kRefreshTimerId, kRefreshIntervalMs, nullptr);
    RefreshUi(true);
    return TRUE;
}

/**
 * @brief Close action intentionally disabled for modal command consistency.
 */
void CMainDialog::OnOK()
{
}

/**
 * @brief Stop coordinator and close dialog.
 */
void CMainDialog::OnCancel()
{
    if (coordinator_) {
        coordinator_->Stop();
    }
    CDialogEx::OnCancel();
}

/**
 * @brief Drive periodic refresh processing from refresh timer.
 */
void CMainDialog::OnTimer(UINT_PTR eventId)
{
    if (eventId == kRefreshTimerId) {
        ++timerTicks_;
        RefreshUi(timerTicks_ % 15 == 0);
        return;
    }
    CDialogEx::OnTimer(eventId);
}

void CMainDialog::OnSize(UINT type, int cx, int cy)
{
    CDialogEx::OnSize(type, cx, cy);
    if (statusText_.GetSafeHwnd() != nullptr) {
        LayoutControls();
    }
}

/**
 * @brief Route navigation command buttons into screen changes.
 */
void CMainDialog::OnNavCommand(UINT id)
{
    const auto index = static_cast<int>(id - IDC_NAV_BASE);
    SwitchScreen(static_cast<MainScreenId>(index));
}

/**
 * @brief Dispatch system/schedule/station function-bar actions based on current screen.
 */
void CMainDialog::OnFunctionCommand(UINT id)
{
    const auto slot = static_cast<int>(id - IDC_FUNCTION_BASE) + 1;
    if (currentScreen_ == MainScreenId::System && coordinator_) {
        if (slot == 1) {
            CHistoryRequestDialog dialog(this);
            if (dialog.DoModal() == IDOK) {
                coordinator_->StartHistoryLoad(dialog.Request());
                RefreshUi(true);
            }
            return;
        }
        if (slot == 2) {
            coordinator_->CancelHistoryLoad();
            RefreshUi(true);
            return;
        }
        return;
    }

    if (currentScreen_ == MainScreenId::Schedule) {
        POSITION position = contentList_.GetFirstSelectedItemPosition();
        const int selectedRow = position == nullptr ? -1 : contentList_.GetNextSelectedItem(position);
        if (slot == 1) {
            ShowScheduleDetails(selectedRow);
            return;
        }
        if (slot == 2) {
            ChangeScheduleOrder(selectedRow);
            return;
        }
    }

    if ((currentScreen_ == MainScreenId::Station || currentScreen_ == MainScreenId::ContainerList) && slot == 1) {
        CString message;
        message.Format(L"コンテナ %d の詳細表示を開きます。", selectedContainerNo_);
        AfxMessageBox(message, MB_OK | MB_ICONINFORMATION);
    }
}

/**
 * @brief Toggle nav pane expansion mode for compact/expanded layout.
 */
void CMainDialog::OnNavExpand()
{
    navExpanded_ = !navExpanded_;
    LayoutControls();
}

/**
 * @brief Update selected container and coordinator binding when selection changes.
 */
void CMainDialog::OnListItemChanged(NMHDR* notify, LRESULT* result)
{
    const auto* change = reinterpret_cast<NMLISTVIEW*>(notify);
    if ((change->uChanged & LVIF_STATE) != 0 && (change->uNewState & LVIS_SELECTED) != 0) {
        if (currentScreen_ == MainScreenId::Station || currentScreen_ == MainScreenId::ContainerList) {
            selectedContainerNo_ = std::max(1, change->iItem + 1);
            if (coordinator_) {
                coordinator_->SetSelectedContainer(selectedContainerNo_);
            }
        }
    }
    *result = 0;
}

/**
 * @brief Create all UI controls and default labels for each screen group.
 */
void CMainDialog::CreateControls()
{
    statusText_.Create(L"COM未接続", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 0, 0), this, IDC_STATUS_TEXT);
    historyProgress_.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(0, 0, 0, 0), this, IDC_HISTORY_PROGRESS);
    detailText_.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER, CRect(0, 0, 0, 0), this, IDC_DETAIL_TEXT);
    contentList_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, CRect(0, 0, 0, 0), this, IDC_CONTENT_LIST);
    contentList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    const std::array<const wchar_t*, 5> labels = {L"ST", L"LIST", L"SCH", L"SYS", L"MNT"};
    for (size_t i = 0; i < navButtons_.size(); ++i) {
        navButtons_[i].Create(labels[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 0, 0), this, IDC_NAV_BASE + static_cast<UINT>(i));
    }

    expandButton_.Create(L">>", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 0, 0), this, IDC_NAV_EXPAND);

    for (size_t i = 0; i < functionButtons_.size(); ++i) {
        CString label;
        label.Format(L"F%zu", i + 1);
        functionButtons_[i].Create(label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 0, 0), this, IDC_FUNCTION_BASE + static_cast<UINT>(i));
    }
}

/**
 * @brief Apply responsive layout each render cycle and screen transition.
 */
void CMainDialog::LayoutControls()
{
    CRect client;
    GetClientRect(&client);
    const int topHeight = 64;
    const int bottomHeight = 64;
    const int navWidth = navExpanded_ ? 176 : 88;
    const int gap = 8;

    statusText_.MoveWindow(gap, gap, client.Width() - 240, topHeight - gap * 2);
    historyProgress_.MoveWindow(client.Width() - 220, 18, 200, 22);

    const int navTop = topHeight + gap;
    const int navBottom = client.Height() - bottomHeight - gap;
    const int navButtonHeight = 48;
    const int navButtonWidth = navExpanded_ ? 78 : 64;
    for (size_t i = 0; i < navButtons_.size(); ++i) {
        const int column = navExpanded_ ? static_cast<int>(i % 2) : 0;
        const int row = navExpanded_ ? static_cast<int>(i / 2) : static_cast<int>(i);
        navButtons_[i].MoveWindow(gap + column * (navButtonWidth + gap), navTop + row * (navButtonHeight + gap), navButtonWidth, navButtonHeight);
    }
    expandButton_.MoveWindow(gap, navBottom - navButtonHeight, navButtonWidth, navButtonHeight);

    const int contentLeft = navWidth + gap;
    const int contentTop = topHeight + gap;
    const int detailWidth = currentScreen_ == MainScreenId::Station ? client.Width() / 3 : 0;
    contentList_.MoveWindow(contentLeft, contentTop, client.Width() - contentLeft - detailWidth - gap, client.Height() - topHeight - bottomHeight - gap * 2);
    detailText_.ShowWindow(currentScreen_ == MainScreenId::Station ? SW_SHOW : SW_HIDE);
    detailText_.MoveWindow(client.Width() - detailWidth, contentTop, detailWidth - gap, client.Height() - topHeight - bottomHeight - gap * 2);

    const int functionWidth = (client.Width() - gap * 9) / 8;
    const int functionTop = client.Height() - bottomHeight + gap;
    for (size_t i = 0; i < functionButtons_.size(); ++i) {
        functionButtons_[i].MoveWindow(gap + static_cast<int>(i) * (functionWidth + gap), functionTop, functionWidth, bottomHeight - gap * 2);
    }
}

/**
 * @brief Connect gateway and launch scheduler threads.
 */
void CMainDialog::ConnectAndStart()
{
    DataGateway gateway(bridge_);
    const auto connectResult = gateway.Connect(bridgeOptions_.ipAddress);
    if (connectResult != BridgeError::Ok) {
        statusText_.SetWindowText(ToCString(L"接続失敗: " + ToDisplayText(connectResult)));
        return;
    }

    coordinator_ = std::make_unique<UpdateCoordinator>(catalog_, gateway);
    coordinator_->Start();
}

/**
 * @brief Refresh status/function bar and optionally grid from latest coordinator snapshot.
 */
void CMainDialog::RefreshUi(bool forceGrid)
{
    if (!coordinator_) {
        return;
    }
    const auto snapshot = coordinator_->Snapshot();
    const auto metrics = coordinator_->Metrics();
    if (metrics.writeCompletedCount != lastSeenWriteCompletedCount_) {
        lastSeenWriteCompletedCount_ = metrics.writeCompletedCount;
        forceGrid = true;
    }
    RefreshStatus(snapshot);
    RefreshFunctions(snapshot);
    if (forceGrid) {
        PopulateCurrentScreen(snapshot);
    }
}

/**
 * @brief Render top status line and progress bar from snapshot and metrics.
 */
void CMainDialog::RefreshStatus(const UpdateSnapshot& snapshot)
{
    const auto metrics = coordinator_->Metrics();
    std::wostringstream text;
    text << L"画面: " << ScreenTitle().GetString()
         << L" / 重要更新: " << metrics.criticalCycles
         << L" / 期限超過: " << metrics.criticalDeadlineMisses
         << L" / 通常更新: " << metrics.normalCycles;
    if (metrics.lastWriteStartDelayMs >= 0) {
        text << L" / 最終Write開始遅延: " << metrics.lastWriteStartDelayMs << L"ms";
        text << L" / Write完了: " << metrics.writeCompletedCount;
        text << L" / 最終Write結果: " << ToDisplayText(metrics.lastWriteErrorCode);
    }
    if (!snapshot.historyStatusText.empty()) {
        text << L" / 履歴: " << snapshot.historyStatusText;
        text << L" " << snapshot.historyProgress << L"%";
        text << L" / 履歴Read: " << metrics.historyReadCount;
        if (metrics.historyErrorCount > 0) {
            text << L" / 履歴エラー: " << metrics.historyErrorCount
                 << L"(" << ToDisplayText(metrics.historyLastErrorCode) << L")";
        }
    }
    if (!snapshot.criticalValues.empty() && snapshot.criticalValues.front().errorCode != BridgeError::Ok) {
        text << L" / " << ToDisplayText(snapshot.criticalValues.front().errorCode);
    }
    statusText_.SetWindowText(ToCString(text.str()));
    historyProgress_.SetPos(snapshot.historyProgress);
}

/**
 * @brief Recompute function bar button labels and enabled states.
 */
void CMainDialog::RefreshFunctions(const UpdateSnapshot& snapshot)
{
    std::vector<FunctionAction> actions;
    if (currentScreen_ == MainScreenId::Station || currentScreen_ == MainScreenId::ContainerList) {
        actions = BuildContainerFunctionActions(true, snapshot.station.selected.missing);
    } else if (currentScreen_ == MainScreenId::Schedule) {
        actions = BuildScheduleFunctionActions(contentList_.GetFirstSelectedItemPosition() != nullptr);
    } else if (currentScreen_ == MainScreenId::System) {
        actions = BuildSystemFunctionActions(snapshot.historyRunning);
    } else {
        actions = BuildBlankFunctionActions();
    }

    for (size_t i = 0; i < functionButtons_.size(); ++i) {
        CString label;
        label.Format(L"F%zu", i + 1);
        if (i < actions.size() && !actions[i].label.empty()) {
            label += L"\n";
            label += actions[i].label.c_str();
        }
        functionButtons_[i].SetWindowText(label);
        functionButtons_[i].EnableWindow(i < actions.size() && actions[i].enabled);
    }
}

/**
 * @brief Switch current view screen and force a fresh render.
 */
void CMainDialog::SwitchScreen(MainScreenId screen)
{
    currentScreen_ = screen;
    navExpanded_ = false;
    LayoutControls();
    RefreshUi(true);
}

/**
 * @brief Build grid content for current screen from snapshot/gateway.
 */
void CMainDialog::PopulateCurrentScreen(const UpdateSnapshot& snapshot)
{
    DataGateway gateway(bridge_);
    switch (currentScreen_) {
    case MainScreenId::Station:
        PopulateStation(snapshot.station);
        break;
    case MainScreenId::ContainerList:
        PopulateGrid(BuildContainerListGrid(snapshot.station));
        break;
    case MainScreenId::Schedule:
        PopulateGrid(BuildScheduleGrid(gateway));
        break;
    case MainScreenId::System: {
        GridModel grid;
        grid.SetColumns({L"日オフセット", L"番号", L"値", L"状態"});
        std::wstring status = snapshot.historyStatusText.empty() ? L"待機" : snapshot.historyStatusText;
        status += L" " + std::to_wstring(snapshot.historyProgress) + L"%";
        grid.AddRow({GridCell::Text(L"状態"), GridCell::Text(L""), GridCell::Text(status), GridCell::Text(snapshot.historyRunning ? L"取得中" : L"待機")});
        for (const auto& record : snapshot.historyRecords) {
            grid.AddRow({
                GridCell::Text(std::to_wstring(record.dayOffset)),
                GridCell::Text(std::to_wstring(record.recordIndex)),
                GridCell::Text(record.displayText),
                GridCell::Text(record.stale ? ToDisplayText(record.errorCode) : L"OK"),
            });
        }
        PopulateGrid(grid);
        break;
    }
    case MainScreenId::Maintenance:
        PopulateGrid(BuildMaintenanceGrid(gateway));
        break;
    }
}

/**
 * @brief Apply a prepared grid model to the list control.
 */
void CMainDialog::PopulateGrid(const GridModel& grid)
{
    contentList_.ApplyModel(grid);
}

/**
 * @brief Populate station screen list + detail side text for selected container.
 */
void CMainDialog::PopulateStation(const StationSnapshot& snapshot)
{
    PopulateGrid(BuildContainerListGrid(snapshot));

    std::wostringstream detail;
    detail << L"選択コンテナ: " << snapshot.selected.containerNo << L"\r\n"
           << L"名称: " << snapshot.selected.containerName << L"\r\n"
           << L"状態: " << snapshot.selected.state << L"\r\n\r\n";
    for (const auto& item : snapshot.selected.items) {
        detail << item.itemName << L" / 入庫 " << item.inboundDate << L" / 出庫 " << item.outboundStart
               << L" / 順序 " << item.outboundOrder << L" / 作業 " << item.workTime << L"\r\n";
    }
    detailText_.SetWindowText(ToCString(detail.str()));
}

/**
 * @brief Open detail message for selected schedule row.
 */
void CMainDialog::ShowScheduleDetails(int row)
{
    if (row < 0) {
        return;
    }

    const auto binding = contentList_.RowBindingAt(row);
    if (binding.containerNo <= 0 || binding.itemNo <= 0) {
        return;
    }

    CString message;
    message.Format(L"コンテナ %d / 品目 %d の詳細表示を開きます。", binding.containerNo, binding.itemNo);
    AfxMessageBox(message, MB_OK | MB_ICONINFORMATION);
}

/**
 * @brief Open edit dialog and write changed order value back to coordinator queue.
 */
void CMainDialog::ChangeScheduleOrder(int row)
{
    if (row < 0 || coordinator_ == nullptr) {
        return;
    }

    const auto binding = contentList_.RowBindingAt(row);
    if (binding.containerNo <= 0 || binding.itemNo <= 0) {
        return;
    }

    const std::wstring itemName(contentList_.GetItemText(row, 1).GetString());
    const std::wstring currentOrder(contentList_.GetItemText(row, 3).GetString());
    COrderEditDialog dialog(binding.containerNo, itemName, currentOrder, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    coordinator_->RequestWrite({2103, binding.containerNo, binding.itemNo, DataStyle::Raw}, dialog.OrderText());
    RefreshUi(false);
}

/**
 * @brief Return localized label for current screen.
 */
CString CMainDialog::ScreenTitle() const
{
    return CString(ScreenName(currentScreen_));
}
