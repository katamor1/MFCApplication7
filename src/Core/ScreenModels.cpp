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

std::wstring ReadText(const DataGateway& gateway, const DataKey& key)
{
    const auto value = gateway.Read(key);
    return value.errorCode == BridgeError::Ok ? value.displayText : L"[" + ToDisplayText(value.errorCode) + L"]";
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
            ReadText(gateway, {2104, containerNo, itemNo, DataStyle::SecondsToHhMmSs}),
        });
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
    GridModel grid;
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫終了予定", L"順序"});
    for (int containerNo = 1; containerNo <= 100; ++containerNo) {
        const int itemCount = ToInt(ReadText(gateway, {2003, containerNo, 0, DataStyle::Raw}));
        for (int itemNo = 1; itemNo <= std::min(itemCount, 10); ++itemNo) {
            grid.AddRow({
                GridCell::Text(std::to_wstring(containerNo)),
                GridCell::Text(ReadText(gateway, {2100, containerNo, itemNo, DataStyle::Raw})),
                GridCell::Text(ReadText(gateway, {3000, containerNo, itemNo, DataStyle::Raw})),
                GridCell::Text(ReadText(gateway, {2103, containerNo, itemNo, DataStyle::Raw}), CellKind::Spin),
            },
            {containerNo, itemNo});
        }
    }
    return grid;
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
