#include "GridModel.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace {

/**
 * @brief Return true when the string is a non-empty signed integer literal.
 */
bool IsIntegerLiteral(const std::wstring& value)
{
    if (value.empty()) {
        return false;
    }

    size_t index = 0;
    if (value[0] == L'+' || value[0] == L'-') {
        if (value.size() == 1) {
            return false;
        }
        index = 1;
    }

    for (; index < value.size(); ++index) {
        if (!std::iswdigit(value[index])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Return true when the candidate is present in the cell option list.
 */
bool HasOption(const GridCell& cell, const std::wstring& value)
{
    return std::find(cell.options.begin(), cell.options.end(), value) != cell.options.end();
}

} // namespace

/**
 * @file GridModel.cpp
 * @brief Provides immutable-ish builders for simple tabular model data used by the list control.
 */

/**
 * @brief Create a text cell value with explicit kind.
 * @param value Cell text.
 * @param kind Kind for rendering/editability.
 * @param options Optional candidate values for combo/radio cells.
 * @return Constructed GridCell.
 */
GridCell GridCell::Text(std::wstring value, CellKind kind, std::vector<std::wstring> options)
{
    return {std::move(value), kind, std::move(options)};
}

/**
 * @brief Return whether a cell kind is editable by the custom grid.
 */
bool IsEditableCellKind(CellKind kind) noexcept
{
    return kind != CellKind::ReadOnlyText;
}

/**
 * @brief Validate one edit value according to the cell kind.
 */
GridEditValidationResult ValidateGridEditValue(const GridCell& cell, const std::wstring& value)
{
    switch (cell.kind) {
    case CellKind::ReadOnlyText:
        return {false, L"read-only cell"};
    case CellKind::Text:
        return {true, {}};
    case CellKind::Spin:
        return {IsIntegerLiteral(value), IsIntegerLiteral(value) ? std::wstring{} : L"integer required"};
    case CellKind::ComboBox:
        return {HasOption(cell, value), HasOption(cell, value) ? std::wstring{} : L"option required"};
    case CellKind::RadioButton:
        return {HasOption(cell, value), HasOption(cell, value) ? std::wstring{} : L"option required"};
    case CellKind::CheckBox:
        return {value == L"true" || value == L"false", (value == L"true" || value == L"false") ? std::wstring{} : L"true/false required"};
    }
    return {false, L"unknown cell kind"};
}

/**
 * @brief Replace all column headers.
 * @param columns Column captions to be bound to the model.
 */
void GridModel::SetColumns(std::vector<std::wstring> columns)
{
    columns_ = std::move(columns);
}

/**
 * @brief Add a row without binding metadata.
 * @param cells Cell values for the row.
 */
void GridModel::AddRow(std::vector<GridCell> cells)
{
    AddRow(std::move(cells), {});
}

/**
 * @brief Add a row with optional external binding payload.
 * @param cells Row cell values.
 * @param binding Additional selection/index binding metadata.
 */
void GridModel::AddRow(std::vector<GridCell> cells, GridRowBinding binding)
{
    rows_.push_back({std::move(cells), binding});
}

/**
 * @brief Remove all rows while keeping current column definitions.
 */
void GridModel::ClearRows()
{
    rows_.clear();
}

/**
 * @brief Update one cell text when an in-place edit is committed.
 */
bool GridModel::SetCellText(int row, int column, std::wstring value)
{
    if (row < 0 || column < 0 || row >= static_cast<int>(rows_.size())) {
        return false;
    }
    auto& cells = rows_[row].cells;
    if (column >= static_cast<int>(cells.size())) {
        return false;
    }
    cells[column].text = std::move(value);
    return true;
}

/**
 * @brief Return number of columns.
 */
size_t GridModel::ColumnCount() const noexcept
{
    return columns_.size();
}

/**
 * @brief Return number of rows.
 */
size_t GridModel::RowCount() const noexcept
{
    return rows_.size();
}

/**
 * @brief Return view of current column list.
 */
const std::vector<std::wstring>& GridModel::Columns() const noexcept
{
    return columns_;
}

/**
 * @brief Return view of current row list.
 */
const std::vector<GridRow>& GridModel::Rows() const noexcept
{
    return rows_;
}
