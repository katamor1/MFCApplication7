#include "MFCApplication7App.h"

#include "BridgeFactory.h"
#include "DataGateway.h"
#include "ExternalProcessLauncher.h"
#include "FunctionBarModel.h"
#include "MainDialog.h"
#include "ScreenModels.h"
#include "StatusSummary.h"
#include "UpdateScheduler.h"

#include <afxcmn.h>

#include <algorithm>
#include <chrono>
#include <thread>

CMFCApplication7App theApp;

/**
 * @file MFCApplication7App.cpp
 * @brief Application bootstrap and startup-time self-test paths.
 */

namespace {

/**
 * @brief Check whether command line contains a specific argument token.
 * @param commandLine Raw argument string.
 * @param argument Target token.
 * @return true if token appears in commandLine.
 */
bool HasArgument(const std::wstring& commandLine, const wchar_t* argument)
{
    return commandLine.find(argument) != std::wstring::npos;
}

/**
 * @brief Poll until completed write count reaches the expected number.
 */
bool WaitForWriteCount(const UpdateCoordinator& coordinator, int expectedCount)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (coordinator.Metrics().writeCompletedCount >= expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Poll until critical snapshot values are available.
 */
bool WaitForCriticalValues(const UpdateCoordinator& coordinator, size_t expectedCount)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (coordinator.Snapshot().criticalValues.size() >= expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Check whether schedule grid contains a bound row.
 */
bool HasScheduleRow(const GridModel& grid, int containerNo, int itemNo)
{
    for (const auto& row : grid.Rows()) {
        if (row.binding.containerNo == containerNo && row.binding.itemNo == itemNo) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check whether read-only detail rows contain an exact key/value pair.
 */
bool HasDetailRow(const ReadOnlyDetailModel& detail, const std::wstring& label, const std::wstring& value)
{
    return std::any_of(detail.rows.begin(), detail.rows.end(), [&](const DetailRow& row) {
        return row.label == label && row.value == value;
    });
}

/**
 * @brief Return the visible schedule row index for a bound item.
 */
int ScheduleRowIndex(const GridModel& grid, int containerNo, int itemNo)
{
    for (size_t index = 0; index < grid.Rows().size(); ++index) {
        const auto& binding = grid.Rows()[index].binding;
        if (binding.containerNo == containerNo && binding.itemNo == itemNo) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

/**
 * @brief Poll until history load begins and reports progress.
 */
bool WaitForHistoryProgress(const UpdateCoordinator& coordinator)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto snapshot = coordinator.Snapshot();
        if (snapshot.historyRunning && snapshot.historyProgress > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Poll until history loading stops.
 */
bool WaitForHistoryStop(const UpdateCoordinator& coordinator)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!coordinator.Snapshot().historyRunning) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Poll until normal update loop has run at least once.
 */
bool WaitForNormalCycles(const UpdateCoordinator& coordinator)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (coordinator.Metrics().normalCycles > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Run write-path smoke test for coordinator + gateway.
 */
int RunWriteSmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 50;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"9876");
    if (!WaitForWriteCount(coordinator, 1)) {
        coordinator.Stop();
        return 60;
    }
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        return 70;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok) {
        return 80;
    }

    const auto readback = gateway.Read({2103, 1, 1, DataStyle::Raw});
    if (readback.errorCode != BridgeError::Ok || readback.displayText != L"9876") {
        return 90;
    }
    return 0;
}

/**
 * @brief Run history-load smoke test and cancel path for scheduler.
 */
int RunHistorySmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 100;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    if (!coordinator.StartHistoryLoad({3})) {
        coordinator.Stop();
        return 110;
    }
    if (!WaitForHistoryProgress(coordinator)) {
        coordinator.Stop();
        return 120;
    }
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"1357");
    if (!WaitForWriteCount(coordinator, 1)) {
        coordinator.Stop();
        return 130;
    }
    coordinator.CancelHistoryLoad();
    if (!WaitForHistoryStop(coordinator)) {
        coordinator.Stop();
        return 140;
    }
    coordinator.Stop();

    const auto snapshot = coordinator.Snapshot();
    const auto metrics = coordinator.Metrics();
    if (!snapshot.historyCancelled || metrics.historyCancelCount < 1 || metrics.historyReadCount <= 0) {
        return 150;
    }
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        return 160;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok) {
        return 170;
    }
    if (metrics.criticalCycles <= 0) {
        return 180;
    }
    return 0;
}

/**
 * @brief Run schedule add/delete write smoke test.
 */
int RunScheduleMutationSmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 200;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    const ScheduleAddRequest addRequest{1, 3, 2222, L"SMOKE-ITEM"};
    coordinator.RequestWrite({2104, addRequest.containerNo, addRequest.itemNo, DataStyle::Raw}, EncodeScheduleAddValue(addRequest));
    if (!WaitForWriteCount(coordinator, 1)) {
        coordinator.Stop();
        return 210;
    }
    if (!HasScheduleRow(BuildScheduleGrid(gateway), 1, 3)) {
        coordinator.Stop();
        return 220;
    }

    coordinator.RequestWrite({2105, 1, 3, DataStyle::Raw}, L"1");
    if (!WaitForWriteCount(coordinator, 2)) {
        coordinator.Stop();
        return 230;
    }
    if (HasScheduleRow(BuildScheduleGrid(gateway), 1, 3)) {
        coordinator.Stop();
        return 240;
    }
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        return 250;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok || metrics.lastScheduleMutationErrorCode != BridgeError::Ok) {
        return 260;
    }
    if (metrics.scheduleAddCompletedCount != 1 || metrics.scheduleDeleteCompletedCount != 1) {
        return 270;
    }
    return 0;
}

