#include "MFCApplication7App.h"

#include "BridgeFactory.h"
#include "DataGateway.h"
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
 * @brief Run core behavior checks and optional smoke subtests.
 */
int RunSelfTest(const BridgeFactoryOptions& options,
                bool writeSmoke,
                bool historySmoke,
                bool scheduleMutationSmoke,
                bool scheduleOrderSmoke,
                bool statusSmoke,
                bool stationLayoutSmoke)
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
                                                    HasArgument(commandLine, L"/StationLayoutSmoke"))));
    }

    CMainDialog dialog(bridgeOptions);
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
