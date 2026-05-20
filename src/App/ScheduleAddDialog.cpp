#include "ScheduleAddDialog.h"

#include <cwctype>
#include <string>

namespace {

CRect DluRect(CWnd* window, int left, int top, int right, int bottom)
{
    CRect rect(left, top, right, bottom);
    ::MapDialogRect(window->GetSafeHwnd(), &rect);
    return rect;
}

bool TryParseInt(const CString& value, int& number)
{
    const std::wstring text(value.GetString());
    if (text.empty()) {
        return false;
    }
    for (const auto ch : text) {
        if (!std::iswdigit(ch)) {
            return false;
        }
    }
    try {
        number = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

/**
 * @file ScheduleAddDialog.cpp
 * @brief Implements modal input for provisional schedule add writes.
 */

CScheduleAddDialog::CScheduleAddDialog(CWnd* parent)
    : CDialogEx(IDD_SCHEDULE_ADD_DIALOG, parent)
{
}

ScheduleAddRequest CScheduleAddDialog::Request() const
{
    return request_;
}

BOOL CScheduleAddDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    contextText_.Create(L"出庫予定を追加します。", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 8, 292, 24), this, IDC_SCHEDULE_ADD_CONTEXT);
    containerLabel_.Create(L"コンテナ番号", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 34, 86, 48), this);
    containerEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, DluRect(this, 96, 32, 160, 48), this, IDC_SCHEDULE_ADD_CONTAINER_EDIT);
    itemLabel_.Create(L"品目番号", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 58, 86, 72), this);
    itemEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, DluRect(this, 96, 56, 160, 72), this, IDC_SCHEDULE_ADD_ITEM_EDIT);
    orderLabel_.Create(L"出庫順序", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 82, 86, 96), this);
    orderEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, DluRect(this, 96, 80, 160, 96), this, IDC_SCHEDULE_ADD_ORDER_EDIT);
    itemNameLabel_.Create(L"品目名", WS_CHILD | WS_VISIBLE | SS_LEFT, DluRect(this, 8, 106, 86, 120), this);
    itemNameEdit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, DluRect(this, 96, 104, 292, 120), this, IDC_SCHEDULE_ADD_ITEM_NAME_EDIT);
    okButton_.Create(L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, DluRect(this, 178, 136, 224, 154), this, IDOK);
    cancelButton_.Create(L"キャンセル", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, DluRect(this, 232, 136, 292, 154), this, IDCANCEL);

    SetDlgItemInt(IDC_SCHEDULE_ADD_CONTAINER_EDIT, request_.containerNo, FALSE);
    SetDlgItemInt(IDC_SCHEDULE_ADD_ITEM_EDIT, request_.itemNo, FALSE);
    SetDlgItemInt(IDC_SCHEDULE_ADD_ORDER_EDIT, request_.order, FALSE);
    SetDlgItemText(IDC_SCHEDULE_ADD_ITEM_NAME_EDIT, request_.itemName.c_str());
    itemNameEdit_.LimitText(40);
    containerEdit_.SetFocus();
    containerEdit_.SetSel(0, -1);
    return FALSE;
}

void CScheduleAddDialog::OnOK()
{
    CString containerText;
    CString itemText;
    CString orderText;
    CString itemNameText;
    GetDlgItemText(IDC_SCHEDULE_ADD_CONTAINER_EDIT, containerText);
    GetDlgItemText(IDC_SCHEDULE_ADD_ITEM_EDIT, itemText);
    GetDlgItemText(IDC_SCHEDULE_ADD_ORDER_EDIT, orderText);
    GetDlgItemText(IDC_SCHEDULE_ADD_ITEM_NAME_EDIT, itemNameText);

    ScheduleAddRequest next;
    if (!TryParseInt(containerText, next.containerNo) ||
        !TryParseInt(itemText, next.itemNo) ||
        !TryParseInt(orderText, next.order)) {
        AfxMessageBox(L"コンテナ番号、品目番号、出庫順序は整数で入力してください。", MB_OK | MB_ICONWARNING);
        return;
    }
    next.itemName = itemNameText.GetString();
    if (!IsValidScheduleAddRequest(next)) {
        AfxMessageBox(L"コンテナ番号は1から100、品目番号は1から1000、出庫順序は1から9999、品目名は1から40文字で入力してください。", MB_OK | MB_ICONWARNING);
        return;
    }

    request_ = next;
    CDialogEx::OnOK();
}