/**
 * @brief Run common status summary smoke test.
 */
int RunStatusSmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 400;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    if (!WaitForCriticalValues(coordinator, catalog.CriticalKeys().size())) {
        coordinator.Stop();
        return 410;
    }
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"3141");
    if (!WaitForWriteCount(coordinator, 1)) {
        coordinator.Stop();
        return 420;
    }
    if (!coordinator.StartHistoryLoad({1})) {
        coordinator.Stop();
        return 430;
    }
    if (!WaitForHistoryProgress(coordinator)) {
        coordinator.Stop();
        return 440;
    }

    const auto snapshot = coordinator.Snapshot();
    const auto metrics = coordinator.Metrics();
    coordinator.CancelHistoryLoad();
    WaitForHistoryStop(coordinator);
    coordinator.Stop();

    const auto summary = BuildStatusSummary(catalog,
                                            snapshot,
                                            metrics,
                                            {L"自己診断", L"2026/05/21 07:03:00", L"selftest"});
    if (summary.displayText.find(L"日時: 2026/05/21 07:03:00") == std::wstring::npos ||
        summary.displayText.find(L"ユーザー: selftest") == std::wstring::npos ||
        summary.displayText.find(L"画面: 自己診断") == std::wstring::npos ||
        summary.displayText.find(L"業務状態: 正常") == std::wstring::npos ||
        summary.displayText.find(L"重要: 正常") == std::wstring::npos ||
        summary.displayText.find(L"最終Write結果: OK") == std::wstring::npos ||
        summary.displayText.find(L"履歴: ") == std::wstring::npos) {
        return 450;
    }
    if (summary.criticalItems.size() != catalog.CriticalKeys().size()) {
        return 460;
    }
    return 0;
}

/**
 * @brief Run fixed station-layout model smoke test.
 */
