#pragma once

#include <string>
#include <vector>

/**
 * @brief グリッド1セルの種類。
 */
enum class CellKind
{
    ReadOnlyText,
    Text,
    Spin,
    ComboBox,
    RadioButton,
    CheckBox,
};

/**
 * @brief グリッドセル値を表す。表示文字列と種類を持つ。
 */
struct GridCell
{
    std::wstring text;
    CellKind kind{CellKind::ReadOnlyText};

    /**
     * @brief 文字列と種類を指定してセルを構築する。
     */
    static GridCell Text(std::wstring value, CellKind kind = CellKind::ReadOnlyText);
};

/**
 * @brief グリッド行が参照している対象行情報。
 */
struct GridRowBinding
{
    int containerNo{};
    int itemNo{};
};

/**
 * @brief 表示用グリッド行。セル列と行バインドを持つ。
 */
struct GridRow
{
    std::vector<GridCell> cells;
    GridRowBinding binding;
};

/**
 * @brief 画面が扱う汎用グリッドデータ。
 */
class GridModel
{
public:
    /**
     * @brief 列見出しを置き換える。
     */
    void SetColumns(std::vector<std::wstring> columns);
    /** 最終列にバインドなしで行を追加。 */
    void AddRow(std::vector<GridCell> cells);
    /** セル + バインド付きで行を追加。 */
    void AddRow(std::vector<GridCell> cells, GridRowBinding binding);
    /** 全行を破棄する。 */
    void ClearRows();

    /** 列数を返す。 */
    size_t ColumnCount() const noexcept;
    /** 行数を返す。 */
    size_t RowCount() const noexcept;
    /** 列見出しを取得する。 */
    const std::vector<std::wstring>& Columns() const noexcept;
    /** 行データを取得する。 */
    const std::vector<GridRow>& Rows() const noexcept;

private:
    std::vector<std::wstring> columns_;
    std::vector<GridRow> rows_;
};
