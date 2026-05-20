#pragma once

#include "resource.h"

#include <afxdialogex.h>
#include <afxwin.h>

#include <string>

/**
 * @brief 出庫予定削除確認ダイアログ。
 */
class CScheduleDeleteConfirmDialog final : public CDialogEx
{
public:
    /**
     * @brief 選択行情報を表示する確認ダイアログを初期化する。
     */
    CScheduleDeleteConfirmDialog(int containerNo, int itemNo, std::wstring itemName, std::wstring order, CWnd* parent);

protected:
    BOOL OnInitDialog() override;

private:
    int containerNo_{};
    int itemNo_{};
    std::wstring itemName_;
    std::wstring order_;
    CStatic contextText_;
    CStatic detailText_;
    CButton okButton_;
    CButton cancelButton_;
};
