#include "ScreenModels.h"

#include "UpdateScheduler.h"

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

/**
 * @brief Resolve display name for maintenance row from catalog metadata.
 */
std::wstring MaintenanceName(const DataCatalog& catalog, int dataId)
{
    const auto* definition = catalog.FindDefinition(dataId);
    if (definition != nullptr && !definition->name.empty()) {
        return definition->name;
    }
    return L"重要情報 " + std::to_wstring(dataId);
}

/**
 * @brief Determine whether a critical value should be surfaced as maintenance abnormal.
 */
bool IsMaintenanceAbnormal(const MaintenanceStatusRow& row)
{
    return row.errorCode != BridgeError::Ok ||
        row.stale ||
        row.displayText.empty() ||
        row.displayText == L"未取得" ||
        row.displayText.find(L"異常") != std::wstring::npos;
}

/**
 * @brief Return true when a maintenance row has no acquired display value.
 */
bool IsMaintenanceMissingValue(const MaintenanceStatusRow& row)
{
    return row.displayText.empty() || row.displayText == L"未取得";
}

/**
 * @brief Return true when a maintenance row contains abnormal status wording.
 */
bool HasMaintenanceAbnormalText(const MaintenanceStatusRow& row)
{
    return row.displayText.find(L"異常") != std::wstring::npos;
}

/**
 * @brief Return true when a launch result belongs to the requested app.
 */
bool MatchesLaunchResult(const ExternalLaunchResult* result, const std::wstring& appId)
{
    return result != nullptr && result->appId == appId;
}

/**
 * @brief Convert a launch result into the system grid status text.
 */
std::wstring ExternalLaunchStatus(const ExternalAppDefinition& app, const ExternalLaunchResult* result)
{
    if (!MatchesLaunchResult(result, app.id)) {
        return L"未起動";
    }
    if (result->success || result->alreadyRunning) {
        return L"起動済み";
    }
    return L"起動失敗";
}

/**
 * @brief Convert a launch result into the system grid detail text.
 */
