#include "DataCatalog.h"
#include "DataGateway.h"
#include "FunctionBarModel.h"
#include "GridModel.h"
#include "MockBackendBridge.h"
#include "ScreenModels.h"
#include "StatusSummary.h"
#include "UpdateScheduler.h"
#include "BridgeFactory.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/**
 * @file tests/CoreTests/main.cpp
 * @brief Runs core behavior regression tests without an external test framework.
 */

namespace {

/**
 * @brief Throw a runtime error when a test assertion fails.
 * @param condition Assertion result.
 * @param message Failure message reported by the test runner.
 */
void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Verify default catalog critical keys and style constraints.
 */
void TestCatalogDefinesCriticalDataAndStyles()
{
    const auto catalog = DataCatalog::CreateDefault();
    Check(catalog.CriticalKeys().size() == 20, "critical key count must be 20");
    Check(catalog.IsStyleAllowed(1000, DataStyle::Raw), "critical raw style should be allowed");
    Check(catalog.IsStyleAllowed(1010, DataStyle::ThousandsSeparated), "number style should be allowed");
    Check(!catalog.IsStyleAllowed(1000, DataStyle::MillimetersToInches), "invalid critical style should be rejected");
}

/**
 * @brief Verify provisional schedule mutation IDs are present and writable.
 */
void TestCatalogDefinesScheduleMutationIds()
{
    const auto catalog = DataCatalog::CreateDefault();
    Check(catalog.FindDefinition(2104) != nullptr, "schedule add id should exist");
    Check(catalog.FindDefinition(2105) != nullptr, "schedule delete id should exist");
    Check(catalog.IsWritable(2104), "schedule add should be writable");
    Check(catalog.IsWritable(2105), "schedule delete should be writable");
    Check(catalog.IsStyleAllowed(2104, DataStyle::Raw), "schedule add should use raw style");
    Check(catalog.IsStyleAllowed(2105, DataStyle::Raw), "schedule delete should use raw style");
    Check(catalog.IsStyleAllowed(2106, DataStyle::SecondsToHhMmSs), "work time should move to 2106 with hhmmss style");
}

/**
 * @brief Verify the JSON catalog can be loaded with expected definitions.
 */
void TestCatalogLoadsFromJsonFile()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    Check(catalog.Definitions().size() == 33, "json catalog should include 33 definitions");
    Check(catalog.CriticalKeys().size() == 20, "json catalog should include 20 critical keys");
    Check(catalog.FindDefinition(2001)->writable, "container name should be writable");
    Check(catalog.FindDefinition(2104)->writable, "json schedule add should be writable");
    Check(catalog.FindDefinition(2105)->writable, "json schedule delete should be writable");
    Check(catalog.IsStyleAllowed(2106, DataStyle::SecondsToHhMmSs), "work time should allow hhmmss style");
}

/**
 * @brief Verify invalid catalog JSON files are rejected.
 */
void TestCatalogRejectsInvalidJsonFile()
{
    bool failed = false;
    try {
        DataCatalog::LoadFromFile(L"tests/CoreTests/invalid_data_catalog.json");
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed, "invalid catalog json should throw");
}

/**
 * @brief Verify unknown display-style names fail catalog loading.
 */
void TestCatalogRejectsUnknownStyleName()
{
    bool failed = false;
    try {
        DataCatalog::LoadFromFile(L"tests/CoreTests/invalid_style_catalog.json");
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed, "unknown catalog style should throw");
}

/**
 * @brief Verify factory options create a usable in-process mock bridge.
 */
void TestBridgeFactoryCreatesMockBridge()
{
    BridgeFactoryOptions options;
    options.bridgeMode = BridgeMode::InProcessMock;
    options.catalogPath = L"config/data_catalog.json";
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    Check(gateway.Connect(options.ipAddress) == BridgeError::Ok, "factory mock bridge should connect");
    const auto value = gateway.Read({1010, 0, 0, DataStyle::ThousandsSeparated});
    Check(value.errorCode == BridgeError::Ok, "factory mock bridge should read formatted critical data");
    Check(value.displayText.find(L",") != std::wstring::npos, "factory mock bridge should preserve formatting");
}

/**
 * @brief Verify mock reads format values and reject invalid style requests.
 */
