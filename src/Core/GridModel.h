#pragma once

#include <string>
#include <vector>

enum class CellKind
{
    ReadOnlyText,
    Text,
    Spin,
    ComboBox,
    RadioButton,
    CheckBox,
};

struct GridCell
{
    std::wstring text;
    CellKind kind{CellKind::ReadOnlyText};

    static GridCell Text(std::wstring value, CellKind kind = CellKind::ReadOnlyText);
};

struct GridRowBinding
{
    int containerNo{};
    int itemNo{};
};

struct GridRow
{
    std::vector<GridCell> cells;
    GridRowBinding binding;
};

class GridModel
{
public:
    void SetColumns(std::vector<std::wstring> columns);
    void AddRow(std::vector<GridCell> cells);
    void AddRow(std::vector<GridCell> cells, GridRowBinding binding);
    void ClearRows();

    size_t ColumnCount() const noexcept;
    size_t RowCount() const noexcept;
    const std::vector<std::wstring>& Columns() const noexcept;
    const std::vector<GridRow>& Rows() const noexcept;

private:
    std::vector<std::wstring> columns_;
    std::vector<GridRow> rows_;
};
