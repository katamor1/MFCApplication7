#pragma once

#include "resource.h"

#include <afxdialogex.h>
#include <afxwin.h>

#include <string>

class COrderEditDialog final : public CDialogEx
{
public:
    COrderEditDialog(int containerNo, std::wstring itemName, std::wstring currentOrder, CWnd* parent);

    std::wstring OrderText() const;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    int containerNo_{};
    std::wstring itemName_;
    std::wstring currentOrder_;
    std::wstring orderText_;
    CStatic contextText_;
    CStatic orderLabel_;
    CEdit orderEdit_;
    CButton okButton_;
    CButton cancelButton_;
};