void TestMockBridgeFormatsValuesAndRejectsInvalidStyle()
{
    auto catalog = DataCatalog::CreateDefault();
    MockBackendBridge bridge(catalog);
    Check(bridge.Connect(L"192.168.0.10") == BridgeError::Ok, "connect should succeed for IPv4 text");

    std::wstring value;
    Check(bridge.Read({1010, 0, 0, DataStyle::ThousandsSeparated}, value) == BridgeError::Ok, "number style should read");
    Check(value.find(L",") != std::wstring::npos, "number style should include thousands separator");

    Check(bridge.Read({1012, 0, 0, DataStyle::SecondsToHhMmSs}, value) == BridgeError::Ok, "time style should read");
    Check(value.size() == 8 && value[2] == L':' && value[5] == L':', "time style should be hh:mm:ss");

    Check(bridge.Read({1014, 0, 0, DataStyle::MillimetersToInches}, value) == BridgeError::Ok, "inch style should read");
    Check(value.find(L"in") != std::wstring::npos, "inch style should append unit");

    Check(bridge.Read({1014, 0, 0, DataStyle::SecondsToHhMmSs}, value) == BridgeError::InvalidStyle, "invalid style should fail");
}

/**
 * @brief Verify gateway read failures preserve error state and mark stale data.
 */
void TestGatewayMarksErrorsAsStale()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"10.0.0.5") == BridgeError::Ok, "gateway connect should succeed");

    const auto ok = gateway.Read({2000, 1, 0, DataStyle::Raw});
    Check(ok.errorCode == BridgeError::Ok, "valid gateway read should succeed");
    Check(!ok.stale, "valid gateway read should not be stale");
    Check(!ok.displayText.empty(), "valid gateway read should have text");

    const auto bad = gateway.Read({9999, 0, 0, DataStyle::Raw});
    Check(bad.errorCode == BridgeError::InvalidDataId, "invalid id should preserve error");
    Check(bad.stale, "invalid gateway read should be stale");
}

/**
 * @brief Verify container function actions reflect current selection state.
 */
void TestFunctionActionsReflectSelection()
{
    const auto none = BuildContainerFunctionActions(false, false);
    Check(none.size() == 8, "function bar should always expose 8 slots");
    Check(!none[0].enabled, "details should be disabled without selection");

    const auto selected = BuildContainerFunctionActions(true, false);
    Check(selected[0].enabled && selected[0].label == L"詳細", "details should be enabled for present container");

    const auto missing = BuildContainerFunctionActions(true, true);
    Check(!missing[0].enabled, "details should be disabled for missing container");
}

/**
 * @brief Verify keyboard F1-F8 values map to function bar slots.
 */
void TestFunctionSlotFromVirtualKey()
{
    for (int index = 0; index < 8; ++index) {
        Check(FunctionSlotFromVirtualKey(0x70 + index) == index + 1, "F1-F8 virtual keys should map to slots 1-8");
    }
    Check(FunctionSlotFromVirtualKey(0x6F) == 0, "key before F1 should not map to a function slot");
    Check(FunctionSlotFromVirtualKey(0x78) == 0, "key after F8 should not map to a function slot");
}

/**
 * @brief Verify schedule actions expose order changes only with a selection.
 */
void TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection()
{
    const auto none = BuildScheduleFunctionActions(false, false);
    Check(none.size() == 8, "schedule actions should always expose 8 slots");
    Check(!none[0].enabled, "schedule details should be disabled without selection");
    Check(!none[1].enabled, "schedule order change should be disabled without selection");
    Check(none[2].enabled && none[2].id == L"add", "schedule add should be enabled without selection");
    Check(!none[3].enabled, "schedule delete should be disabled without selection");
    Check(!none[4].enabled, "schedule move-up should be disabled without selection");

    const auto selected = BuildScheduleFunctionActions(true, true);
    Check(selected[0].enabled && selected[0].id == L"details", "schedule details should be enabled with selection");
    Check(selected[1].enabled && selected[1].id == L"order-change", "schedule F2 should edit order with selection");
    Check(selected[1].label == L"順序変更", "schedule F2 should be labeled for order change");
    Check(selected[2].enabled && selected[2].id == L"add", "schedule F3 should add schedule rows");
    Check(selected[3].enabled && selected[3].id == L"delete", "schedule F4 should delete selected rows");
    Check(selected[4].enabled && selected[4].id == L"move-up", "schedule F5 should move selected rows up");

    const auto first = BuildScheduleFunctionActions(true, false);
    Check(!first[4].enabled, "schedule F5 should be disabled for first row");
}

