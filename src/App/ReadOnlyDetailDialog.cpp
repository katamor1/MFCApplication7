#include "ReadOnlyDetailDialog.h"

#include <utility>

namespace {

/**
 * @brief Convert a wide string to CString.
 */
CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

CRect DluRect(CWnd* window, int left, int top, int right, int bottom)
{
    CRect rect(left, top, right, bottom);
    ::MapDialogRect(window->GetSafeHwnd(), &rect);
    return rect;
}

} // namespace

/**
 * @file ReadOnlyDetailDialog.cpp
 * @brief Implements read-only diagnostic detail display.
 */

/**
 * @brief Construct dialog with an immutable detail model.
 * @param model Detail rows to display.
 * @param parent Optional parent window.
 */
CReadOnlyDetailDialog::CReadOnlyDetailDialog(ReadOnlyDetailModel model, CWnd* parent)
    : CDialogEx(IDD_READ_ONLY_DETAIL_DIALOG, parent)
    , model_(std::move(model))
{
}

/**
 * @brief Initialize controls and render all detail rows.
 */
BOOL CReadOnlyDetailDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(ToCString(model_.title));

    detailList_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                       DluRect(this, 8, 8, 312, 140),
                       this,
                       IDC_READ_ONLY_DETAIL_LIST);
    detailList_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    detailList_.InsertColumn(0, L"項目", LVCFMT_LEFT, 120);
    detailList_.InsertColumn(1, L"値", LVCFMT_LEFT, 300);

    for (int row = 0; row < static_cast<int>(model_.rows.size()); ++row) {
        detailList_.InsertItem(row, ToCString(model_.rows[static_cast<size_t>(row)].label));
        detailList_.SetItemText(row, 1, ToCString(model_.rows[static_cast<size_t>(row)].value));
    }

    okButton_.Create(L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, DluRect(this, 260, 154, 312, 172), this, IDOK);
    return TRUE;
}
