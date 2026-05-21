#include "ScreenModels.h"

#include <algorithm>

/**
 * @file ScreenModels.cpp
 * @brief Build screen-layer DTOs from gateway data for UI screens.
 */

namespace {

/**
 * @brief Convert text data safely to integer; non-numeric returns 0.
 */
int ToInt(const std::wstring& value)
{
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

/**
 * @brief Parse a strictly positive integer order value.
 */
bool TryParsePositiveInt(const std::wstring& value, int& parsed)
{
    try {
        size_t index = 0;
        const int result = std::stoi(value, &index);
        if (index != value.size() || result <= 0) {
            return false;
        }
        parsed = result;
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring ReadText(const DataGateway& gateway, const DataKey& key)
{
    const auto value = gateway.Read(key);
    return value.errorCode == BridgeError::Ok ? value.displayText : L"[" + ToDisplayText(value.errorCode) + L"]";
}

struct ScheduleGridEntry
{
    std::vector<GridCell> cells;
    GridRowBinding binding;
    int order{};
    bool validOrder{false};
};

StationLayoutKind LayoutKindForColumn(int column) noexcept
{
    switch (column) {
    case 0:
        return StationLayoutKind::LeftSemiCircle;
    case 2:
        return StationLayoutKind::BottomSemiCircle;
    case 4:
        return StationLayoutKind::RightSemiCircle;
    default:
        return StationLayoutKind::Straight;
    }
}

} // namespace

/**
 * @brief Build per-container summary including state and visible items.
 */
ContainerSummary BuildContainerSummary(const DataGateway& gateway, int containerNo, int maxItems)
{
    ContainerSummary summary;
    summary.containerNo = containerNo;
    summary.containerName = ReadText(gateway, {2001, containerNo, 0, DataStyle::Raw});
    summary.state = ReadText(gateway, {2002, containerNo, 0, DataStyle::Raw});
    summary.missing = summary.state == L"コンテナなし";

    const int itemCount = ToInt(ReadText(gateway, {2003, containerNo, 0, DataStyle::Raw}));
    const int visibleItems = std::min(std::max(itemCount, 0), maxItems);
    for (int itemNo = 1; itemNo <= visibleItems; ++itemNo) {
        summary.items.push_back({
            ReadText(gateway, {2100, containerNo, itemNo, DataStyle::Raw}),
            ReadText(gateway, {2101, containerNo, itemNo, DataStyle::Raw}),
            ReadText(gateway, {2102, containerNo, itemNo, DataStyle::Raw}),
            ReadText(gateway, {2103, containerNo, itemNo, DataStyle::ThousandsSeparated}),
            ReadText(gateway, {2106, containerNo, itemNo, DataStyle::SecondsToHhMmSs}),
        });
        if (summary.items.back().itemName.empty()) {
            summary.items.pop_back();
        }
    }
    return summary;
}

/**
 * @brief Build station snapshot with all containers and selected container.
 */
StationSnapshot BuildStationSnapshot(const DataGateway& gateway, int selectedContainerNo)
{
    StationSnapshot snapshot;
    snapshot.containers.reserve(100);
    for (int containerNo = 1; containerNo <= 100; ++containerNo) {
        snapshot.containers.push_back(BuildContainerSummary(gateway, containerNo, 0));
    }
    snapshot.selected = BuildContainerSummary(gateway, std::max(1, std::min(100, selectedContainerNo)), 5);
    return snapshot;
}

/**
 * @brief Build fixed 5-column by 20-row station layout from station snapshot.
 */
StationLayoutModel BuildStationLayoutModel(const StationSnapshot& snapshot, int selectedContainerNo)
{
    constexpr int kColumnCount = 5;
    constexpr int kRowsPerColumn = 20;

    StationLayoutModel layout;
    layout.columnCount = kColumnCount;
    layout.rowsPerColumn = kRowsPerColumn;
    layout.cells.reserve(static_cast<size_t>(kColumnCount * kRowsPerColumn));

    for (int index = 0; index < kColumnCount * kRowsPerColumn; ++index) {
        const int containerNo = index + 1;
        const int column = index / kRowsPerColumn;
        const int row = index % kRowsPerColumn;
        StationLayoutCell cell;
        cell.containerNo = containerNo;
        cell.column = column;
        cell.row = row;
        cell.kind = LayoutKindForColumn(column);
        cell.displayText = std::to_wstring(containerNo);
        cell.selected = containerNo == selectedContainerNo;

        const auto found = std::find_if(snapshot.containers.begin(), snapshot.containers.end(), [containerNo](const ContainerSummary& container) {
            return container.containerNo == containerNo;
        });
        if (found != snapshot.containers.end()) {
            cell.state = found->state;
            cell.missing = found->missing;
        }

        layout.cells.push_back(std::move(cell));
    }

    return layout;
}

/**
 * @brief Build simplified container list grid for station/list screens.
 */
GridModel BuildContainerListGrid(const StationSnapshot& snapshot)
{
    GridModel grid;
    grid.SetColumns({L"番号", L"名称", L"状態"});
    for (const auto& container : snapshot.containers) {
        grid.AddRow({
            GridCell::Text(std::to_wstring(container.containerNo)),
            GridCell::Text(container.containerName),
            GridCell::Text(container.state),
        });
    }
    return grid;
}

/**
 * @brief Build schedule list from visible item rows.
 */
GridModel BuildScheduleGrid(const DataGateway& gateway)
{
    std::vector<ScheduleGridEntry> entries;
    for (int containerNo = 1; containerNo <= 100; ++containerNo) {
        const int itemCount = ToInt(ReadText(gateway, {2003, containerNo, 0, DataStyle::Raw}));
        for (int itemNo = 1; itemNo <= std::min(itemCount, 1000); ++itemNo) {
            const auto itemName = ReadText(gateway, {2100, containerNo, itemNo, DataStyle::Raw});
            if (itemName.empty()) {
                continue;
            }
            const auto orderText = ReadText(gateway, {2103, containerNo, itemNo, DataStyle::Raw});
            int order = 0;
            const bool validOrder = TryParsePositiveInt(orderText, order);
            entries.push_back({
                {
                GridCell::Text(std::to_wstring(containerNo)),
                GridCell::Text(itemName),
                GridCell::Text(ReadText(gateway, {3000, containerNo, itemNo, DataStyle::Raw})),
                GridCell::Text(orderText, CellKind::Spin),
                },
                {containerNo, itemNo},
                order,
                validOrder,
            });
        }
    }

    std::sort(entries.begin(), entries.end(), [](const ScheduleGridEntry& left, const ScheduleGridEntry& right) {
        if (left.validOrder != right.validOrder) {
            return left.validOrder;
        }
        if (left.validOrder && left.order != right.order) {
            return left.order < right.order;
        }
        if (left.binding.containerNo != right.binding.containerNo) {
            return left.binding.containerNo < right.binding.containerNo;
        }
        return left.binding.itemNo < right.binding.itemNo;
    });

    GridModel grid;
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫終了予定", L"順序"});
    for (auto& entry : entries) {
        grid.AddRow(std::move(entry.cells), entry.binding);
    }
    return grid;
}

/**
 * @brief Build the two order writes needed to swap selected row with previous visible row.
 */
std::vector<ScheduleOrderWrite> BuildScheduleMoveUpWrites(const GridModel& grid, int selectedRow)
{
    if (selectedRow <= 0 || selectedRow >= static_cast<int>(grid.Rows().size())) {
        return {};
    }

    const auto& previous = grid.Rows()[static_cast<size_t>(selectedRow - 1)];
    const auto& selected = grid.Rows()[static_cast<size_t>(selectedRow)];
    if (previous.binding.containerNo <= 0 || previous.binding.itemNo <= 0 ||
        selected.binding.containerNo <= 0 || selected.binding.itemNo <= 0 ||
        previous.cells.size() <= 3 || selected.cells.size() <= 3) {
        return {};
    }

    int previousOrder = 0;
    int selectedOrder = 0;
    if (!TryParsePositiveInt(previous.cells[3].text, previousOrder) ||
        !TryParsePositiveInt(selected.cells[3].text, selectedOrder)) {
        return {};
    }

    return {
        {{2103, selected.binding.containerNo, selected.binding.itemNo, DataStyle::Raw}, std::to_wstring(previousOrder)},
        {{2103, previous.binding.containerNo, previous.binding.itemNo, DataStyle::Raw}, std::to_wstring(selectedOrder)},
    };
}

/**
 * @brief Build maintenance screen rows from fixed definition range.
 */
GridModel BuildMaintenanceGrid(const DataGateway& gateway)
{
    GridModel grid;
    grid.SetColumns({L"項目", L"値", L"操作可"});
    for (int dataId = 1000; dataId < 1020; ++dataId) {
        grid.AddRow({
            GridCell::Text(L"重要情報 " + std::to_wstring(dataId)),
            GridCell::Text(ReadText(gateway, {dataId, 0, 0, DataStyle::Raw})),
            GridCell::Text(L"false", CellKind::CheckBox),
        });
    }
    return grid;
}
