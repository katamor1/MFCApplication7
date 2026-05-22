#include "MainDialog.h"

#include "HistoryRequestDialog.h"
#include "OrderEditDialog.h"
#include "ReadOnlyDetailDialog.h"
#include "ScheduleAddDialog.h"
#include "ScheduleDeleteConfirmDialog.h"
#include "StatusSummary.h"

#include <algorithm>
#include <sstream>
#include <utility>

/**
 * @file MainDialog.cpp
 * @brief Main screen orchestration: timer updates, screen switching, and rendering.
 */

namespace {

constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 33;

CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

bool IsSelectedContainerMissing(const StationSnapshot& station, int selectedContainerNo)
{
    const auto found = std::find_if(station.containers.begin(), station.containers.end(), [selectedContainerNo](const ContainerSummary& container) {
        return container.containerNo == selectedContainerNo;
    });
    if (found == station.containers.end()) {
        return true;
    }
    return found->missing;
}

std::wstring CurrentDateTimeText()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[20]{};
    swprintf_s(buffer,
               L"%04u/%02u/%02u %02u:%02u:%02u",
               now.wYear,
               now.wMonth,
               now.wDay,
               now.wHour,
               now.wMinute,
               now.wSecond);
    return buffer;
}

std::wstring CurrentUserName()
{
    wchar_t buffer[256]{};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (GetUserNameW(buffer, &size) == FALSE || size == 0) {
        return L"unknown";
    }
    return buffer;
}

bool TryParseScheduleOrderText(const std::wstring& value, int& order)
{
    constexpr int kMaxOrder = 9999;
    if (value.empty()) {
        return false;
    }

    int parsed = 0;
    for (const wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        parsed = (parsed * 10) + (ch - L'0');
        if (parsed > kMaxOrder) {
            return false;
        }
    }

    if (parsed <= 0) {
        return false;
    }
    order = parsed;
    return true;
}

} // namespace

BEGIN_MESSAGE_MAP(CMainDialog, CDialogEx)
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_COMMAND_RANGE(IDC_NAV_BASE, IDC_NAV_BASE + 14, &CMainDialog::OnNavCommand)
    ON_COMMAND_RANGE(IDC_FUNCTION_BASE, IDC_FUNCTION_BASE + 7, &CMainDialog::OnFunctionCommand)
    ON_BN_CLICKED(IDC_NAV_EXPAND, &CMainDialog::OnNavExpand)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_CONTENT_LIST, &CMainDialog::OnListItemChanged)
    ON_BN_CLICKED(IDC_STATION_LAYOUT, &CMainDialog::OnStationLayoutClicked)
    ON_BN_CLICKED(IDC_CONTAINER_LIST_LAYOUT, &CMainDialog::OnContainerListLayoutClicked)
    ON_MESSAGE(WM_GRID_EDIT_COMMITTED, &CMainDialog::OnGridEditCommitted)
END_MESSAGE_MAP()

/**
 * @brief Construct main dialog with startup bridge options.
 */
CMainDialog::CMainDialog(BridgeFactoryOptions options)
    : CDialogEx(IDD_MFCAPPLICATION7_DIALOG)
    , catalog_(LoadConfiguredCatalogOrDefault(options.catalogPath))
    , bridgeOptions_(std::move(options))
    , bridge_(CreateBackendBridge(bridgeOptions_))
    , navItems_(BuildDefaultNavigationItems())
    , externalApps_(BuildDefaultExternalAppDefinitions())
    , processLauncher_(std::make_unique<Win32ExternalProcessLauncher>())
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
 * @brief Route physical F1-F8 key presses to enabled function buttons.
 */
