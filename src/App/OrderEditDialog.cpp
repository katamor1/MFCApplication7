#include "OrderEditDialog.h"

#include <cwctype>
#include <utility>

namespace {

/**
 * @brief Convert wide string to CString.
 */
CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

bool IsValidOrder(const std::wstring& value)
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
        const auto number = std::stoi(value);
        return number >= 1 && number <= 9999;
    } catch (...) {
        return false;
    }
}

CRect DluRect(CWnd* window, int left, int top, int right, int bottom)
{
    CRect rect(left, top, right, bottom);
    ::MapDialogRect(window->GetSafeHwnd(), &rect);
    return rect;
}

} // namespace

/**
 * @file OrderEditDialog.cpp
 * @brief Implements order edit modal interaction and input validation.
 */

/**
 * @brief Construct dialog with editing context and defaults.
 * @param containerNo Container identifier.
 * @param itemName Item label for confirmation.
 * @param currentOrder Current outbound order value.
 * @param parent Optional parent window.
 */
COrderEditDialog::COrderEditDialog(int containerNo, std::wstring itemName, std::wstring currentOrder, CWnd* parent)
    : CDialogEx(IDD_ORDER_EDIT_DIALOG, parent)
    , containerNo_(containerNo)
    , itemName_(std::move(itemName))
    , currentOrder_(std::move(currentOrder))
    , orderText_(currentOrder_)
{
}

/**
 * @brief Return currently selected order text for caller.
 */
std::wstring COrderEditDialog::OrderText() const
{
    return orderText_;
}

/**
 * @brief Initialize dialog controls and pre-fill current value.
 */
BOOL COrderEditDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    contextText_.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 8, 252, 28), this, IDC_ORDER_CONTEXT);
    orderLabel_.Create(L"出庫順序", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 42, 70, 56), this);
    orderEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, DluRect(this, 78, 40, 140, 56), this, IDC_ORDER_EDIT);
    okButton_.Create(L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, DluRect(this, 140, 84, 190, 102), this, IDOK);
    cancelButton_.Create(L"キャンセル", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, DluRect(this, 198, 84, 252, 102), this, IDCANCEL);

    CString context;
    context.Format(L"コンテナ %d / %s", containerNo_, itemName_.c_str());
    SetDlgItemText(IDC_ORDER_CONTEXT, context);
    SetDlgItemText(IDC_ORDER_EDIT, ToCString(currentOrder_));
    orderEdit_.SetFocus();
    orderEdit_.SetSel(0, -1);
    return FALSE;
}

/**
 * @brief Validate input before accepting and close dialog.
 */
void COrderEditDialog::OnOK()
{
    CString value;
    GetDlgItemText(IDC_ORDER_EDIT, value);
    const std::wstring text(value.GetString());
    if (!IsValidOrder(text)) {
        AfxMessageBox(L"出庫順序は1から9999の整数で入力してください。", MB_OK | MB_ICONWARNING);
        return;
    }

    orderText_ = text;
    CDialogEx::OnOK();
}
