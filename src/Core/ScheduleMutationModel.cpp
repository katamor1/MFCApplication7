#include "ScheduleMutationModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

/**
 * @file ScheduleMutationModel.cpp
 * @brief Pure schedule mutation helpers for renumbering, duplicate checks, and Undo writes.
 */

namespace {

constexpr int kMaxScheduleOrder = 9999;

bool IsValidScheduleBinding(GridRowBinding binding) noexcept
{
    return binding.containerNo > 0 && binding.itemNo > 0;
}

bool SameScheduleBinding(GridRowBinding left, GridRowBinding right) noexcept
{
    return left.containerNo == right.containerNo && left.itemNo == right.itemNo;
}

bool TryParsePositiveInt(const std::wstring& value, int& parsed)
{
    if (value.empty()) {
        return false;
    }

    int result = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        result = (result * 10) + (ch - L'0');
        if (result > kMaxScheduleOrder) {
            return false;
        }
    }

    if (result <= 0) {
        return false;
    }
    parsed = result;
    return true;
}

ScheduleCellWrite MakeRawWrite(int dataId, GridRowBinding binding, std::wstring value)
{
    return {{dataId, binding.containerNo, binding.itemNo, DataStyle::Raw}, std::move(value)};
}

} // namespace

ScheduleUndoStack::ScheduleUndoStack(size_t maxSize)
    : maxSize_(std::max<size_t>(1, maxSize))
{
}

void ScheduleUndoStack::Push(ScheduleUndoEntry entry)
{
    if (entry.writes.empty()) {
        return;
    }

    entries_.push_back(std::move(entry));
    if (entries_.size() > maxSize_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxSize_));
    }
}

bool ScheduleUndoStack::CanUndo() const noexcept
{
    return !entries_.empty();
}

size_t ScheduleUndoStack::Size() const noexcept
{
    return entries_.size();
}

std::optional<ScheduleUndoEntry> ScheduleUndoStack::Pop()
{
    if (entries_.empty()) {
        return std::nullopt;
    }

    auto entry = std::move(entries_.back());
    entries_.pop_back();
    return entry;
}

void ScheduleUndoStack::Restore(ScheduleUndoEntry entry)
{
    Push(std::move(entry));
}

std::vector<ScheduleCellWrite> BuildScheduleRenumberWrites(const GridModel& grid,
                                                           int firstOrder,
                                                           int step)
{
    if (firstOrder <= 0 || step <= 0) {
        return {};
    }

    std::vector<ScheduleCellWrite> writes;
    int sequence = 0;
    for (const auto& row : grid.Rows()) {
        if (!IsValidScheduleBinding(row.binding) || row.cells.size() <= ScheduleGridColumn::Order) {
            continue;
        }

        const int expectedOrder = firstOrder + (sequence * step);
        ++sequence;
        if (expectedOrder > kMaxScheduleOrder) {
            break;
        }

        int currentOrder = 0;
        if (TryParsePositiveInt(row.cells[ScheduleGridColumn::Order].text, currentOrder) &&
            currentOrder == expectedOrder) {
            continue;
        }

        writes.push_back(MakeRawWrite(2103, row.binding, std::to_wstring(expectedOrder)));
    }
    return writes;
}

std::vector<ScheduleCellWrite> CaptureScheduleOrderRestoreWrites(const GridModel& grid,
                                                                 const std::vector<int>& rowIndices)
{
    std::vector<ScheduleCellWrite> writes;
    for (const int rowIndex : rowIndices) {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(grid.Rows().size())) {
            continue;
        }

        const auto& row = grid.Rows()[static_cast<size_t>(rowIndex)];
        if (!IsValidScheduleBinding(row.binding) || row.cells.size() <= ScheduleGridColumn::Order) {
            continue;
        }

        const auto& value = row.cells[ScheduleGridColumn::Order].text;
        if (value.empty()) {
            continue;
        }

        writes.push_back(MakeRawWrite(2103, row.binding, value));
    }
    return writes;
}