/**
 * @brief Verify grid rows preserve their cell editor kinds.
 */
void TestGridModelKeepsCellKinds()
{
    GridModel grid;
    grid.SetColumns({L"Name", L"Qty", L"Enabled"});
    grid.AddRow({GridCell::Text(L"Item A", CellKind::ReadOnlyText),
                 GridCell::Text(L"3", CellKind::Spin),
                 GridCell::Text(L"true", CellKind::CheckBox)});

    Check(grid.ColumnCount() == 3, "grid should keep column count");
    Check(grid.RowCount() == 1, "grid should keep row count");
    Check(grid.Rows()[0].cells[1].kind == CellKind::Spin, "grid should preserve spin cell kind");
    Check(grid.Rows()[0].cells[2].kind == CellKind::CheckBox, "grid should preserve checkbox cell kind");
}

/**
 * @brief Verify schedule grid rows bind to container and item identifiers.
 */
void TestScheduleGridBindsRowsToContainerItems()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto grid = BuildScheduleGrid(gateway);
    Check(grid.RowCount() > 0, "schedule grid should contain rows");
    const auto& firstRow = grid.Rows().front();
    Check(firstRow.binding.containerNo == 1, "first schedule row should bind container 1");
    Check(firstRow.binding.itemNo == 1, "first schedule row should bind item 1");
    Check(firstRow.cells.size() == 4, "schedule row should keep four cells");
    Check(firstRow.cells[3].kind == CellKind::Spin, "schedule order cell should remain spin kind");
}

/**
 * @brief Verify schedule grid sorts by outbound order then container/item.
 */
void TestScheduleGridSortsByOutboundOrder()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    Check(gateway.Write({2103, 10, 1, DataStyle::Raw}, L"1") == BridgeError::Ok, "test row should accept low order");
    Check(gateway.Write({2103, 1, 1, DataStyle::Raw}, L"9000") == BridgeError::Ok, "test row should accept high order");
    Check(gateway.Write({2103, 2, 1, DataStyle::Raw}, L"1") == BridgeError::Ok, "tie row should accept low order");

    const auto grid = BuildScheduleGrid(gateway);
    Check(grid.RowCount() > 2, "schedule grid should contain sorted rows");
    Check(grid.Rows()[0].binding.containerNo == 2 && grid.Rows()[0].binding.itemNo == 1, "tie sort should use container/item order first");
    Check(grid.Rows()[1].binding.containerNo == 10 && grid.Rows()[1].binding.itemNo == 1, "low order row should sort before default rows");

    int previousOrder = 0;
    for (const auto& row : grid.Rows()) {
        const int currentOrder = std::stoi(row.cells[3].text);
        Check(currentOrder >= previousOrder, "schedule rows should be non-decreasing by order");
        previousOrder = currentOrder;
    }
}

/**
 * @brief Verify mock writes are visible through subsequent schedule reads.
 */
void TestMockWriteUpdatesScheduleOrderReadback()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const DataKey orderKey{2103, 1, 1, DataStyle::Raw};
    Check(gateway.Write(orderKey, L"4321") == BridgeError::Ok, "writable schedule order should accept write");
    const auto value = gateway.Read(orderKey);
    Check(value.errorCode == BridgeError::Ok, "written schedule order should read back");
    Check(value.displayText == L"4321", "written schedule order should preserve new value");
    const auto formatted = gateway.Read({2103, 1, 1, DataStyle::ThousandsSeparated});
    Check(formatted.errorCode == BridgeError::Ok, "written schedule order should read back with formatted style");
    Check(formatted.displayText == L"4,321", "formatted schedule order should use raw written value");
}

/**
 * @brief Find one grid row by schedule binding.
 */
