#pragma once

#include "GridModel.h"

#include <afxcmn.h>

class CCustomGridCtrl final : public CListCtrl
{
public:
    void ApplyModel(const GridModel& model);
    CellKind CellKindAt(int row, int column) const;
    GridRowBinding RowBindingAt(int row) const;

private:
    GridModel model_;
};
