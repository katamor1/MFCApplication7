#pragma once

#include "GridModel.h"

#include <afxcmn.h>

/**
 * @brief GridModel を表示するための CListCtrl ラッパ。
 */
class CCustomGridCtrl final : public CListCtrl
{
public:
    /**
     * @brief 受け取ったモデルを画面に反映する。
     */
    void ApplyModel(const GridModel& model);
    /**
     * @brief 指定セルの種別を返す（編集可否判定に使う）。
     */
    CellKind CellKindAt(int row, int column) const;
    /**
     * @brief 指定行のバインド情報を返す。
     */
    GridRowBinding RowBindingAt(int row) const;

private:
    GridModel model_;
};
