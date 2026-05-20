#include "GridModel.h"

#include <utility>

/**
 * @file GridModel.cpp
 * @brief Provides immutable-ish builders for simple tabular model data used by the list control.
 */

/**
 * @brief Create a text cell value with explicit kind.
 * @param value Cell text.
 * @param kind Kind for rendering/editability.
 * @return Constructed GridCell.
 */
GridCell GridCell::Text(std::wstring value, CellKind kind)
{
    return {std::move(value), kind};
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
