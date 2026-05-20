#include "MFCApplication7App.h"

#include "BridgeFactory.h"
#include "DataGateway.h"
#include "MainDialog.h"
#include "ScreenModels.h"
#include "UpdateScheduler.h"

#include <afxcmn.h>

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
 * @brief Run core behavior checks and optional smoke subtests.
 */
int RunSelfTest(const BridgeFactoryOptions& options, bool writeSmoke, bool historySmoke)
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
        ::ExitProcess(static_cast<UINT>(RunSelfTest(bridgeOptions, HasArgument(commandLine, L"/WriteSmoke"), HasArgument(commandLine, L"/HistorySmoke"))));
    }

    CMainDialog dialog(bridgeOptions);
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
