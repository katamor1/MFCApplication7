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

namespace {

bool HasArgument(const std::wstring& commandLine, const wchar_t* argument)
{
    return commandLine.find(argument) != std::wstring::npos;
}

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

int RunSelfTest(const BridgeFactoryOptions& options, bool writeSmoke)
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
    return 0;
}

} // namespace

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
        ::ExitProcess(static_cast<UINT>(RunSelfTest(bridgeOptions, HasArgument(commandLine, L"/WriteSmoke"))));
    }

    CMainDialog dialog(bridgeOptions);
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