int RunStationLayoutSmoke(const BridgeFactoryOptions& options)
{
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 500;
    }

    const auto station = BuildStationSnapshot(gateway, 29);
    const auto layout = BuildStationLayoutModel(station, 29);
    if (layout.columnCount != 5 || layout.rowsPerColumn != 20 || layout.cells.size() != 100) {
        return 510;
    }
    if (layout.cells[0].containerNo != 1 ||
        layout.cells[0].column != 0 ||
        layout.cells[0].row != 0 ||
        layout.cells[0].kind != StationLayoutKind::LeftSemiCircle) {
        return 520;
    }
    if (layout.cells[20].containerNo != 21 ||
        layout.cells[20].column != 1 ||
        layout.cells[20].row != 0 ||
        layout.cells[20].kind != StationLayoutKind::Straight) {
        return 530;
    }
    if (layout.cells[99].containerNo != 100 ||
        layout.cells[99].column != 4 ||
        layout.cells[99].row != 19 ||
        layout.cells[99].kind != StationLayoutKind::RightSemiCircle) {
        return 540;
    }
    const auto selectedCount = std::count_if(layout.cells.begin(), layout.cells.end(), [](const StationLayoutCell& cell) {
        return cell.selected;
    });
    if (selectedCount != 1 || !layout.cells[28].selected || !layout.cells[28].missing || !station.selected.missing) {
        return 550;
    }
    return 0;
}

/**
 * @brief Run row-major 3-column container-list layout smoke test.
 */
int RunContainerListLayoutSmoke(const BridgeFactoryOptions& options)
{
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 600;
    }

    const auto station = BuildStationSnapshot(gateway, 29);
    const auto layout = BuildContainerListLayoutModel(station, 29);
    if (layout.columnCount != 3 || layout.rowCount != 34 || layout.cells.size() != 100) {
        return 610;
    }
    if (layout.cells[0].containerNo != 1 ||
        layout.cells[0].column != 0 ||
        layout.cells[0].row != 0 ||
        layout.cells[1].containerNo != 2 ||
        layout.cells[1].column != 1 ||
        layout.cells[1].row != 0 ||
        layout.cells[2].containerNo != 3 ||
        layout.cells[2].column != 2 ||
        layout.cells[2].row != 0 ||
        layout.cells[3].containerNo != 4 ||
        layout.cells[3].column != 0 ||
        layout.cells[3].row != 1) {
        return 620;
    }
    if (layout.cells[99].containerNo != 100 ||
        layout.cells[99].column != 0 ||
        layout.cells[99].row != 33) {
        return 630;
    }
    const auto selectedCount = std::count_if(layout.cells.begin(), layout.cells.end(), [](const ContainerListCell& cell) {
        return cell.selected;
    });
    if (selectedCount != 1 || !layout.cells[28].selected || !layout.cells[28].missing || !station.selected.missing) {
        return 640;
    }
    return 0;
}

/**
 * @brief Run maintenance abnormal detail model smoke test.
 */
int RunMaintenanceDetailSmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 700;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    if (!WaitForCriticalValues(coordinator, catalog.CriticalKeys().size())) {
        coordinator.Stop();
        return 710;
    }

    auto snapshot = coordinator.Snapshot();
    coordinator.Stop();
    auto model = BuildMaintenanceStatusModel(catalog, snapshot);
    if (model.rows.size() != 20 || BuildMaintenanceStatusGrid(model).RowCount() != 20) {
        return 720;
    }
    if (snapshot.criticalValues.size() < 2) {
        return 730;
    }

    snapshot.criticalValues[1] = {L"異常検知", BridgeError::Timeout, {}, true};
    model = BuildMaintenanceStatusModel(catalog, snapshot);
    if (model.abnormalCount < 1 || !model.rows[1].abnormal || !model.rows[1].operationAvailable) {
        return 740;
    }

    const auto detail = BuildMaintenanceDetailModel(model.rows[1]);
    if (detail.title.find(model.rows[1].name) == std::wstring::npos || detail.rows.size() < 7) {
        return 750;
    }
    const auto actions = BuildMaintenanceFunctionActions(model.rows[1].abnormal);
    if (actions.empty() || !actions[0].enabled || actions[0].label != L"詳細") {
        return 760;
    }
    return 0;
}

/**
 * @brief Run container and schedule read-only detail model smoke test.
 */