bool HasScheduleRow(const GridModel& grid, int containerNo, int itemNo, std::wstring* itemName = nullptr, std::wstring* order = nullptr)
{
    for (const auto& row : grid.Rows()) {
        if (row.binding.containerNo == containerNo && row.binding.itemNo == itemNo) {
            if (itemName != nullptr && row.cells.size() > 1) {
                *itemName = row.cells[1].text;
            }
            if (order != nullptr && row.cells.size() > 3) {
                *order = row.cells[3].text;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief Verify mock schedule add/delete writes are visible in schedule grid reads.
 */
void TestMockScheduleAddAndDeleteReflectInGrid()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const ScheduleAddRequest request{1, 3, 777, L"ADDED-ITEM"};
    Check(IsValidScheduleAddRequest(request), "schedule add request should validate");
    Check(gateway.Write({2104, request.containerNo, request.itemNo, DataStyle::Raw}, EncodeScheduleAddValue(request)) == BridgeError::Ok,
          "schedule add write should succeed");

    std::wstring itemName;
    std::wstring order;
    Check(HasScheduleRow(BuildScheduleGrid(gateway), 1, 3, &itemName, &order), "added schedule row should appear in grid");
    Check(itemName == L"ADDED-ITEM", "added schedule row should expose item name");
    Check(order == L"777", "added schedule row should expose order");

    Check(gateway.Write({2105, 1, 3, DataStyle::Raw}, L"1") == BridgeError::Ok, "schedule delete write should succeed");
    Check(!HasScheduleRow(BuildScheduleGrid(gateway), 1, 3), "deleted schedule row should disappear from grid");
}

/**
 * @brief Verify move-up writes swap selected and previous row orders.
 */
void TestBuildScheduleMoveUpWrites()
{
    GridModel grid;
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫終了予定", L"順序"});
    grid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A"), GridCell::Text(L""), GridCell::Text(L"10", CellKind::Spin)}, {1, 1});
    grid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B"), GridCell::Text(L""), GridCell::Text(L"20", CellKind::Spin)}, {2, 1});

    const auto writes = BuildScheduleMoveUpWrites(grid, 1);
    Check(writes.size() == 2, "move-up should emit two order writes");
    Check(writes[0].key == DataKey{2103, 2, 1, DataStyle::Raw}, "selected row should receive previous order");
    Check(writes[0].value == L"10", "selected row should write previous order value");
    Check(writes[1].key == DataKey{2103, 1, 1, DataStyle::Raw}, "previous row should receive selected order");
    Check(writes[1].value == L"20", "previous row should write selected order value");

    Check(BuildScheduleMoveUpWrites(grid, 0).empty(), "first row should not move up");
    Check(BuildScheduleMoveUpWrites(grid, -1).empty(), "negative row should not move up");

    GridModel invalidGrid;
    invalidGrid.SetColumns({L"コンテナ", L"品目名", L"出庫終了予定", L"順序"});
    invalidGrid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A"), GridCell::Text(L""), GridCell::Text(L"bad", CellKind::Spin)}, {1, 1});
    invalidGrid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B"), GridCell::Text(L""), GridCell::Text(L"20", CellKind::Spin)}, {2, 1});
    Check(BuildScheduleMoveUpWrites(invalidGrid, 1).empty(), "non-numeric order should not move up");
}

/**
 * @brief Wait until write completion metrics reach the requested count.
 * @param coordinator Coordinator under test.
 * @param minimumCount Required completed write count.
 * @return true when the metric reaches the requested value before timeout.
 */
bool WaitForWriteCount(const UpdateCoordinator& coordinator, int minimumCount)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (coordinator.Metrics().writeCompletedCount >= minimumCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Wait until history loading is no longer running.
 * @param coordinator Coordinator under test.
 * @return true when history stops before timeout.
 */
bool WaitForHistoryStop(const UpdateCoordinator& coordinator)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (!coordinator.Snapshot().historyRunning) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Wait until history loading reports progress.
 * @param coordinator Coordinator under test.
 * @return true when progress is observed before timeout.
 */
bool WaitForHistoryProgress(const UpdateCoordinator& coordinator)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto snapshot = coordinator.Snapshot();
        if (snapshot.historyRunning && snapshot.historyProgress > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Verify accepted and rejected history day-count boundaries.
 */
void TestHistoryRequestValidation()
{
    Check(!IsValidHistoryRequest({0}), "history days 0 should be rejected");
    Check(IsValidHistoryRequest({1}), "history days 1 should be accepted");
    Check(IsValidHistoryRequest({365}), "history days 365 should be accepted");
    Check(!IsValidHistoryRequest({366}), "history days 366 should be rejected");
}

/**
 * @brief Verify history keys map day offset and record index to sub IDs.
 */
void TestHistoryKeyGenerationUsesOutboundHistoryId()
{
    const auto key = MakeHistoryKey(2, 345);
    Check(key.dataId == 4000, "history key should read dataId 4000");
    Check(key.subId1 == 2, "history key should use day offset as subId1");
    Check(key.subId2 == 345, "history key should use record index as subId2");
    Check(key.style == DataStyle::Raw, "history key should use raw style");
}

/**
 * @brief Verify system function actions switch between start and cancel states.
 */
void TestSystemFunctionActionsReflectHistoryRunning()
{
    const auto idle = BuildSystemFunctionActions(false);
    Check(idle[0].enabled && idle[0].id == L"history", "system F1 should start history while idle");
    Check(!idle[1].enabled, "system F2 should be disabled while idle");

    const auto running = BuildSystemFunctionActions(true);
    Check(!running[0].enabled, "system F1 should be disabled while history is running");
    Check(running[1].enabled && running[1].id == L"history-cancel", "system F2 should cancel running history");
}

/**
 * @brief Verify successful writes update coordinator metrics and backend state.
 */
void TestUpdateCoordinatorRecordsSuccessfulWriteMetrics()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"6789");
    Check(WaitForWriteCount(coordinator, 1), "coordinator should complete queued write");
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.writeCompletedCount == 1, "coordinator should record one write completion");
    Check(metrics.lastWriteErrorCode == BridgeError::Ok, "coordinator should record successful write error code");
    Check(metrics.scheduleOrderWriteCompletedCount == 1, "coordinator should count schedule order writes");
    Check(metrics.lastWriteStartDelayMs >= 0 && metrics.lastWriteStartDelayMs <= 100, "write should start within 100ms");
    const auto value = gateway.Read({2103, 1, 1, DataStyle::Raw});
    Check(value.displayText == L"6789", "coordinator write should update backend value");
}

/**
 * @brief Verify read-only write attempts are counted with their error code.
 */
void TestUpdateCoordinatorRecordsReadOnlyWriteError()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2000, 1, 0, DataStyle::Raw}, L"99");
    Check(WaitForWriteCount(coordinator, 1), "coordinator should complete read-only write attempt");
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.writeCompletedCount == 1, "coordinator should count failed write completion");
    Check(metrics.lastWriteErrorCode == BridgeError::ReadOnly, "coordinator should preserve read-only write error");
}

