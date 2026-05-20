#include "ScheduleDeleteConfirmDialog.h"

#include <utility>

namespace {

CRect DluRect(CWnd* window, int left, int top, int right, int bottom)
{
    CRect rect(left, top, right, bottom);
    ::MapDialogRect(window->GetSafeHwnd(), &rect);
    return rect;
}

} // namespace

/**
 * @file ScheduleDeleteConfirmDialog.cpp
 * @brief Implements schedule deletion confirmation dialog.
 */

CScheduleDeleteConfirmDialog::CScheduleDeleteConfirmDialog(int containerNo, int itemNo, std::wstring itemName, std::wstring order, CWnd* parent)
    : CDialogEx(IDD_SCHEDULE_DELETE_CONFIRM_DIALOG, parent)
    , containerNo_(containerNo)
    , itemNo_(itemNo)
    , itemName_(std::move(itemName))
    , order_(std::move(order))
{
}

BOOL CScheduleDeleteConfirmDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    contextText_.Create(L"選択した出庫予定を削除します。", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 8, 292, 24), this, IDC_SCHEDULE_DELETE_CONTEXT);

    CString detail;
    detail.Format(L"コンテナ %d / 品目 %d\r\n%s\r\n出庫順序: %s", containerNo_, itemNo_, itemName_.c_str(), order_.c_str());
    detailText_.Create(detail, WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 34, 292, 84), this, IDC_SCHEDULE_DELETE_DETAIL);
    okButton_.Create(L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, DluRect(this, 178, 96, 224, 114), this, IDOK);
    cancelButton_.Create(L"キャンセル", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, DluRect(this, 232, 96, 292, 114), this, IDCANCEL);
    return TRUE;
}
