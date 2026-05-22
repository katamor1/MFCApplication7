#pragma once

#include "ScreenModels.h"
#include "UpdateScheduler.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Schedule 操作を取り消すための Write 群。
 */
struct ScheduleUndoEntry
{
    /** 画面表示と診断用の操作名。 */
    std::wstring label;
    /** Undo 実行時に投入する Write 群。 */
    std::vector<ScheduleCellWrite> writes;
};

/**
 * @brief Schedule 同順序検出結果。
 */
struct ScheduleDuplicateOrder
{
    /** 重複行が見つかったか。 */
    bool found{false};
    /** 見つかった行のバインド。 */
    GridRowBinding binding;
    /** 見つかった表示行番号。 */
    int row{-1};
};

/**
 * @brief Schedule 画面用の LIFO Undo 履歴。
 */
class ScheduleUndoStack
{
public:
    /** @brief 最大履歴数を指定して構築する。 */
    explicit ScheduleUndoStack(size_t maxSize = 20);

    /** @brief Undo 項目を末尾に積む。上限超過時は最古を捨てる。 */
    void Push(ScheduleUndoEntry entry);
    /** @brief 取り消し可能な履歴があるかを返す。 */
    bool CanUndo() const noexcept;
    /** @brief 現在の履歴数を返す。 */
    size_t Size() const noexcept;
    /** @brief 最新の Undo 項目を取り出す。 */
    std::optional<ScheduleUndoEntry> Pop();
    /** @brief 失敗した Undo 項目を先頭候補として戻す。 */
    void Restore(ScheduleUndoEntry entry);

private:
    size_t maxSize_{20};
    std::vector<ScheduleUndoEntry> entries_;
};

/**
 * @brief 現在の表示順を 10,20,30... に再採番する Write 群を作る。
 */
std::vector<ScheduleCellWrite> BuildScheduleRenumberWrites(const GridModel& grid,
                                                           int firstOrder = 10,
                                                           int step = 10);
/**
 * @brief 指定表示行の現在順序を復元する Undo Write 群を作る。
 */
std::vector<ScheduleCellWrite> CaptureScheduleOrderRestoreWrites(const GridModel& grid,
                                                                 const std::vector<int>& rowIndices);
/**
 * @brief Schedule セルの現在値を復元する Undo Write を作る。
 */
std::vector<ScheduleCellWrite> BuildScheduleCellRestoreWrites(GridRowBinding binding,
                                                              int column,
                                                              const std::wstring& value);
/**
 * @brief 追加操作を取り消す削除 Write を作る。
 */
std::vector<ScheduleCellWrite> BuildScheduleAddUndoWrites(const ScheduleAddRequest& request);
/**
 * @brief 削除操作を取り消す追加/復元 Write 群を作る。
 */
std::vector<ScheduleCellWrite> BuildScheduleDeleteUndoWrites(const GridRow& row);
/**
 * @brief 同じ出庫順序を持つ別行を検出する。
 */
ScheduleDuplicateOrder FindDuplicateScheduleOrder(const GridModel& grid,
                                                  int order,
                                                  GridRowBinding excludedBinding = {});
/**
 * @brief 指定バインドを持つ Schedule 行が存在するかを返す。
 */
bool HasScheduleRowBinding(const GridModel& grid, GridRowBinding binding);
