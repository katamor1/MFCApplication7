#pragma once

#include "ScreenModels.h"
#include "resource.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>

/**
 * @brief 読み取り専用のキー/値詳細ダイアログ。
 */
class CReadOnlyDetailDialog final : public CDialogEx
{
public:
    /**
     * @brief 表示モデルを受け取ってダイアログを初期化する。
     */
    CReadOnlyDetailDialog(ReadOnlyDetailModel model, CWnd* parent);

protected:
    /**
     * @brief 詳細行リストとOKボタンを生成する。
     */
    BOOL OnInitDialog() override;

private:
    ReadOnlyDetailModel model_;
    CListCtrl detailList_;
    CButton okButton_;
};
