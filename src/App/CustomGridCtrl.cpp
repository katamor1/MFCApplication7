#include "CustomGridCtrl.h"

namespace {

/**
 * @brief Convert wide string to an MFC CString.
 */
CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

} // namespace

/**
 * @file CustomGridCtrl.cpp
 * @brief Implements grid control rendering and cell/binding query helpers.
 */

/**
 * @brief Apply a full model snapshot into the list control view.
 * @param model Display model to render.
 */
void CCustomGridCtrl::ApplyModel(const GridModel& model)
{
    model_ = model;

    DeleteAllItems();
    while (DeleteColumn(0)) {
    }

    for (int column = 0; column < static_cast<int>(model_.Columns().size()); ++column) {
        InsertColumn(column, ToCString(model_.Columns()[column]), LVCFMT_LEFT, 180);
    }

    for (int row = 0; row < static_cast<int>(model_.Rows().size()); ++row) {
        const auto& cells = model_.Rows()[row].cells;
        if (cells.empty()) {
            continue;
        }
        InsertItem(row, ToCString(cells[0].text));
        for (int column = 1; column < static_cast<int>(cells.size()); ++column) {
            SetItemText(row, column, ToCString(cells[column].text));
        }
    }
}

/**
 * @brief Return editable kind for one cell coordinate.
 * @param row Row index.
 * @param column Column index.
 * @return Cell kind for rendering and edit policy.
 */
CellKind CCustomGridCtrl::CellKindAt(int row, int column) const
{
    if (row < 0 || column < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return CellKind::ReadOnlyText;
    }
    const auto& cells = model_.Rows()[row].cells;
    if (column >= static_cast<int>(cells.size())) {
        return CellKind::ReadOnlyText;
    }
    return cells[column].kind;
}

/**
 * @brief Return row binding for command handlers.
 * @param row Row index.
 * @return Bound payload (container/item mapping) or default values if out of range.
 */
GridRowBinding CCustomGridCtrl::RowBindingAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return {};
    }
    return model_.Rows()[row].binding;
}
