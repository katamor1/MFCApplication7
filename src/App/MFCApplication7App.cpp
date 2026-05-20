#include "MFCApplication7App.h"

#include "DataGateway.h"
#include "MainDialog.h"
#include "MockBackendBridge.h"
#include "ScreenModels.h"

#include <afxcmn.h>

CMFCApplication7App theApp;

namespace {

int RunSelfTest()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    if (gateway.Connect(L"127.0.0.1") != BridgeError::Ok) {
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

    if (CString(m_lpCmdLine).Find(L"/SelfTest") >= 0) {
        ::ExitProcess(static_cast<UINT>(RunSelfTest()));
    }

    CMainDialog dialog;
    m_pMainWnd = &dialog;
    dialog.DoModal();
    return FALSE;
}