std::wstring ExternalLaunchDetail(const ExternalAppDefinition& app, const ExternalLaunchResult* result)
{
    if (!MatchesLaunchResult(result, app.id)) {
        return app.executablePath;
    }
    if (result->message.empty()) {
        return result->success ? L"起動しました" : L"起動失敗";
    }
    return result->message;
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
    summary.itemCount = std::max(itemCount, 0);
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
 * @brief Build row-major 3-column container list layout from station snapshot.
 */
ContainerListLayoutModel BuildContainerListLayoutModel(const StationSnapshot& snapshot, int selectedContainerNo)
{
    constexpr int kColumnCount = 3;

    ContainerListLayoutModel layout;
    layout.columnCount = kColumnCount;
    layout.rowCount = static_cast<int>((snapshot.containers.size() + kColumnCount - 1) / kColumnCount);
    layout.cells.reserve(snapshot.containers.size());

    for (size_t index = 0; index < snapshot.containers.size(); ++index) {
        const auto& container = snapshot.containers[index];
        ContainerListCell cell;
        cell.containerNo = container.containerNo;
        cell.column = static_cast<int>(index % kColumnCount);
        cell.row = static_cast<int>(index / kColumnCount);
        cell.displayText = std::to_wstring(container.containerNo);
        cell.containerName = container.containerName;
        cell.state = container.state;
        cell.missing = container.missing;
        cell.selected = container.containerNo == selectedContainerNo;
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
 * @brief Build read-only detail rows for one container.
 */
ReadOnlyDetailModel BuildContainerDetailModel(const ContainerSummary& summary)
{
    ReadOnlyDetailModel detail;
    detail.title = L"コンテナ詳細: " + std::to_wstring(summary.containerNo);
    detail.rows = {
        {L"コンテナ番号", std::to_wstring(summary.containerNo)},
        {L"名称", summary.containerName},
        {L"状態", summary.state},
        {L"品目数", std::to_wstring(summary.itemCount)},
        {L"表示品目数", std::to_wstring(summary.items.size())},
    };

    for (size_t index = 0; index < summary.items.size(); ++index) {
        const auto& item = summary.items[index];
        const std::wstring prefix = L"品目" + std::to_wstring(index + 1) + L" ";
        detail.rows.push_back({prefix + L"名称", item.itemName});
        detail.rows.push_back({prefix + L"入庫日", item.inboundDate});
        detail.rows.push_back({prefix + L"出庫開始", item.outboundStart});
        detail.rows.push_back({prefix + L"出庫順序", item.outboundOrder});
        detail.rows.push_back({prefix + L"作業時間", item.workTime});
    }
    return detail;
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
                GridCell::Text(itemName, CellKind::Text),
                GridCell::Text(ReadText(gateway, {2102, containerNo, itemNo, DataStyle::Raw}), CellKind::Text),
                GridCell::Text(ReadText(gateway, {3000, containerNo, itemNo, DataStyle::Raw}), CellKind::Text),
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
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫開始予定", L"出庫終了予定", L"順序"});
    for (auto& entry : entries) {
        grid.AddRow(std::move(entry.cells), entry.binding);
    }
    return grid;
}

/**
 * @brief Build read-only detail rows for one schedule row binding.
 */
ReadOnlyDetailModel BuildScheduleDetailModel(const DataGateway& gateway, GridRowBinding binding)
{
    ReadOnlyDetailModel detail;
    detail.title = L"スケジュール詳細: コンテナ " + std::to_wstring(binding.containerNo) +
        L" / 品目 " + std::to_wstring(binding.itemNo);
    detail.rows = {
        {L"コンテナ番号", std::to_wstring(binding.containerNo)},
        {L"品目番号", std::to_wstring(binding.itemNo)},
        {L"品目名", ReadText(gateway, {2100, binding.containerNo, binding.itemNo, DataStyle::Raw})},
        {L"入庫日", ReadText(gateway, {2101, binding.containerNo, binding.itemNo, DataStyle::Raw})},
        {L"出庫開始", ReadText(gateway, {2102, binding.containerNo, binding.itemNo, DataStyle::Raw})},
        {L"出庫終了", ReadText(gateway, {3000, binding.containerNo, binding.itemNo, DataStyle::Raw})},
        {L"出庫順序", ReadText(gateway, {2103, binding.containerNo, binding.itemNo, DataStyle::Raw})},
        {L"作業時間", ReadText(gateway, {2106, binding.containerNo, binding.itemNo, DataStyle::SecondsToHhMmSs})},
    };
    return detail;
}

/**
 * @brief Build the two order writes needed to swap selected row with previous visible row.
 */
std::vector<ScheduleCellWrite> BuildScheduleMoveUpWrites(const GridModel& grid, int selectedRow)
{
    if (selectedRow <= 0 || selectedRow >= static_cast<int>(grid.Rows().size())) {
        return {};
    }

    const auto& previous = grid.Rows()[static_cast<size_t>(selectedRow - 1)];
    const auto& selected = grid.Rows()[static_cast<size_t>(selectedRow)];
    if (previous.binding.containerNo <= 0 || previous.binding.itemNo <= 0 ||
        selected.binding.containerNo <= 0 || selected.binding.itemNo <= 0 ||
        previous.cells.size() <= ScheduleGridColumn::Order || selected.cells.size() <= ScheduleGridColumn::Order) {
        return {};
    }

    int previousOrder = 0;
    int selectedOrder = 0;
    if (!TryParsePositiveInt(previous.cells[ScheduleGridColumn::Order].text, previousOrder) ||
        !TryParsePositiveInt(selected.cells[ScheduleGridColumn::Order].text, selectedOrder)) {
        return {};
    }

    return {
        {{2103, selected.binding.containerNo, selected.binding.itemNo, DataStyle::Raw}, std::to_wstring(previousOrder)},
        {{2103, previous.binding.containerNo, previous.binding.itemNo, DataStyle::Raw}, std::to_wstring(selectedOrder)},
    };
}

/**
 * @brief Build one write for a valid in-cell schedule edit.
 */
std::vector<ScheduleCellWrite> BuildScheduleCellEditWrites(GridRowBinding binding,
                                                           int column,
                                                           CellKind kind,
                                                           const std::wstring& value)
{
    constexpr int kMaxOrder = 9999;

    if (binding.containerNo <= 0 || binding.itemNo <= 0) {
        return {};
    }

    if (column == ScheduleGridColumn::ItemName ||
        column == ScheduleGridColumn::OutboundStart ||
        column == ScheduleGridColumn::OutboundEnd) {
        if (kind != CellKind::Text || value.empty()) {
            return {};
        }

        int dataId = 0;
        if (column == ScheduleGridColumn::ItemName) {
            dataId = 2100;
        } else if (column == ScheduleGridColumn::OutboundStart) {
            dataId = 2102;
        } else {
            dataId = 3000;
        }
        return {
            {{dataId, binding.containerNo, binding.itemNo, DataStyle::Raw}, value},
        };
    }

    if (column != ScheduleGridColumn::Order || kind != CellKind::Spin) {
        return {};
    }

    int order = 0;
    if (!TryParsePositiveInt(value, order) || order > kMaxOrder) {
        return {};
    }

    return {
        {{2103, binding.containerNo, binding.itemNo, DataStyle::Raw}, value},
    };
}

/**
 * @brief Build V1 fixed external app definitions for the system screen.
 */
std::vector<ExternalAppDefinition> BuildDefaultExternalAppDefinitions()
{
    return {
        {L"container-controller", L"コンテナコントローラ", L"ContainerController.exe", L"", L"", false},
    };
}

/**
 * @brief Build system screen rows for external launch state and history state.
 */
GridModel BuildSystemGrid(const UpdateSnapshot& snapshot,
                          const std::vector<ExternalAppDefinition>& externalApps,
                          const ExternalLaunchResult* lastLaunchResult)
{
    GridModel grid;
    grid.SetColumns({L"種別", L"名称", L"状態", L"詳細"});

    for (const auto& app : externalApps) {
        grid.AddRow({
            GridCell::Text(L"外部アプリ"),
            GridCell::Text(app.label),
            GridCell::Text(ExternalLaunchStatus(app, lastLaunchResult)),
            GridCell::Text(ExternalLaunchDetail(app, lastLaunchResult)),
        }, {0, 0, 0, app.id});
    }

    std::wstring status = snapshot.historyStatusText.empty() ? L"待機" : snapshot.historyStatusText;
    status += L" " + std::to_wstring(snapshot.historyProgress) + L"%";
    grid.AddRow({
        GridCell::Text(L"履歴"),
        GridCell::Text(L"出庫履歴"),
        GridCell::Text(snapshot.historyRunning ? L"取得中" : L"待機"),
        GridCell::Text(status),
    });

    for (const auto& record : snapshot.historyRecords) {
        grid.AddRow({
            GridCell::Text(L"履歴"),
            GridCell::Text(L"日" + std::to_wstring(record.dayOffset) + L" #" + std::to_wstring(record.recordIndex)),
            GridCell::Text(record.stale ? ToDisplayText(record.errorCode) : L"OK"),
            GridCell::Text(record.displayText),
        });
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

/**
 * @brief Build maintenance critical status rows from latest update snapshot.
 */
MaintenanceStatusModel BuildMaintenanceStatusModel(const DataCatalog& catalog, const UpdateSnapshot& snapshot)
{
    MaintenanceStatusModel model;
    const auto& keys = catalog.CriticalKeys();
    model.rows.reserve(keys.size());

    for (size_t index = 0; index < keys.size(); ++index) {
        MaintenanceStatusRow row;
        row.dataId = keys[index].dataId;
        row.name = MaintenanceName(catalog, row.dataId);

        if (index < snapshot.criticalValues.size()) {
            const auto& value = snapshot.criticalValues[index];
            row.displayText = value.displayText.empty() ? L"未取得" : value.displayText;
            row.errorCode = value.errorCode;
            row.stale = value.stale || value.errorCode != BridgeError::Ok;
        } else {
            row.displayText = L"未取得";
            row.errorCode = BridgeError::InternalError;
            row.stale = true;
        }

        row.abnormal = IsMaintenanceAbnormal(row);
        row.operationAvailable = row.abnormal;
        row.supportHint = BuildMaintenanceSupportHint(row);
        if (row.abnormal) {
            ++model.abnormalCount;
        }
        model.rows.push_back(std::move(row));
    }

    return model;
}

/**
 * @brief Build read-only operator support hints for one maintenance status row.
 */
MaintenanceSupportHint BuildMaintenanceSupportHint(const MaintenanceStatusRow& row)
{
    constexpr const wchar_t* kNoRecoveryWriteNote =
        L"正式な復旧操作IDが未定義のため、本画面から復旧Writeは行わない。";

    MaintenanceSupportHint hint;
    hint.operatorNote = kNoRecoveryWriteNote;

    if (!row.abnormal) {
        hint.reason = MaintenanceAbnormalReason::None;
        hint.reasonText = L"なし";
        hint.priorityText = L"通常";
        hint.recommendedCheck = L"追加確認不要。";
        return hint;
    }

    if (row.errorCode != BridgeError::Ok) {
        hint.reason = MaintenanceAbnormalReason::ReadError;
        hint.reasonText = L"ReadError";
        hint.priorityText = L"高";
        hint.recommendedCheck = L"通信、COM接続、対象装置の状態を確認してください。";
        return hint;
    }

    if (row.stale) {
        hint.reason = MaintenanceAbnormalReason::Stale;
        hint.reasonText = L"Stale";
        hint.priorityText = L"中";
        hint.recommendedCheck = L"重要情報の最新値を再確認してください。";
        return hint;
    }

    if (IsMaintenanceMissingValue(row)) {
        hint.reason = MaintenanceAbnormalReason::MissingValue;
        hint.reasonText = L"MissingValue";
        hint.priorityText = L"中";
        hint.recommendedCheck = L"重要情報の取得状態を確認してください。";
        return hint;
    }

    if (HasMaintenanceAbnormalText(row)) {
        hint.reason = MaintenanceAbnormalReason::AbnormalText;
        hint.reasonText = L"AbnormalText";
        hint.priorityText = L"高";
        hint.recommendedCheck = L"コンテナコントローラで該当状態を確認してください。";
        return hint;
    }

    hint.reason = MaintenanceAbnormalReason::Unknown;
    hint.reasonText = L"Unknown";
    hint.priorityText = L"中";
    hint.recommendedCheck = L"重要情報の状態を確認してください。";
    return hint;
}

/**
 * @brief Build read-only detail rows for one maintenance status row.
 */
ReadOnlyDetailModel BuildMaintenanceDetailModel(const MaintenanceStatusRow& row)
{
    const auto supportHint = row.supportHint.reasonText.empty() ? BuildMaintenanceSupportHint(row) : row.supportHint;
    ReadOnlyDetailModel detail;
    detail.title = L"保守詳細: " + row.name;
    detail.rows = {
        {L"データID", std::to_wstring(row.dataId)},
        {L"名称", row.name},
        {L"値", row.displayText.empty() ? L"未取得" : row.displayText},
        {L"状態", row.abnormal ? L"異常" : L"正常"},
        {L"エラー", ToDisplayText(row.errorCode)},
        {L"stale", row.stale ? L"true" : L"false"},
        {L"操作可", row.operationAvailable ? L"true" : L"false"},
        {L"原因分類", supportHint.reasonText},
        {L"確認優先度", supportHint.priorityText},
        {L"推奨確認", supportHint.recommendedCheck},
        {L"管理者メモ", supportHint.operatorNote},
    };
    return detail;
}

/**
 * @brief Build maintenance grid from maintenance status model.
 */
GridModel BuildMaintenanceStatusGrid(const MaintenanceStatusModel& model)
{
    GridModel grid;
    grid.SetColumns({L"ID", L"項目", L"値", L"状態", L"操作可"});
    for (const auto& row : model.rows) {
        grid.AddRow({
            GridCell::Text(std::to_wstring(row.dataId)),
            GridCell::Text(row.name),
            GridCell::Text(row.displayText),
            GridCell::Text(row.abnormal ? L"異常" : L"正常"),
            GridCell::Text(row.operationAvailable ? L"true" : L"false", CellKind::CheckBox),
        }, {0, 0, row.dataId});
    }
    return grid;
}