/**
 * @brief Verify schedule mutation writes update common and dedicated metrics.
 */
void TestUpdateCoordinatorRecordsScheduleMutationMetrics()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2104, 1, 3, DataStyle::Raw}, EncodeScheduleAddValue({1, 3, 1234, L"METRIC-ITEM"}));
    coordinator.RequestWrite({2105, 1, 3, DataStyle::Raw}, L"1");
    coordinator.RequestWrite({2104, 101, 1, DataStyle::Raw}, EncodeScheduleAddValue({101, 1, 1234, L"BAD"}));
    Check(WaitForWriteCount(coordinator, 3), "coordinator should complete schedule mutation writes");
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.writeCompletedCount == 3, "schedule mutations should count as completed writes");
    Check(metrics.scheduleAddCompletedCount == 2, "schedule add metric should count add completions");
    Check(metrics.scheduleDeleteCompletedCount == 1, "schedule delete metric should count delete completions");
    Check(metrics.lastWriteErrorCode == BridgeError::InvalidSubDataId, "last invalid mutation should expose write error");
    Check(metrics.lastScheduleMutationErrorCode == BridgeError::InvalidSubDataId, "schedule mutation should retain last mutation error");
}

/**
 * @brief Verify history loading can be cancelled and reported.
 */
void TestUpdateCoordinatorCancelsHistoryLoad()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(coordinator.StartHistoryLoad({3}), "valid history request should start");
    Check(WaitForHistoryProgress(coordinator), "history should report progress before cancellation");
    coordinator.CancelHistoryLoad();
    Check(WaitForHistoryStop(coordinator), "history should stop after cancellation");
    coordinator.Stop();

    const auto snapshot = coordinator.Snapshot();
    const auto metrics = coordinator.Metrics();
    Check(!snapshot.historyRunning, "history should not be running after cancellation");
    Check(snapshot.historyCancelled, "snapshot should mark cancellation");
    Check(snapshot.historyStatusText == L"履歴中断", "snapshot should expose cancel status text");
    Check(metrics.historyCancelCount == 1, "metrics should count history cancellations");
}

/**
 * @brief Verify completed history loads cap retained records.
 */
