#pragma once

#include "resource.h"

#include <afxdialogex.h>
#include <afxwin.h>

#include <string>

/**
 * @brief 出庫順序編集ダイアログ。
 */
class COrderEditDialog final : public CDialogEx
{
public:
    /**
     * @brief コンテナ番号と品目名を見せた状態で初期化する。
     */
    COrderEditDialog(int containerNo, std::wstring itemName, std::wstring currentOrder, CWnd* parent);

    /**
     * @brief 入力された順序文字列を取得する。
     */
    std::wstring OrderText() const;

protected:
    /**
     * @brief コンテキストラベルと初期値を初期描画する。
     */
    BOOL OnInitDialog() override;
    /**
     * @brief 入力検証（1~9999）を通した上で確定する。
     */
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
