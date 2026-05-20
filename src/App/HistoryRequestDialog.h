#pragma once

#include "UpdateScheduler.h"
#include "resource.h"

#include <afxdialogex.h>
#include <afxwin.h>

/**
 * @file HistoryRequestDialog.h
 * @brief Dialog declaration for selecting historical record retrieval range.
 */

class CHistoryRequestDialog final : public CDialogEx
{
public:
    /**
     * @brief 履歴日数の入力ダイアログを初期化する。
     */
    explicit CHistoryRequestDialog(CWnd* parent);

    /**
     * @brief 確定済みの履歴要求を返す。
     */
    HistoryRequest Request() const noexcept;

protected:
    /**
     * @brief デフォルト日数と説明文を構築する。
     */
    BOOL OnInitDialog() override;
    /**
     * @brief 1~365 の日数入力を検証して確定する。
     */
    void OnOK() override;

private:
    HistoryRequest request_{7};
    CStatic contextText_;
    CStatic daysLabel_;
    CEdit daysEdit_;
    CButton okButton_;
    CButton cancelButton_;
};