BOOL CMainDialog::PreTranslateMessage(MSG* message)
{
    if (message != nullptr && message->message == WM_KEYDOWN) {
        const int slot = FunctionSlotFromVirtualKey(static_cast<int>(message->wParam));
        if (slot >= 1 && slot <= static_cast<int>(functionButtons_.size())) {
            const auto index = static_cast<size_t>(slot - 1);
            if (functionButtons_[index].GetSafeHwnd() != nullptr && functionButtons_[index].IsWindowEnabled()) {
                OnFunctionCommand(IDC_FUNCTION_BASE + static_cast<UINT>(index));
            }
            return TRUE;
        }
    }
    return CDialogEx::PreTranslateMessage(message);
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
    if (index < 0 || index >= static_cast<int>(navItems_.size())) {
        return;
    }
    SwitchScreen(navItems_[static_cast<size_t>(index)].screen);
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
        if (slot == 3) {
            LaunchSelectedExternalApp();
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
        if (slot == 3) {
            AddScheduleItem();
            return;
        }
        if (slot == 4) {
            DeleteScheduleItem(selectedRow);
            return;
        }
        if (slot == 5) {
            MoveScheduleItemUp(selectedRow);
            return;
        }
        if (slot == 6) {
            RenumberScheduleRows();
            return;
        }
        if (slot == 7) {
            UndoScheduleMutation();
            return;
        }
    }

    if (currentScreen_ == MainScreenId::Maintenance) {
        if (slot == 1) {
            ShowMaintenanceDetails();
            return;
        }
        return;
    }

    if ((currentScreen_ == MainScreenId::Station || currentScreen_ == MainScreenId::ContainerList) && slot == 1) {
        ShowContainerDetails();
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
        } else if (currentScreen_ == MainScreenId::Maintenance) {
            selectedMaintenanceDataId_ = contentList_.RowBindingAt(change->iItem).dataId;
            if (coordinator_) {
                RefreshFunctions(coordinator_->Snapshot());
            }
        } else if (currentScreen_ == MainScreenId::System && coordinator_) {
            RefreshFunctions(coordinator_->Snapshot());
        }
    }
    *result = 0;
}

/**
 * @brief Update selected station container from layout click notification.
 */
void CMainDialog::OnStationLayoutClicked()
{
    selectedContainerNo_ = stationLayout_.SelectedContainerNo();
    if (coordinator_) {
        coordinator_->SetSelectedContainer(selectedContainerNo_);
    }
    RefreshUi(true);
}

/**
 * @brief Update selected container from container-list card click notification.
 */
void CMainDialog::OnContainerListLayoutClicked()
{
    selectedContainerNo_ = containerListLayout_.SelectedContainerNo();
    if (coordinator_) {
        coordinator_->SetSelectedContainer(selectedContainerNo_);
    }
    RefreshUi(true);
}

/**
 * @brief Route custom-grid edit commits into the current screen command path.
 */