void TestUpdateCoordinatorCapsHistoryRecords()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(coordinator.StartHistoryLoad({1}), "one day history should start");
    Check(WaitForHistoryStop(coordinator), "one day history should finish");
    coordinator.Stop();

    const auto snapshot = coordinator.Snapshot();
    const auto metrics = coordinator.Metrics();
    Check(snapshot.historyProgress == 100, "completed history should be 100 percent");
    Check(snapshot.historyStatusText == L"履歴取得完了", "completed history should expose completion status");
    Check(snapshot.historyRecords.size() == 500, "snapshot should retain latest 500 history records");
    Check(snapshot.historyRecords.front().recordIndex == 500, "history cap should drop earliest records");
    Check(snapshot.historyRecords.back().recordIndex == 999, "history cap should retain latest record");
    Check(metrics.historyReadCount == 1000, "metrics should count history reads");
    Check(metrics.historyErrorCount == 0, "mock history should not record read errors");
    Check(metrics.historyLastErrorCode == BridgeError::Ok, "mock history should finish with ok error code");
}

/**
 * @brief Verify invalid history requests are rejected before backend access.
 */
void TestHistoryLoadRejectsInvalidRequestBeforeCommunication()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(!coordinator.StartHistoryLoad({0}), "invalid history request should be rejected");
    coordinator.Stop();

    const auto snapshot = coordinator.Snapshot();
    const auto metrics = coordinator.Metrics();
    Check(!snapshot.historyRunning, "invalid history request should not start");
    Check(snapshot.historyStatusText == L"履歴日数が不正です", "invalid history request should expose status");
    Check(metrics.historyReadCount == 0, "invalid history request should not read backend");
}

/**
 * @brief Verify history work does not block user-write responsiveness.
 */
void TestHistoryLoadKeepsWritePriorityResponsive()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(coordinator.StartHistoryLoad({3}), "history should start");
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"2468");
    Check(WaitForWriteCount(coordinator, 1), "write should complete while history is running");
    coordinator.CancelHistoryLoad();
    Check(WaitForHistoryStop(coordinator), "history should stop after cancellation");
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.writeCompletedCount == 1, "write should complete during history");
    Check(metrics.lastWriteErrorCode == BridgeError::Ok, "write should succeed during history");
    Check(metrics.lastWriteStartDelayMs >= 0 && metrics.lastWriteStartDelayMs <= 100, "history should not delay write start over 100ms");
}

/**
 * @brief Verify queued work priority order from critical through history.
 */
void TestPriorityQueueOrdersCriticalBeforeNormalAndHistory()
{
    PrioritizedWorkQueue queue;
    queue.Push({WorkPriority::History, 4, L"history"});
    queue.Push({WorkPriority::Normal, 3, L"normal"});
    queue.Push({WorkPriority::Critical, 2, L"critical"});
    queue.Push({WorkPriority::UserWrite, 1, L"write"});

    Check(queue.Pop().priority == WorkPriority::Critical, "critical should be first");
    Check(queue.Pop().priority == WorkPriority::UserWrite, "write should be second");
    Check(queue.Pop().priority == WorkPriority::Normal, "normal should be third");
    Check(queue.Pop().priority == WorkPriority::History, "history should be fourth");
}

/**
 * @brief Verify station snapshots expose container summary data.
 */
void TestScreenSnapshotBuildsContainerSummary()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto station = BuildStationSnapshot(gateway, 1);
    Check(station.containers.size() == 100, "station snapshot should include 100 containers");
    Check(station.selected.containerNo == 1, "selected container should match requested id");
    Check(!station.selected.items.empty(), "selected container should expose items");
}

/**
 * @brief Verify fixed station layout maps 100 containers into 5x20 cells.
 */