int RunDetailSmoke(const BridgeFactoryOptions& options)
{
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 1100;
    }

    const auto containerSummary = BuildContainerSummary(gateway, 9, 5);
    const auto containerDetail = BuildContainerDetailModel(containerSummary);
    if (containerSummary.itemCount != 10 ||
        containerSummary.items.size() != 5 ||
        containerDetail.title != L"コンテナ詳細: 9") {
        return 1110;
    }
    if (!HasDetailRow(containerDetail, L"コンテナ番号", L"9") ||
        !HasDetailRow(containerDetail, L"名称", L"CNT-9") ||
        !HasDetailRow(containerDetail, L"品目数", L"10") ||
        !HasDetailRow(containerDetail, L"表示品目数", L"5") ||
        !HasDetailRow(containerDetail, L"品目1 名称", L"ITEM-9-1")) {
        return 1120;
    }

    const auto missingSummary = BuildContainerSummary(gateway, 29, 5);
    const auto missingActions = BuildContainerFunctionActions(true, missingSummary.missing);
    if (!missingSummary.missing || !missingSummary.items.empty() || missingActions.empty() || missingActions[0].enabled) {
        return 1130;
    }

    const auto scheduleDetail = BuildScheduleDetailModel(gateway, {1, 1});
    if (scheduleDetail.title != L"スケジュール詳細: コンテナ 1 / 品目 1" ||
        !HasDetailRow(scheduleDetail, L"品目名", L"ITEM-1-1") ||
        !HasDetailRow(scheduleDetail, L"出庫開始", L"2026/05/21 2:00") ||
        !HasDetailRow(scheduleDetail, L"出庫終了", L"2026/05/22 2:30") ||
        !HasDetailRow(scheduleDetail, L"出庫順序", L"11") ||
        !HasDetailRow(scheduleDetail, L"作業時間", L"00:05:45")) {
        return 1140;
    }
    return 0;
}

/**
 * @brief Run max-load mock smoke test with history, writes, and periodic updates.
 */
int RunMaxLoadSmoke(const BridgeFactoryOptions& options)
{
    auto maxOptions = options;
    if (maxOptions.bridgeMode == BridgeMode::InProcessMock) {
        maxOptions.mockLoadProfile = MockLoadProfile::MaxLoad;
    }

    const auto catalog = LoadConfiguredCatalogOrDefault(maxOptions.catalogPath);
    auto bridge = CreateBackendBridge(maxOptions);
    DataGateway gateway(bridge);
    if (gateway.Connect(maxOptions.ipAddress) != BridgeError::Ok) {
        return 800;
    }

    const auto grid = BuildScheduleGrid(gateway);
    if (maxOptions.bridgeMode == BridgeMode::InProcessMock && grid.RowCount() != 1000) {
        return 810;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    if (!coordinator.StartHistoryLoad({3})) {
        coordinator.Stop();
        return 820;
    }
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"1111");
    coordinator.RequestWrite({2103, 50, 5, DataStyle::Raw}, L"2222");
    coordinator.RequestWrite({2103, 100, 10, DataStyle::Raw}, L"3333");

    if (!WaitForCriticalValues(coordinator, catalog.CriticalKeys().size())) {
        coordinator.Stop();
        return 830;
    }
    if (!WaitForNormalCycles(coordinator)) {
        coordinator.Stop();
        return 840;
    }
    if (!WaitForHistoryProgress(coordinator)) {
        coordinator.Stop();
        return 850;
    }
    if (!WaitForWriteCount(coordinator, 3)) {
        coordinator.Stop();
        return 860;
    }
    coordinator.CancelHistoryLoad();
    WaitForHistoryStop(coordinator);
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    if (metrics.criticalCycles <= 0 || metrics.normalCycles <= 0 || metrics.historyReadCount <= 0) {
        return 870;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok || metrics.writeCompletedCount < 3) {
        return 880;
    }
    if (metrics.maxWriteStartDelayMs < 0 || metrics.maxWriteStartDelayMs > 100 || metrics.writeStartDelayExceededCount != 0) {
        return 890;
    }
    return 0;
}

/**
 * @brief Run custom grid in-place edit smoke test without showing the main dialog.
 */
