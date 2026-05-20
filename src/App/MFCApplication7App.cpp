#include "MFCApplication7App.h"

#include "BridgeFactory.h"
#include "DataGateway.h"
#include "MainDialog.h"
#include "ScreenModels.h"

#include <afxcmn.h>

CMFCApplication7App theApp;

namespace {

int RunSelfTest(const BridgeFactoryOptions& options)
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

    const auto bridgeOptions = ParseBridgeFactoryOptions(m_lpCmdLine == nullptr ? L"" : std::wstring(m_lpCmdLine));
    if (CString(m_lpCmdLine).Find(L"/SelfTest") >= 0) {
        ::ExitProcess(static_cast<UINT>(RunSelfTest(bridgeOptions)));
    }

    CMainDialog dialog(bridgeOptions);
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