void TestStationLayoutModelUsesFixedFiveColumnPlacement()
{
    StationSnapshot snapshot;
    snapshot.containers.reserve(100);
    for (int containerNo = 1; containerNo <= 100; ++containerNo) {
        ContainerSummary container;
        container.containerNo = containerNo;
        container.containerName = L"CNT-" + std::to_wstring(containerNo);
        container.state = containerNo == 29 ? L"コンテナなし" : L"空";
        container.missing = container.state == L"コンテナなし";
        snapshot.containers.push_back(container);
    }

    const auto layout = BuildStationLayoutModel(snapshot, 21);
    Check(layout.columnCount == 5, "station layout should use five columns");
    Check(layout.rowsPerColumn == 20, "station layout should use twenty rows per column");
    Check(layout.cells.size() == 100, "station layout should expose 100 cells");

    const auto& first = layout.cells[0];
    Check(first.containerNo == 1, "container 1 should be first cell");
    Check(first.column == 0 && first.row == 0, "container 1 should be column 0 row 0");
    Check(first.kind == StationLayoutKind::LeftSemiCircle, "first column should be left semicircle");
    Check(first.displayText == L"1", "cell display text should be container number");

    const auto& twentyFirst = layout.cells[20];
    Check(twentyFirst.containerNo == 21, "container 21 should start the second column");
    Check(twentyFirst.column == 1 && twentyFirst.row == 0, "container 21 should be column 1 row 0");
    Check(twentyFirst.kind == StationLayoutKind::Straight, "second column should be straight");
    Check(twentyFirst.selected, "selected container should be marked");

    const auto& missing = layout.cells[28];
    Check(missing.containerNo == 29, "container 29 should be present");
    Check(missing.missing, "container without state should be marked missing");
    Check(missing.state == L"コンテナなし", "cell should preserve container state");

    const auto& last = layout.cells[99];
    Check(last.containerNo == 100, "container 100 should be last cell");
    Check(last.column == 4 && last.row == 19, "container 100 should be column 4 row 19");
    Check(last.kind == StationLayoutKind::RightSemiCircle, "last column should be right semicircle");
}

/**
 * @brief Verify normal critical values build the common status summary.
 */
void TestStatusSummaryShowsNormalCriticalState()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    UpdateSnapshot snapshot;
    for (size_t index = 0; index < catalog.CriticalKeys().size(); ++index) {
        snapshot.criticalValues.push_back({index == 0 ? L"正常" : L"VALUE-" + std::to_wstring(index), BridgeError::Ok, {}, false});
    }

    SchedulerMetrics metrics;
    metrics.criticalCycles = 10;
    metrics.criticalDeadlineMisses = 0;
    metrics.normalCycles = 2;
    const StatusContext context{L"システム", L"2026/05/21 07:00:00", L"operator"};

    const auto summary = BuildStatusSummary(catalog, snapshot, metrics, context);
    Check(summary.businessStateText == L"正常", "status summary should use dataId 1000 as business state");
    Check(summary.criticalErrorCount == 0, "normal critical values should not count errors");
    Check(!summary.hasCriticalError, "normal critical values should not set critical error flag");
    Check(summary.criticalItems.size() == 20, "status summary should expose all critical items");
    Check(summary.criticalItems[0].name == L"重要情報 1000", "critical item should use catalog name");
    Check(summary.displayText.find(L"日時: 2026/05/21 07:00:00 / ユーザー: operator / 画面: システム / 業務状態: 正常 / 重要: 正常") != std::wstring::npos,
          "status summary first line should include context and normal state");
    Check(summary.displayText.find(L"\r\n重要更新: 10 / 期限超過: 0 / 通常更新: 2") != std::wstring::npos,
          "status summary second line should include update metrics");
}

/**
 * @brief Verify stale, error, and missing critical values are counted and exposed.
 */
void TestStatusSummaryCountsCriticalErrorsAndMissingValues()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    UpdateSnapshot snapshot;
    snapshot.criticalValues.push_back({L"", BridgeError::Ok, {}, false});
    snapshot.criticalValues.push_back({L"", BridgeError::Timeout, {}, true});

    SchedulerMetrics metrics;
    const StatusContext context{L"保守", L"2026/05/21 07:01:00", L"operator"};

    const auto summary = BuildStatusSummary(catalog, snapshot, metrics, context);
    Check(summary.businessStateText == L"状態不明", "empty dataId 1000 should make business state unknown");
    Check(summary.hasCriticalError, "critical errors should set critical error flag");
    Check(summary.criticalErrorCount == 19, "one explicit error plus eighteen missing values should count as errors");
    Check(summary.criticalItems.size() == 20, "missing critical values should still create status items");
    Check(summary.criticalItems[1].name == L"重要情報 1001", "error item should keep catalog name");
    Check(summary.criticalItems[1].errorCode == BridgeError::Timeout, "error item should preserve backend error");
    Check(summary.criticalItems[1].stale, "error item should preserve stale state");
    Check(summary.criticalItems[2].displayText == L"未取得", "missing critical item should show not-yet-read text");
    Check(summary.criticalItems[2].errorCode == BridgeError::InternalError, "missing critical item should use internal error code");
    Check(summary.criticalItems[2].stale, "missing critical item should be stale");
    Check(summary.displayText.find(L"重要: 異常19件") != std::wstring::npos, "status line should include critical error count");
}