std::vector<ScheduleCellWrite> BuildScheduleCellRestoreWrites(GridRowBinding binding,
                                                              int column,
                                                              const std::wstring& value)
{
    if (!IsValidScheduleBinding(binding)) {
        return {};
    }

    int dataId = 0;
    if (column == ScheduleGridColumn::ItemName) {
        dataId = 2100;
    } else if (column == ScheduleGridColumn::OutboundStart) {
        dataId = 2102;
    } else if (column == ScheduleGridColumn::OutboundEnd) {
        dataId = 3000;
    } else if (column == ScheduleGridColumn::Order) {
        int order = 0;
        if (!TryParsePositiveInt(value, order)) {
            return {};
        }
        dataId = 2103;
    } else {
        return {};
    }

    return {MakeRawWrite(dataId, binding, value)};
}

std::vector<ScheduleCellWrite> BuildScheduleAddUndoWrites(const ScheduleAddRequest& request)
{
    if (!IsValidScheduleAddRequest(request)) {
        return {};
    }
    return {{{2105, request.containerNo, request.itemNo, DataStyle::Raw}, L"1"}};
}

std::vector<ScheduleCellWrite> BuildScheduleDeleteUndoWrites(const GridRow& row)
{
    if (!IsValidScheduleBinding(row.binding) ||
        row.cells.size() <= ScheduleGridColumn::Order ||
        row.cells[ScheduleGridColumn::ItemName].text.empty()) {
        return {};
    }

    int order = 0;
    if (!TryParsePositiveInt(row.cells[ScheduleGridColumn::Order].text, order)) {
        return {};
    }

    ScheduleAddRequest request{
        row.binding.containerNo,
        row.binding.itemNo,
        order,
        row.cells[ScheduleGridColumn::ItemName].text,
    };
    if (!IsValidScheduleAddRequest(request)) {
        return {};
    }

    std::vector<ScheduleCellWrite> writes;
    writes.push_back({{2104, request.containerNo, request.itemNo, DataStyle::Raw}, EncodeScheduleAddValue(request)});
    if (!row.cells[ScheduleGridColumn::OutboundStart].text.empty()) {
        writes.push_back(MakeRawWrite(2102, row.binding, row.cells[ScheduleGridColumn::OutboundStart].text));
    }
    if (!row.cells[ScheduleGridColumn::OutboundEnd].text.empty()) {
        writes.push_back(MakeRawWrite(3000, row.binding, row.cells[ScheduleGridColumn::OutboundEnd].text));
    }
    return writes;
}

ScheduleDuplicateOrder FindDuplicateScheduleOrder(const GridModel& grid,
                                                  int order,
                                                  GridRowBinding excludedBinding)
{
    if (order <= 0 || order > kMaxScheduleOrder) {
        return {};
    }

    const bool hasExcludedBinding = IsValidScheduleBinding(excludedBinding);
    int rowIndex = 0;
    for (const auto& row : grid.Rows()) {
        if (!IsValidScheduleBinding(row.binding) || row.cells.size() <= ScheduleGridColumn::Order) {
            ++rowIndex;
            continue;
        }
        if (hasExcludedBinding && SameScheduleBinding(row.binding, excludedBinding)) {
            ++rowIndex;
            continue;
        }

        int currentOrder = 0;
        if (TryParsePositiveInt(row.cells[ScheduleGridColumn::Order].text, currentOrder) && currentOrder == order) {
            return {true, row.binding, rowIndex};
        }
        ++rowIndex;
    }

    return {};
}

bool HasScheduleRowBinding(const GridModel& grid, GridRowBinding binding)
{
    if (!IsValidScheduleBinding(binding)) {
        return false;
    }
    return std::any_of(grid.Rows().begin(), grid.Rows().end(), [binding](const GridRow& row) {
        return SameScheduleBinding(row.binding, binding);
    });
}
