#pragma once

#include "UpdateScheduler.h"
#include "resource.h"

#include <afxdialogex.h>
#include <afxwin.h>

/**
 * @brief 出庫予定追加ダイアログ。
 */
class CScheduleAddDialog final : public CDialogEx
{
public:
    /**
     * @brief 既定値を持つ追加ダイアログを初期化する。
     */
    explicit CScheduleAddDialog(CWnd* parent);

    /**
     * @brief OK確定済みの追加要求を返す。
     */
    ScheduleAddRequest Request() const;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    ScheduleAddRequest request_{1, 3, 1, L"NEW-ITEM"};
    CStatic contextText_;
    CStatic containerLabel_;
    CStatic itemLabel_;
    CStatic orderLabel_;
    CStatic itemNameLabel_;
    CEdit containerEdit_;
    CEdit itemEdit_;
    CEdit orderEdit_;
    CEdit itemNameEdit_;
    CButton okButton_;
    CButton cancelButton_;
};
