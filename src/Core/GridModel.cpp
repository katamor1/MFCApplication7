#include "GridModel.h"

#include <utility>

GridCell GridCell::Text(std::wstring value, CellKind kind)
{
    return {std::move(value), kind};
}

void GridModel::SetColumns(std::vector<std::wstring> columns)
{
    columns_ = std::move(columns);
}

void GridModel::AddRow(std::vector<GridCell> cells)
{
    AddRow(std::move(cells), {});
}

void GridModel::AddRow(std::vector<GridCell> cells, GridRowBinding binding)
{
    rows_.push_back({std::move(cells), binding});
}

void GridModel::ClearRows()
{
    rows_.clear();
}

size_t GridModel::ColumnCount() const noexcept
{
    return columns_.size();
}

size_t GridModel::RowCount() const noexcept
{
    return rows_.size();
}

const std::vector<std::wstring>& GridModel::Columns() const noexcept
{
    return columns_;
}

const std::vector<GridRow>& GridModel::Rows() const noexcept
{
    return rows_;
}