int RunGridEditSmoke()
{
    CString windowClass = AfxRegisterWndClass(0);
    CWnd parent;
    if (!parent.CreateEx(0, windowClass, L"GridEditSmoke", WS_OVERLAPPED, CRect(0, 0, 640, 240), nullptr, 0)) {
        return 900;
    }

    CCustomGridCtrl grid;
    if (!grid.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                     CRect(0, 0, 620, 140),
                     &parent,
                     IDC_CONTENT_LIST)) {
        parent.DestroyWindow();
        return 910;
    }
    grid.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    GridModel model;
    model.SetColumns({L"Text", L"Spin", L"Combo", L"Radio", L"Check"});
    model.AddRow({GridCell::Text(L"alpha", CellKind::Text),
                  GridCell::Text(L"10", CellKind::Spin),
                  GridCell::Text(L"A", CellKind::ComboBox, {L"A", L"B"}),
                  GridCell::Text(L"R1", CellKind::RadioButton, {L"R1", L"R2"}),
                  GridCell::Text(L"false", CellKind::CheckBox)},
                 {7, 8, 9});
    grid.ApplyModel(model);

    if (grid.BeginEditCell(0, 0)) {
        parent.DestroyWindow();
        return 920;
    }
    grid.SetEditingEnabled(true);

    if (!grid.BeginEditCell(0, 0)) {
        parent.DestroyWindow();
        return 930;
    }
    CWnd* edit = grid.GetDlgItem(IDC_GRID_INPLACE_EDIT);
    if (edit == nullptr) {
        parent.DestroyWindow();
        return 940;
    }
    edit->SetWindowText(L"beta");
    edit->SendMessage(WM_KEYDOWN, VK_RETURN, 0);
    if (grid.Model().Rows()[0].cells[0].text != L"beta" ||
        grid.LastEditCommit().oldText != L"alpha" ||
        grid.LastEditCommit().newText != L"beta" ||
        grid.LastEditCommit().binding.containerNo != 7) {
        parent.DestroyWindow();
        return 950;
    }

    if (!grid.BeginEditCell(0, 1)) {
        parent.DestroyWindow();
        return 960;
    }
    edit = grid.GetDlgItem(IDC_GRID_INPLACE_EDIT);
    if (edit == nullptr) {
        parent.DestroyWindow();
        return 970;
    }
    edit->SetWindowText(L"25");
    edit->SendMessage(WM_KEYDOWN, VK_RETURN, 0);
    if (grid.Model().Rows()[0].cells[1].text != L"25" || grid.LastEditCommit().kind != CellKind::Spin) {
        parent.DestroyWindow();
        return 980;
    }

    if (!grid.BeginEditCell(0, 2)) {
        parent.DestroyWindow();
        return 990;
    }
    auto* combo = static_cast<CComboBox*>(grid.GetDlgItem(IDC_GRID_INPLACE_COMBO));
    if (combo == nullptr) {
        parent.DestroyWindow();
        return 1000;
    }
    combo->SetCurSel(1);
    combo->SendMessage(WM_KEYDOWN, VK_RETURN, 0);
    if (grid.Model().Rows()[0].cells[2].text != L"B" || grid.LastEditCommit().kind != CellKind::ComboBox) {
        parent.DestroyWindow();
        return 1010;
    }

    if (!grid.BeginEditCell(0, 3)) {
        parent.DestroyWindow();
        return 1020;
    }
    combo = static_cast<CComboBox*>(grid.GetDlgItem(IDC_GRID_INPLACE_COMBO));
    if (combo == nullptr) {
        parent.DestroyWindow();
        return 1030;
    }
    combo->SetCurSel(1);
    combo->SendMessage(WM_KEYDOWN, VK_RETURN, 0);
    if (grid.Model().Rows()[0].cells[3].text != L"R2" || grid.LastEditCommit().kind != CellKind::RadioButton) {
        parent.DestroyWindow();
        return 1040;
    }

    if (!grid.BeginEditCell(0, 4)) {
        parent.DestroyWindow();
        return 1050;
    }
    if (grid.Model().Rows()[0].cells[4].text != L"true" || grid.LastEditCommit().kind != CellKind::CheckBox) {
        parent.DestroyWindow();
        return 1060;
    }

    if (!grid.BeginEditCell(0, 0)) {
        parent.DestroyWindow();
        return 1070;
    }
    edit = grid.GetDlgItem(IDC_GRID_INPLACE_EDIT);
    if (edit == nullptr) {
        parent.DestroyWindow();
        return 1080;
    }
    edit->SetWindowText(L"cancelled");
    edit->SendMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    if (grid.Model().Rows()[0].cells[0].text != L"beta") {
        parent.DestroyWindow();
        return 1090;
    }

    grid.DestroyWindow();
    parent.DestroyWindow();
    return 0;
}