/**
 * @brief Verify write, schedule-write, and history details are appended to the status summary.
 */
void TestStatusSummaryIncludesWriteAndHistoryDetails()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    UpdateSnapshot snapshot;
    for (size_t index = 0; index < catalog.CriticalKeys().size(); ++index) {
        snapshot.criticalValues.push_back({index == 0 ? L"正常" : L"VALUE-" + std::to_wstring(index), BridgeError::Ok, {}, false});
    }
    snapshot.historyStatusText = L"履歴取得中";
    snapshot.historyProgress = 42;

    SchedulerMetrics metrics;
    metrics.criticalCycles = 5;
    metrics.normalCycles = 1;
    metrics.lastWriteStartDelayMs = 12;
    metrics.writeCompletedCount = 3;
    metrics.lastWriteErrorCode = BridgeError::Ok;
    metrics.scheduleOrderWriteCompletedCount = 2;
    metrics.scheduleAddCompletedCount = 1;
    metrics.scheduleDeleteCompletedCount = 1;
    metrics.lastScheduleMutationErrorCode = BridgeError::Ok;
    metrics.historyReadCount = 77;
    metrics.historyErrorCount = 2;
    metrics.historyLastErrorCode = BridgeError::Timeout;
    const StatusContext context{L"スケジュール", L"2026/05/21 07:02:00", L"operator"};

    const auto summary = BuildStatusSummary(catalog, snapshot, metrics, context);
    Check(summary.displayText.find(L"最終Write開始遅延: 12ms / Write完了: 3 / 最終Write結果: OK") != std::wstring::npos,
          "status summary should include write metrics");
    Check(summary.displayText.find(L"予定順序: 2 / 予定追加: 1 / 予定削除: 1 / 予定Write結果: OK") != std::wstring::npos,
          "status summary should include schedule write metrics");
    Check(summary.displayText.find(L"履歴: 履歴取得中 42% / 履歴Read: 77 / 履歴エラー: 2(タイムアウト)") != std::wstring::npos,
          "status summary should include history metrics and error code");
}

} // namespace

/**
 * @brief Run all core tests and print a compact pass/fail result.
 * @return 0 on success, 1 when any test throws.
 */
int wmain()
{
    const std::vector<void (*)()> tests = {
        TestCatalogDefinesCriticalDataAndStyles,
        TestCatalogDefinesScheduleMutationIds,
        TestCatalogLoadsFromJsonFile,
        TestCatalogRejectsInvalidJsonFile,
        TestCatalogRejectsUnknownStyleName,
        TestBridgeFactoryCreatesMockBridge,
        TestMockBridgeFormatsValuesAndRejectsInvalidStyle,
        TestGatewayMarksErrorsAsStale,
        TestFunctionActionsReflectSelection,
        TestFunctionSlotFromVirtualKey,
        TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection,
        TestSystemFunctionActionsReflectHistoryRunning,
        TestGridModelKeepsCellKinds,
        TestScheduleGridBindsRowsToContainerItems,
        TestScheduleGridSortsByOutboundOrder,
        TestMockWriteUpdatesScheduleOrderReadback,
        TestMockScheduleAddAndDeleteReflectInGrid,
        TestBuildScheduleMoveUpWrites,
        TestHistoryRequestValidation,
        TestHistoryKeyGenerationUsesOutboundHistoryId,
        TestUpdateCoordinatorRecordsSuccessfulWriteMetrics,
        TestUpdateCoordinatorRecordsReadOnlyWriteError,
        TestUpdateCoordinatorRecordsScheduleMutationMetrics,
        TestUpdateCoordinatorCancelsHistoryLoad,
        TestUpdateCoordinatorCapsHistoryRecords,
        TestHistoryLoadRejectsInvalidRequestBeforeCommunication,
        TestHistoryLoadKeepsWritePriorityResponsive,
        TestPriorityQueueOrdersCriticalBeforeNormalAndHistory,
        TestScreenSnapshotBuildsContainerSummary,
        TestStationLayoutModelUsesFixedFiveColumnPlacement,
        TestStatusSummaryShowsNormalCriticalState,
        TestStatusSummaryCountsCriticalErrorsAndMissingValues,
        TestStatusSummaryIncludesWriteAndHistoryDetails,
    };

    try {
        for (const auto test : tests) {
            test();
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "PASS: " << tests.size() << " core tests\n";
    return 0;
}