LRESULT CMainDialog::OnGridEditCommitted(WPARAM wParam, LPARAM lParam)
{
    if (static_cast<UINT>(wParam) != IDC_CONTENT_LIST ||
        reinterpret_cast<CCustomGridCtrl*>(lParam) != &contentList_ ||
        currentScreen_ != MainScreenId::Schedule) {
        return 0;
    }

    HandleScheduleGridEdit(contentList_.LastEditCommit());
    return 0;
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
    contentList_.SetEditingEnabled(false);
    containerListLayout_.Create(WS_CHILD | WS_BORDER, CRect(0, 0, 0, 0), this, IDC_CONTAINER_LIST_LAYOUT);
    stationLayout_.Create(WS_CHILD | WS_BORDER, CRect(0, 0, 0, 0), this, IDC_STATION_LAYOUT);
    navOverlay_.Create(L"", WS_CHILD | SS_WHITERECT | WS_BORDER, CRect(0, 0, 0, 0), this, IDC_NAV_OVERLAY);

    navButtons_.reserve(navItems_.size());
    for (size_t i = 0; i < navItems_.size(); ++i) {
        auto button = std::make_unique<CButton>();
        button->Create(ToCString(navItems_[i].shortLabel), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 0, 0), this, IDC_NAV_BASE + static_cast<UINT>(i));
        navButtons_.push_back(std::move(button));
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
    const int collapsedNavWidth = 88;
    const int expandedNavWidth = 176;
    const int gap = 8;

    statusText_.MoveWindow(gap, gap, client.Width() - 240, topHeight - gap * 2);
    historyProgress_.MoveWindow(client.Width() - 220, 18, 200, 22);

    const int navTop = topHeight + gap;
    const int navBottom = client.Height() - bottomHeight - gap;
    const int navButtonHeight = 48;
    const int navButtonWidth = navExpanded_ ? 78 : 64;
    const auto navCells = BuildNavigationCells(navItems_, currentScreen_, navExpanded_);
    navOverlay_.ShowWindow(navExpanded_ ? SW_SHOW : SW_HIDE);
    navOverlay_.MoveWindow(0, topHeight, expandedNavWidth, std::max(1, client.Height() - topHeight - bottomHeight));
    for (size_t i = 0; i < navButtons_.size(); ++i) {
        if (i >= navCells.size()) {
            navButtons_[i]->ShowWindow(SW_HIDE);
            continue;
        }
        const auto& cell = navCells[i];
        navButtons_[i]->ShowWindow(SW_SHOW);
        navButtons_[i]->SetWindowText(ToCString(cell.shortLabel));
        navButtons_[i]->MoveWindow(gap + cell.column * (navButtonWidth + gap), navTop + cell.row * (navButtonHeight + gap), navButtonWidth, navButtonHeight);
    }
    expandButton_.SetWindowText(navExpanded_ ? L"<<" : L">>");
    expandButton_.MoveWindow(gap, navBottom - navButtonHeight, navButtonWidth, navButtonHeight);

    const int contentLeft = collapsedNavWidth + gap;
    const int contentTop = topHeight + gap;
    const int detailWidth = currentScreen_ == MainScreenId::Station ? client.Width() / 3 : 0;
    const int contentWidth = std::max(1, client.Width() - contentLeft - detailWidth - gap);
    const int contentHeight = std::max(1, client.Height() - topHeight - bottomHeight - gap * 2);
    if (currentScreen_ == MainScreenId::Station) {
        contentList_.ShowWindow(SW_HIDE);
        containerListLayout_.ShowWindow(SW_HIDE);
        stationLayout_.ShowWindow(SW_SHOW);
        stationLayout_.MoveWindow(contentLeft, contentTop, contentWidth, contentHeight);
    } else if (currentScreen_ == MainScreenId::ContainerList) {
        contentList_.ShowWindow(SW_HIDE);
        stationLayout_.ShowWindow(SW_HIDE);
        containerListLayout_.ShowWindow(SW_SHOW);
        containerListLayout_.MoveWindow(contentLeft, contentTop, contentWidth, contentHeight);
    } else {
        stationLayout_.ShowWindow(SW_HIDE);
        containerListLayout_.ShowWindow(SW_HIDE);
        contentList_.ShowWindow(SW_SHOW);
        contentList_.MoveWindow(contentLeft, contentTop, contentWidth, contentHeight);
    }
    detailText_.ShowWindow(currentScreen_ == MainScreenId::Station ? SW_SHOW : SW_HIDE);
    detailText_.MoveWindow(client.Width() - detailWidth, contentTop, std::max(1, detailWidth - gap), contentHeight);

    const int functionWidth = (client.Width() - gap * 9) / 8;
    const int functionTop = client.Height() - bottomHeight + gap;
    for (size_t i = 0; i < functionButtons_.size(); ++i) {
        functionButtons_[i].MoveWindow(gap + static_cast<int>(i) * (functionWidth + gap), functionTop, functionWidth, bottomHeight - gap * 2);
    }

    if (navExpanded_) {
        navOverlay_.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (const auto& button : navButtons_) {
            button->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        expandButton_.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    CompletePendingScheduleMutation(metrics);
    if (metrics.writeCompletedCount != lastSeenWriteCompletedCount_) {
        lastSeenWriteCompletedCount_ = metrics.writeCompletedCount;
        forceGrid = true;
    }
    contentList_.SetEditingEnabled(currentScreen_ == MainScreenId::Schedule && !HasPendingScheduleMutation());
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
    const StatusContext context{
        std::wstring(ScreenTitle().GetString()),
        CurrentDateTimeText(),
        CurrentUserName(),
    };
    const auto summary = BuildStatusSummary(catalog_, snapshot, metrics, context);
    auto displayText = summary.displayText;
    if (!scheduleOperationMessage_.empty()) {
        displayText += L" / 予定操作: " + scheduleOperationMessage_;
    }
    statusText_.SetWindowText(ToCString(displayText));
    historyProgress_.SetPos(snapshot.historyProgress);
}

/**
 * @brief Recompute function bar button labels and enabled states.
 */
void CMainDialog::RefreshFunctions(const UpdateSnapshot& snapshot)
{
    std::vector<FunctionAction> actions;
    if (currentScreen_ == MainScreenId::Station || currentScreen_ == MainScreenId::ContainerList) {
        const bool missing = IsSelectedContainerMissing(snapshot.station, selectedContainerNo_);
        actions = BuildContainerFunctionActions(true, missing);
    } else if (currentScreen_ == MainScreenId::Schedule) {
        const bool hasSelection = contentList_.GetFirstSelectedItemPosition() != nullptr;
        const bool hasRows = contentList_.Model().RowCount() > 0;
        const bool pending = HasPendingScheduleMutation();
        actions = BuildScheduleFunctionActions(hasSelection,
                                               hasSelection && CanMoveScheduleSelectionUp(),
                                               hasRows,
                                               scheduleUndoStack_.CanUndo(),
                                               pending);
    } else if (currentScreen_ == MainScreenId::System) {
        actions = BuildSystemFunctionActions(snapshot.historyRunning, !SelectedExternalAppId().empty());
    } else if (currentScreen_ == MainScreenId::Maintenance) {
        const auto* selected = SelectedMaintenanceRow();
        actions = BuildMaintenanceFunctionActions(selected != nullptr && selected->abnormal);
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
    contentList_.SetEditingEnabled(currentScreen_ == MainScreenId::Schedule && !HasPendingScheduleMutation());
    switch (currentScreen_) {
    case MainScreenId::Station:
        PopulateStation(snapshot.station);
        break;
    case MainScreenId::ContainerList:
        containerListLayout_.ApplyModel(BuildContainerListLayoutModel(snapshot.station, selectedContainerNo_));
        break;
    case MainScreenId::Schedule:
        PopulateGrid(BuildScheduleGrid(gateway));
        break;
    case MainScreenId::System: {
        PopulateGrid(BuildSystemGrid(snapshot,
                                     externalApps_,
                                     hasExternalLaunchResult_ ? &lastExternalLaunchResult_ : nullptr));
        break;
    }
    case MainScreenId::Maintenance:
        currentMaintenanceStatus_ = BuildMaintenanceStatusModel(catalog_, snapshot);
        PopulateGrid(BuildMaintenanceStatusGrid(currentMaintenanceStatus_));
        if (selectedMaintenanceDataId_ > 0) {
            const auto& rows = contentList_.Model().Rows();
            for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
                if (rows[static_cast<size_t>(row)].binding.dataId == selectedMaintenanceDataId_) {
                    contentList_.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    contentList_.EnsureVisible(row, FALSE);
                    break;
                }
            }
        }
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
    stationLayout_.ApplyModel(BuildStationLayoutModel(snapshot, selectedContainerNo_));

    const ContainerSummary* selected = &snapshot.selected;
    const auto found = std::find_if(snapshot.containers.begin(), snapshot.containers.end(), [this](const ContainerSummary& container) {
        return container.containerNo == selectedContainerNo_;
    });
    ContainerSummary summaryOnly;
    if (snapshot.selected.containerNo != selectedContainerNo_ && found != snapshot.containers.end()) {
        summaryOnly = *found;
        selected = &summaryOnly;
    }

    std::wostringstream detail;
    detail << L"選択コンテナ: " << selected->containerNo << L"\r\n"
           << L"名称: " << selected->containerName << L"\r\n"
           << L"状態: " << selected->state << L"\r\n\r\n";
    for (const auto& item : selected->items) {
        detail << item.itemName << L" / 入庫 " << item.inboundDate << L" / 出庫 " << item.outboundStart
               << L" / 順序 " << item.outboundOrder << L" / 作業 " << item.workTime << L"\r\n";
    }
    detailText_.SetWindowText(ToCString(detail.str()));
}

/**
 * @brief Open read-only details for the selected container.
 */
void CMainDialog::ShowContainerDetails()
{
    if (selectedContainerNo_ <= 0 || !bridge_) {
        return;
    }

    DataGateway gateway(bridge_);
    const auto summary = BuildContainerSummary(gateway, selectedContainerNo_, 5);
    if (summary.missing) {
        return;
    }

    CReadOnlyDetailDialog dialog(BuildContainerDetailModel(summary), this);
    dialog.DoModal();
}

/**
 * @brief Open read-only details for selected schedule row.
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

    DataGateway gateway(bridge_);
    CReadOnlyDetailDialog dialog(BuildScheduleDetailModel(gateway, binding), this);
    dialog.DoModal();
}

/**
 * @brief Open edit dialog and write changed order value back to coordinator queue.
 */
void CMainDialog::ChangeScheduleOrder(int row)
{
    if (row < 0 || coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto binding = contentList_.RowBindingAt(row);
    if (binding.containerNo <= 0 || binding.itemNo <= 0) {
        return;
    }

    const std::wstring itemName(contentList_.GetItemText(row, ScheduleGridColumn::ItemName).GetString());
    const std::wstring currentOrder(contentList_.GetItemText(row, ScheduleGridColumn::Order).GetString());
    COrderEditDialog dialog(binding.containerNo, itemName, currentOrder, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    int newOrder = 0;
    if (!TryParseScheduleOrderText(dialog.OrderText(), newOrder) ||
        WarnIfDuplicateScheduleOrder(newOrder, binding)) {
        RefreshUi(true);
        return;
    }

    auto writes = BuildScheduleCellEditWrites(binding, ScheduleGridColumn::Order, CellKind::Spin, dialog.OrderText());
    ScheduleUndoEntry undo{L"順序変更", BuildScheduleCellRestoreWrites(binding, ScheduleGridColumn::Order, currentOrder)};
    if (undo.writes.empty()) {
        scheduleOperationMessage_ = L"順序変更をUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"順序変更", writes, std::move(undo));
}

/**
 * @brief Enqueue the two writes needed for schedule order move-up.
 */
void CMainDialog::MoveScheduleItemUp(int row)
{
    if (row < 0 || coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto writes = BuildScheduleMoveUpWrites(contentList_.Model(), row);
    ScheduleUndoEntry undo{L"繰上げ", CaptureScheduleOrderRestoreWrites(contentList_.Model(), {row - 1, row})};
    if (!writes.empty() && undo.writes.empty()) {
        scheduleOperationMessage_ = L"繰上げをUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"繰上げ", writes, std::move(undo));
}

/**
 * @brief Open read-only details for the selected abnormal maintenance row.
 */
void CMainDialog::ShowMaintenanceDetails()
{
    const auto* selected = SelectedMaintenanceRow();
    if (selected == nullptr || !selected->abnormal) {
        return;
    }

    CReadOnlyDetailDialog dialog(BuildMaintenanceDetailModel(*selected), this);
    dialog.DoModal();
}

/**
 * @brief Open add dialog and enqueue a provisional schedule add write.
 */
void CMainDialog::AddScheduleItem()
{
    if (coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    CScheduleAddDialog dialog(this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    const auto request = dialog.Request();
    if (HasScheduleRowBinding(contentList_.Model(), {request.containerNo, request.itemNo})) {
        scheduleOperationMessage_ = L"追加先の予定行が既に存在するため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    if (WarnIfDuplicateScheduleOrder(request.order, {})) {
        RefreshUi(true);
        return;
    }

    const std::vector<ScheduleCellWrite> writes{
        {{2104, request.containerNo, request.itemNo, DataStyle::Raw}, EncodeScheduleAddValue(request)},
    };
    ScheduleUndoEntry undo{L"追加", BuildScheduleAddUndoWrites(request)};
    if (undo.writes.empty()) {
        scheduleOperationMessage_ = L"追加をUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"追加", writes, std::move(undo));
}

/**
 * @brief Confirm deletion for selected schedule row and enqueue delete write.
 */
void CMainDialog::DeleteScheduleItem(int row)
{
    if (row < 0 || coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto binding = contentList_.RowBindingAt(row);
    if (binding.containerNo <= 0 || binding.itemNo <= 0) {
        return;
    }
    if (row >= static_cast<int>(contentList_.Model().Rows().size())) {
        return;
    }

    const std::wstring itemName(contentList_.GetItemText(row, ScheduleGridColumn::ItemName).GetString());
    const std::wstring currentOrder(contentList_.GetItemText(row, ScheduleGridColumn::Order).GetString());
    CScheduleDeleteConfirmDialog dialog(binding.containerNo, binding.itemNo, itemName, currentOrder, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }

    const std::vector<ScheduleCellWrite> writes{
        {{2105, binding.containerNo, binding.itemNo, DataStyle::Raw}, L"1"},
    };
    ScheduleUndoEntry undo{L"削除", BuildScheduleDeleteUndoWrites(contentList_.Model().Rows()[static_cast<size_t>(row)])};
    if (undo.writes.empty()) {
        scheduleOperationMessage_ = L"削除をUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"削除", writes, std::move(undo));
}

/**
 * @brief Convert a committed schedule-cell edit into one queued schedule write.
 */
void CMainDialog::HandleScheduleGridEdit(const GridEditCommit& commit)
{
    if (coordinator_ == nullptr || commit.oldText == commit.newText) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        scheduleOperationMessage_ = L"前回の予定操作が完了するまでインセル編集は受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }

    const auto writes = BuildScheduleCellEditWrites(commit.binding, commit.column, commit.kind, commit.newText);
    if (writes.empty()) {
        RefreshUi(true);
        return;
    }
    if (commit.column == ScheduleGridColumn::Order) {
        int newOrder = 0;
        if (!TryParseScheduleOrderText(commit.newText, newOrder) ||
            WarnIfDuplicateScheduleOrder(newOrder, commit.binding)) {
            RefreshUi(true);
            return;
        }
    }

    ScheduleUndoEntry undo{L"セル編集", BuildScheduleCellRestoreWrites(commit.binding, commit.column, commit.oldText)};
    if (undo.writes.empty()) {
        scheduleOperationMessage_ = L"セル編集をUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"セル編集", writes, std::move(undo));
}

/**
 * @brief Renumber visible schedule rows to 10-step order values.
 */
void CMainDialog::RenumberScheduleRows()
{
    if (coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<int> rowIndices;
    const auto& rows = contentList_.Model().Rows();
    rowIndices.reserve(rows.size());
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        rowIndices.push_back(row);
    }

    const auto writes = BuildScheduleRenumberWrites(contentList_.Model());
    if (writes.empty()) {
        scheduleOperationMessage_ = L"再採番不要";
        RefreshUi(false);
        return;
    }

    ScheduleUndoEntry undo{L"再採番", CaptureScheduleOrderRestoreWrites(contentList_.Model(), rowIndices)};
    if (undo.writes.empty()) {
        scheduleOperationMessage_ = L"再採番をUndo用に記録できないため受付しません。";
        AfxMessageBox(scheduleOperationMessage_.c_str(), MB_OK | MB_ICONWARNING);
        RefreshUi(true);
        return;
    }
    EnqueueScheduleMutation(L"再採番", writes, std::move(undo));
}

/**
 * @brief Execute the newest schedule undo entry.
 */
void CMainDialog::UndoScheduleMutation()
{
    if (coordinator_ == nullptr) {
        return;
    }
    if (HasPendingScheduleMutation()) {
        AfxMessageBox(L"前回の予定操作が完了するまで待ってください。", MB_OK | MB_ICONWARNING);
        return;
    }

    auto undo = scheduleUndoStack_.Pop();
    if (!undo.has_value()) {
        scheduleOperationMessage_ = L"Undo履歴がありません。";
        RefreshUi(false);
        return;
    }

    const auto writes = undo->writes;
    EnqueueScheduleMutation(L"Undo: " + undo->label, writes, std::move(*undo), true);
}

/**
 * @brief Launch the selected system external app row through the process launcher boundary.
 */
void CMainDialog::LaunchSelectedExternalApp()
{
    const auto appId = SelectedExternalAppId();
    if (appId.empty() || processLauncher_ == nullptr) {
        return;
    }

    const auto found = std::find_if(externalApps_.begin(), externalApps_.end(), [&](const ExternalAppDefinition& app) {
        return app.id == appId;
    });
    if (found == externalApps_.end()) {
        return;
    }

    lastExternalLaunchResult_ = processLauncher_->Launch(*found);
    hasExternalLaunchResult_ = true;
    RefreshUi(true);
}

/**
 * @brief Check whether selected schedule row can swap with the previous visible row.
 */
bool CMainDialog::CanMoveScheduleSelectionUp() const
{
    POSITION position = contentList_.GetFirstSelectedItemPosition();
    if (position == nullptr) {
        return false;
    }
    const int selectedRow = contentList_.GetNextSelectedItem(position);
    return !BuildScheduleMoveUpWrites(contentList_.Model(), selectedRow).empty();
}

/**
 * @brief Return whether a schedule write batch is pending.
 */
bool CMainDialog::HasPendingScheduleMutation() const noexcept
{
    return pendingScheduleMutation_.has_value();
}

/**
 * @brief Enqueue a schedule mutation batch and remember its undo action.
 */
void CMainDialog::EnqueueScheduleMutation(std::wstring label,
                                          const std::vector<ScheduleCellWrite>& writes,
                                          ScheduleUndoEntry undoEntry,
                                          bool restoreUndoOnFailure)
{
    if (coordinator_ == nullptr || writes.empty() || HasPendingScheduleMutation()) {
        return;
    }

    const auto metrics = coordinator_->Metrics();
    PendingScheduleMutation pending;
    pending.label = std::move(label);
    pending.expectedWriteCompletedCount = metrics.writeCompletedCount + static_cast<int>(writes.size());
    pending.baseScheduleMutationErrorCount = metrics.scheduleMutationErrorCount;
    pending.undoEntry = std::move(undoEntry);
    pending.restoreUndoOnFailure = restoreUndoOnFailure;
    pendingScheduleMutation_ = std::move(pending);

    for (const auto& write : writes) {
        coordinator_->RequestWrite(write.key, write.value);
    }

    scheduleOperationMessage_ = pendingScheduleMutation_->label + L" 送信中";
    contentList_.SetEditingEnabled(false);
    RefreshUi(false);
}

/**
 * @brief Complete pending schedule mutation based on scheduler metrics.
 */
void CMainDialog::CompletePendingScheduleMutation(const SchedulerMetrics& metrics)
{
    if (!pendingScheduleMutation_.has_value() ||
        metrics.writeCompletedCount < pendingScheduleMutation_->expectedWriteCompletedCount) {
        return;
    }

    auto pending = std::move(*pendingScheduleMutation_);
    pendingScheduleMutation_.reset();

    const bool failed = metrics.scheduleMutationErrorCount > pending.baseScheduleMutationErrorCount ||
        metrics.lastScheduleMutationErrorCode != BridgeError::Ok ||
        metrics.lastWriteErrorCode != BridgeError::Ok;
    if (failed) {
        if (pending.restoreUndoOnFailure) {
            scheduleUndoStack_.Restore(std::move(pending.undoEntry));
        }
        const auto error = metrics.lastScheduleMutationErrorCode != BridgeError::Ok
            ? metrics.lastScheduleMutationErrorCode
            : metrics.lastWriteErrorCode;
        scheduleOperationMessage_ = pending.label + L" 失敗: " + ToDisplayText(error) + L"。再実行または状態確認を行ってください。";
        return;
    }

    if (!pending.restoreUndoOnFailure) {
        scheduleUndoStack_.Push(std::move(pending.undoEntry));
    }
    scheduleOperationMessage_ = pending.label + L" 完了";
}

/**
 * @brief Warn and reject when another visible schedule row already has the order.
 */
bool CMainDialog::WarnIfDuplicateScheduleOrder(int order, GridRowBinding excludedBinding)
{
    const auto duplicate = FindDuplicateScheduleOrder(contentList_.Model(), order, excludedBinding);
    if (!duplicate.found) {
        return false;
    }

    CString message;
    message.Format(L"同じ出庫順序 %d がコンテナ %d / 品目 %d に既に存在するため受付できません。",
                   order,
                   duplicate.binding.containerNo,
                   duplicate.binding.itemNo);
    scheduleOperationMessage_ = L"同じ出庫順序のため受付拒否";
    AfxMessageBox(message, MB_OK | MB_ICONWARNING);
    return true;
}

/**
 * @brief Return selected external app binding from the system grid.
 */
std::wstring CMainDialog::SelectedExternalAppId() const
{
    POSITION position = contentList_.GetFirstSelectedItemPosition();
    if (position == nullptr) {
        return {};
    }
    const int selectedRow = contentList_.GetNextSelectedItem(position);
    if (selectedRow < 0) {
        return {};
    }
    return contentList_.RowBindingAt(selectedRow).externalAppId;
}

/**
 * @brief Return currently selected maintenance row, if it still exists.
 */
const MaintenanceStatusRow* CMainDialog::SelectedMaintenanceRow() const
{
    if (selectedMaintenanceDataId_ <= 0) {
        return nullptr;
    }
    const auto found = std::find_if(currentMaintenanceStatus_.rows.begin(),
                                   currentMaintenanceStatus_.rows.end(),
                                   [this](const MaintenanceStatusRow& row) {
                                       return row.dataId == selectedMaintenanceDataId_;
                                   });
    if (found == currentMaintenanceStatus_.rows.end()) {
        return nullptr;
    }
    return &*found;
}

/**
 * @brief Return localized label for current screen.
 */
CString CMainDialog::ScreenTitle() const
{
    return ToCString(NavigationLabelForScreen(navItems_, currentScreen_));
}