/**
 * @brief Run schedule order sort and move-up write smoke test.
 */
int RunScheduleOrderSmoke(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 300;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"9000");
    coordinator.RequestWrite({2103, 2, 1, DataStyle::Raw}, L"1");
    coordinator.RequestWrite({2103, 10, 1, DataStyle::Raw}, L"2");
    if (!WaitForWriteCount(coordinator, 3)) {
        coordinator.Stop();
        return 310;
    }

    auto grid = BuildScheduleGrid(gateway);
    if (grid.RowCount() < 2 ||
        grid.Rows()[0].binding.containerNo != 2 || grid.Rows()[0].binding.itemNo != 1 ||
        grid.Rows()[1].binding.containerNo != 10 || grid.Rows()[1].binding.itemNo != 1) {
        coordinator.Stop();
        return 320;
    }

    const int targetRow = ScheduleRowIndex(grid, 10, 1);
    const auto writes = BuildScheduleMoveUpWrites(grid, targetRow);
    if (writes.size() != 2) {
        coordinator.Stop();
        return 330;
    }
    for (const auto& write : writes) {
        coordinator.RequestWrite(write.key, write.value);
    }
    if (!WaitForWriteCount(coordinator, 5)) {
        coordinator.Stop();
        return 340;
    }

    grid = BuildScheduleGrid(gateway);
    coordinator.Stop();
    if (grid.RowCount() < 2 ||
        grid.Rows()[0].binding.containerNo != 10 || grid.Rows()[0].binding.itemNo != 1 ||
        grid.Rows()[1].binding.containerNo != 2 || grid.Rows()[1].binding.itemNo != 1) {
        return 350;
    }

    const auto metrics = coordinator.Metrics();
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        return 360;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok || metrics.scheduleOrderWriteCompletedCount < 5) {
        return 370;
    }
    return 0;
}

/**
 * @brief Run external app launch smoke test through fake launcher without side effects.
 */
int RunExternalLaunchSmoke()
{
    const auto apps = BuildDefaultExternalAppDefinitions();
    if (apps.size() != 1 || apps[0].id != L"container-controller") {
        return 1200;
    }

    FakeExternalProcessLauncher launcher;
    const auto first = launcher.Launch(apps[0]);
    if (!first.success || first.alreadyRunning || first.message != L"起動しました") {
        return 1210;
    }
    auto grid = BuildSystemGrid(UpdateSnapshot{}, apps, &first);
    if (grid.RowCount() == 0 || grid.Rows()[0].cells[2].text != L"起動済み") {
        return 1220;
    }

    const auto duplicate = launcher.Launch(apps[0]);
    if (!duplicate.success || !duplicate.alreadyRunning || duplicate.message != L"起動済み") {
        return 1230;
    }
    grid = BuildSystemGrid(UpdateSnapshot{}, apps, &duplicate);
    if (grid.Rows()[0].cells[2].text != L"起動済み" ||
        grid.Rows()[0].cells[3].text.find(L"起動済み") == std::wstring::npos) {
        return 1240;
    }

    auto failingApp = apps[0];
    failingApp.id = L"container-controller-failure";
    failingApp.label = L"コンテナコントローラ失敗";
    launcher.FailNext(2, L"fake failure");
    const auto failure = launcher.Launch(failingApp);
    if (failure.success || failure.errorCode != 2 || failure.message != L"fake failure") {
        return 1250;
    }
    grid = BuildSystemGrid(UpdateSnapshot{}, {failingApp}, &failure);
    if (grid.Rows()[0].cells[2].text != L"起動失敗" ||
        grid.Rows()[0].cells[3].text.find(L"fake failure") == std::wstring::npos) {
        return 1260;
    }

    return 0;
}

