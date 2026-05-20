#include "HistoryRequestDialog.h"

#include <cwctype>
#include <string>

/**
 * @file HistoryRequestDialog.cpp
 * @brief Implements modal dialog for history request input and validation.
 */

namespace {

/**
 * @brief Parse and validate user-entered history day count.
 * @param value Raw text.
 * @param days Output numeric value.
 * @return true on valid [1,365], false otherwise.
 */
bool TryParseDays(const std::wstring& value, int& days)
{
    if (value.empty()) {
        return false;
    }
    for (const auto ch : value) {
        if (!std::iswdigit(ch)) {
            return false;
        }
    }

    try {
        days = std::stoi(value);
        return IsValidHistoryRequest({days});
    } catch (...) {
        return false;
    }
}

} // namespace

/**
 * @brief Construct dialog.
 */
CHistoryRequestDialog::CHistoryRequestDialog(CWnd* parent)
    : CDialogEx(IDD_HISTORY_REQUEST_DIALOG, parent)
{
}

/**
 * @brief Return requested history range.
 */
HistoryRequest CHistoryRequestDialog::Request() const noexcept
{
    return request_;
}

/**
 * @brief Initialize the default text and control state.
 */
BOOL CHistoryRequestDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    contextText_.Create(L"出庫履歴の取得期間を指定してください。", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(12, 12, 380, 48), this, IDC_HISTORY_CONTEXT);
    daysLabel_.Create(L"過去日数", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(12, 62, 100, 84), this);
    daysEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(116, 58, 210, 82), this, IDC_HISTORY_DAYS_EDIT);
    okButton_.Create(L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, CRect(196, 112, 286, 142), this, IDOK);
    cancelButton_.Create(L"キャンセル", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(300, 112, 420, 142), this, IDCANCEL);

    SetDlgItemText(IDC_HISTORY_DAYS_EDIT, L"7");
    daysEdit_.SetFocus();
    daysEdit_.SetSel(0, -1);
    return FALSE;
}

/**
 * @brief Validate requested day count and accept only valid values.
 */
void CHistoryRequestDialog::OnOK()
{
    CString value;
    GetDlgItemText(IDC_HISTORY_DAYS_EDIT, value);

    int days = 0;
    if (!TryParseDays(value.GetString(), days)) {
        AfxMessageBox(L"履歴日数は1から365の整数で入力してください。", MB_OK | MB_ICONWARNING);
        return;
    }

    request_.days = days;
    CDialogEx::OnOK();
}
