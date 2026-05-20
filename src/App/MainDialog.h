#pragma once

#include "DataCatalog.h"
#include "DataGateway.h"
#include "FunctionBarModel.h"
#include "CustomGridCtrl.h"
#include "BridgeFactory.h"
#include "ScreenModels.h"
#include "UpdateScheduler.h"
#include "resource.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>

#include <array>
#include <memory>

enum class MainScreenId
{
    Station = 0,
    ContainerList = 1,
    Schedule = 2,
    System = 3,
    Maintenance = 4,
};

class CMainDialog final : public CDialogEx
{
public:
    explicit CMainDialog(BridgeFactoryOptions options);
    ~CMainDialog() override;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;

    afx_msg void OnTimer(UINT_PTR eventId);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnNavCommand(UINT id);
    afx_msg void OnFunctionCommand(UINT id);
    afx_msg void OnNavExpand();
    afx_msg void OnListItemChanged(NMHDR* notify, LRESULT* result);

    DECLARE_MESSAGE_MAP()

private:
    void CreateControls();
    void LayoutControls();
    void ConnectAndStart();
    void RefreshUi(bool forceGrid);
    void RefreshStatus(const UpdateSnapshot& snapshot);
    void RefreshFunctions(const UpdateSnapshot& snapshot);
    void SwitchScreen(MainScreenId screen);
    void PopulateCurrentScreen(const UpdateSnapshot& snapshot);
    void PopulateGrid(const GridModel& grid);
    void PopulateStation(const StationSnapshot& snapshot);
    CString ScreenTitle() const;

    DataCatalog catalog_;
    BridgeFactoryOptions bridgeOptions_;
    std::shared_ptr<IBackendBridge> bridge_;
    std::unique_ptr<UpdateCoordinator> coordinator_;

    CStatic statusText_;
    CProgressCtrl historyProgress_;
    CStatic detailText_;
    CCustomGridCtrl contentList_;
    CButton expandButton_;
    std::array<CButton, 5> navButtons_;
    std::array<CButton, 8> functionButtons_;

    MainScreenId currentScreen_{MainScreenId::Station};
    bool navExpanded_{false};
    int selectedContainerNo_{1};
    unsigned int timerTicks_{0};
};