/**
 * @brief Run core behavior checks and optional smoke subtests.
 */
int RunSelfTest(const BridgeFactoryOptions& options,
                bool writeSmoke,
                bool historySmoke,
                bool scheduleMutationSmoke,
                bool scheduleOrderSmoke,
                bool statusSmoke,
                bool stationLayoutSmoke,
                bool containerListLayoutSmoke,
                bool maintenanceDetailSmoke,
                bool detailSmoke,
                bool gridEditSmoke,
                bool maxLoadSmoke,
                bool externalLaunchSmoke)
{
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        return 10;
    }

    const auto station = BuildStationSnapshot(gateway, 1);
    if (station.containers.size() != 100 || station.selected.items.empty()) {
        return 20;
    }
    if (BuildScheduleGrid(gateway).RowCount() == 0) {
        return 30;
    }
    if (BuildMaintenanceGrid(gateway).RowCount() != 20) {
        return 40;
    }
    if (writeSmoke) {
        return RunWriteSmoke(options);
    }
    if (historySmoke) {
        return RunHistorySmoke(options);
    }
    if (statusSmoke) {
        return RunStatusSmoke(options);
    }
    if (stationLayoutSmoke) {
        return RunStationLayoutSmoke(options);
    }
    if (containerListLayoutSmoke) {
        return RunContainerListLayoutSmoke(options);
    }
    if (maintenanceDetailSmoke) {
        return RunMaintenanceDetailSmoke(options);
    }
    if (detailSmoke) {
        return RunDetailSmoke(options);
    }
    if (gridEditSmoke) {
        return RunGridEditSmoke();
    }
    if (maxLoadSmoke) {
        return RunMaxLoadSmoke(options);
    }
    if (externalLaunchSmoke) {
        return RunExternalLaunchSmoke();
    }
    if (scheduleMutationSmoke) {
        return RunScheduleMutationSmoke(options);
    }
    if (scheduleOrderSmoke) {
        return RunScheduleOrderSmoke(options);
    }
    return 0;
}

} // namespace

/**
 * @brief Initialize app, optional self-test execution, and open main dialog.
 */
BOOL CMFCApplication7App::InitInstance()
{
    CWinApp::InitInstance();

    INITCOMMONCONTROLSEX initControls{};
    initControls.dwSize = sizeof(initControls);
    initControls.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&initControls);

    const std::wstring commandLine = m_lpCmdLine == nullptr ? L"" : std::wstring(m_lpCmdLine);
    const auto bridgeOptions = ParseBridgeFactoryOptions(commandLine);
    if (HasArgument(commandLine, L"/SelfTest")) {
        ::ExitProcess(static_cast<UINT>(RunSelfTest(bridgeOptions,
                                                    HasArgument(commandLine, L"/WriteSmoke"),
                                                    HasArgument(commandLine, L"/HistorySmoke"),
                                                    HasArgument(commandLine, L"/ScheduleMutationSmoke"),
                                                    HasArgument(commandLine, L"/ScheduleOrderSmoke"),
                                                    HasArgument(commandLine, L"/StatusSmoke"),
                                                    HasArgument(commandLine, L"/StationLayoutSmoke"),
                                                    HasArgument(commandLine, L"/ContainerListLayoutSmoke"),
                                                    HasArgument(commandLine, L"/MaintenanceDetailSmoke"),
                                                    HasArgument(commandLine, L"/DetailSmoke"),
                                                    HasArgument(commandLine, L"/GridEditSmoke"),
                                                    HasArgument(commandLine, L"/MaxLoadSmoke"),
                                                    HasArgument(commandLine, L"/ExternalLaunchSmoke"))));
    }

    CMainDialog dialog(bridgeOptions);
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
