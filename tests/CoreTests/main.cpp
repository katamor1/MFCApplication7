#include "DataCatalog.h"
#include "DataGateway.h"
#include "FunctionBarModel.h"
#include "GridModel.h"
#include "MockBackendBridge.h"
#include "NavigationModel.h"
#include "ScheduleMutationModel.h"
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
 * @brief Verify mock load-profile and latency CLI options are parsed.
 */
void TestBridgeFactoryParsesMockLoadAndLatencyOptions()
{
    const auto options = ParseBridgeFactoryOptions(L"/Bridge:Mock /MockProfile:MaxLoad /MockCriticalReadDelayMs:1 /MockNormalReadDelayMs:2 /MockHistoryReadDelayMs:3 /MockWriteDelayMs:4");
    Check(options.bridgeMode == BridgeMode::InProcessMock, "mock bridge option should keep in-process mode");
    Check(options.mockLoadProfile == MockLoadProfile::MaxLoad, "mock profile should parse max-load profile");
    Check(options.mockLatencyOptions.criticalReadDelayMs == 1, "critical delay should parse");
    Check(options.mockLatencyOptions.normalReadDelayMs == 2, "normal delay should parse");
    Check(options.mockLatencyOptions.historyReadDelayMs == 3, "history delay should parse");
    Check(options.mockLatencyOptions.writeDelayMs == 4, "write delay should parse");
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
 * @brief Verify max-load mock profile exposes 100 containers and 1000 schedule items.
 */
void TestMockMaxLoadProfileBuildsThousandScheduleRows()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog, MockLoadProfile::MaxLoad);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto missingContainerItemCount = gateway.Read({2003, 29, 0, DataStyle::Raw});
    Check(missingContainerItemCount.errorCode == BridgeError::Ok, "max-load item count should read");
    Check(missingContainerItemCount.displayText == L"10", "max-load should expose ten items even for default missing containers");

    const auto station = BuildStationSnapshot(gateway, 29);
    Check(station.containers.size() == 100, "max-load station should still expose 100 containers");
    Check(!station.containers[28].missing, "max-load should not mark synthetic containers as missing");

    const auto schedule = BuildScheduleGrid(gateway);
    Check(schedule.RowCount() == 1000, "max-load schedule should expose 1000 item rows");
    Check(schedule.Rows().front().binding.containerNo == 1 && schedule.Rows().front().binding.itemNo == 1,
          "max-load schedule should keep container/item binding");
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
 * @brief Verify default navigation items and their one/two-column layouts.
 */
void TestNavigationModelBuildsDefaultCells()
{
    const auto items = BuildDefaultNavigationItems();
    Check(items.size() == 5, "default navigation should expose five screens");
    Check(items[0].screen == MainScreenId::Station && items[0].shortLabel == L"ST", "station should be first navigation item");
    Check(items[1].screen == MainScreenId::ContainerList && items[1].shortLabel == L"LIST", "container list should be second navigation item");
    Check(items[2].screen == MainScreenId::Schedule && items[2].shortLabel == L"SCH", "schedule should be third navigation item");
    Check(items[3].screen == MainScreenId::System && items[3].shortLabel == L"SYS", "system should be fourth navigation item");
    Check(items[4].screen == MainScreenId::Maintenance && items[4].shortLabel == L"MNT", "maintenance should be fifth navigation item");

    Check(NavigationLabelForScreen(items, MainScreenId::Station) == L"コンテナステーション", "station navigation label should be Japanese");
    Check(NavigationLabelForScreen(items, MainScreenId::ContainerList) == L"コンテナ一覧", "container list navigation label should be Japanese");
    Check(NavigationLabelForScreen(items, MainScreenId::Schedule) == L"コンテナスケジュール", "schedule navigation label should be Japanese");
    Check(NavigationLabelForScreen(items, MainScreenId::System) == L"システム", "system navigation label should be Japanese");
    Check(NavigationLabelForScreen(items, MainScreenId::Maintenance) == L"コンテナ保守", "maintenance navigation label should be Japanese");

    const auto collapsed = BuildNavigationCells(items, MainScreenId::Schedule, false);
    Check(collapsed.size() == 5, "collapsed navigation should keep all items");
    for (int index = 0; index < 5; ++index) {
        Check(collapsed[static_cast<size_t>(index)].column == 0, "collapsed navigation should use one column");
        Check(collapsed[static_cast<size_t>(index)].row == index, "collapsed navigation row should follow item order");
    }
    Check(collapsed[2].selected, "collapsed navigation should mark current screen");
    Check(!collapsed[0].selected && !collapsed[4].selected, "collapsed navigation should not mark other screens");

    const auto expanded = BuildNavigationCells(items, MainScreenId::System, true);
    Check(expanded[0].column == 0 && expanded[0].row == 0, "expanded station cell should be left top");
    Check(expanded[1].column == 1 && expanded[1].row == 0, "expanded list cell should be right top");
    Check(expanded[2].column == 0 && expanded[2].row == 1, "expanded schedule cell should be left second row");
    Check(expanded[3].column == 1 && expanded[3].row == 1, "expanded system cell should be right second row");
    Check(expanded[4].column == 0 && expanded[4].row == 2, "expanded maintenance cell should be left third row");
    Check(expanded[3].selected, "expanded navigation should mark selected screen");
}

/**
 * @brief Verify schedule actions expose mutation buttons according to schedule state.
 */
void TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection()
{
    const auto none = BuildScheduleFunctionActions(false, false, false, false, false);
    Check(none.size() == 8, "schedule actions should always expose 8 slots");
    Check(!none[0].enabled, "schedule details should be disabled without selection");
    Check(!none[1].enabled, "schedule order change should be disabled without selection");
    Check(none[2].enabled && none[2].id == L"add", "schedule add should be enabled without selection");
    Check(!none[3].enabled, "schedule delete should be disabled without selection");
    Check(!none[4].enabled, "schedule move-up should be disabled without selection");
    Check(!none[5].enabled && none[5].id == L"renumber", "schedule renumber should need rows");
    Check(!none[6].enabled && none[6].id == L"undo", "schedule undo should need history");

    const auto selected = BuildScheduleFunctionActions(true, true, true, true, false);
    Check(selected[0].enabled && selected[0].id == L"details", "schedule details should be enabled with selection");
    Check(selected[1].enabled && selected[1].id == L"order-change", "schedule F2 should edit order with selection");
    Check(selected[1].label == L"順序変更", "schedule F2 should be labeled for order change");
    Check(selected[2].enabled && selected[2].id == L"add", "schedule F3 should add schedule rows");
    Check(selected[3].enabled && selected[3].id == L"delete", "schedule F4 should delete selected rows");
    Check(selected[4].enabled && selected[4].id == L"move-up", "schedule F5 should move selected rows up");
    Check(selected[5].enabled && selected[5].id == L"renumber", "schedule F6 should renumber visible rows");
    Check(selected[5].label == L"再採番", "schedule F6 should be labeled for renumber");
    Check(selected[6].enabled && selected[6].id == L"undo", "schedule F7 should undo completed mutations");

    const auto first = BuildScheduleFunctionActions(true, false, true, false, false);
    Check(!first[4].enabled, "schedule F5 should be disabled for first row");
    Check(first[5].enabled, "schedule F6 should be enabled when rows exist");

    const auto pending = BuildScheduleFunctionActions(true, true, true, true, true);
    Check(pending[0].enabled, "schedule details should stay available while mutation is pending");
    Check(!pending[1].enabled, "schedule F2 should be disabled while mutation is pending");
    Check(!pending[2].enabled, "schedule F3 should be disabled while mutation is pending");
    Check(!pending[3].enabled, "schedule F4 should be disabled while mutation is pending");
    Check(!pending[4].enabled, "schedule F5 should be disabled while mutation is pending");
    Check(!pending[5].enabled, "schedule F6 should be disabled while mutation is pending");
    Check(!pending[6].enabled, "schedule F7 should be disabled while mutation is pending");
}

/**
 * @brief Verify grid rows preserve their cell editor kinds.
 */
void TestGridModelKeepsCellKinds()
{
    GridModel grid;
    grid.SetColumns({L"Name", L"Qty", L"Mode", L"Enabled"});
    grid.AddRow({GridCell::Text(L"Item A", CellKind::ReadOnlyText),
                 GridCell::Text(L"3", CellKind::Spin),
                 GridCell::Text(L"A", CellKind::ComboBox, {L"A", L"B"}),
                 GridCell::Text(L"true", CellKind::CheckBox)});

    Check(grid.ColumnCount() == 4, "grid should keep column count");
    Check(grid.RowCount() == 1, "grid should keep row count");
    Check(grid.Rows()[0].cells[1].kind == CellKind::Spin, "grid should preserve spin cell kind");
    Check(grid.Rows()[0].cells[2].kind == CellKind::ComboBox, "grid should preserve combo cell kind");
    Check(grid.Rows()[0].cells[2].options.size() == 2, "grid should preserve combo options");
    Check(grid.Rows()[0].cells[3].kind == CellKind::CheckBox, "grid should preserve checkbox cell kind");
}

/**
 * @brief Verify cell kinds and values follow the grid edit policy.
 */
void TestGridEditPolicyValidatesCellKinds()
{
    Check(!IsEditableCellKind(CellKind::ReadOnlyText), "read-only text should not be editable");
    Check(IsEditableCellKind(CellKind::Text), "text cells should be editable");
    Check(IsEditableCellKind(CellKind::Spin), "spin cells should be editable");
    Check(IsEditableCellKind(CellKind::ComboBox), "combo cells should be editable");
    Check(IsEditableCellKind(CellKind::RadioButton), "radio cells should be editable");
    Check(IsEditableCellKind(CellKind::CheckBox), "checkbox cells should be editable");

    Check(ValidateGridEditValue(GridCell::Text(L"", CellKind::Text), L"").valid, "text should allow empty value");
    Check(ValidateGridEditValue(GridCell::Text(L"1", CellKind::Spin), L"-12").valid, "spin should allow signed integer");
    Check(!ValidateGridEditValue(GridCell::Text(L"1", CellKind::Spin), L"").valid, "spin should reject empty value");
    Check(!ValidateGridEditValue(GridCell::Text(L"1", CellKind::Spin), L"12x").valid, "spin should reject non-integer value");

    const auto combo = GridCell::Text(L"A", CellKind::ComboBox, {L"A", L"B"});
    Check(ValidateGridEditValue(combo, L"B").valid, "combo should allow option value");
    Check(!ValidateGridEditValue(combo, L"C").valid, "combo should reject value outside options");

    const auto radio = GridCell::Text(L"R1", CellKind::RadioButton, {L"R1", L"R2"});
    Check(ValidateGridEditValue(radio, L"R2").valid, "radio should allow option value");
    Check(!ValidateGridEditValue(radio, L"R3").valid, "radio should reject value outside options");

    Check(ValidateGridEditValue(GridCell::Text(L"false", CellKind::CheckBox), L"true").valid, "checkbox should allow true");
    Check(ValidateGridEditValue(GridCell::Text(L"false", CellKind::CheckBox), L"false").valid, "checkbox should allow false");
    Check(!ValidateGridEditValue(GridCell::Text(L"false", CellKind::CheckBox), L"yes").valid, "checkbox should reject other values");
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
    Check(firstRow.cells.size() == 5, "schedule row should keep five cells");
    Check(firstRow.cells[ScheduleGridColumn::ItemName].kind == CellKind::Text, "schedule item name cell should be editable text");
    Check(firstRow.cells[ScheduleGridColumn::OutboundStart].kind == CellKind::Text, "schedule outbound start cell should be editable text");
    Check(firstRow.cells[ScheduleGridColumn::OutboundEnd].kind == CellKind::Text, "schedule outbound end cell should be editable text");
    Check(firstRow.cells[ScheduleGridColumn::Order].kind == CellKind::Spin, "schedule order cell should remain spin kind");
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
        const int currentOrder = std::stoi(row.cells[ScheduleGridColumn::Order].text);
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
            if (order != nullptr && row.cells.size() > ScheduleGridColumn::Order) {
                *order = row.cells[ScheduleGridColumn::Order].text;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief Find one read-only detail row by label/value.
 */
bool HasDetailRow(const ReadOnlyDetailModel& detail, const std::wstring& label, const std::wstring& value)
{
    for (const auto& row : detail.rows) {
        if (row.label == label && row.value == value) {
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
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫開始予定", L"出庫終了予定", L"順序"});
    grid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A", CellKind::Text), GridCell::Text(L"start-a", CellKind::Text), GridCell::Text(L"end-a", CellKind::Text), GridCell::Text(L"10", CellKind::Spin)}, {1, 1});
    grid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B", CellKind::Text), GridCell::Text(L"start-b", CellKind::Text), GridCell::Text(L"end-b", CellKind::Text), GridCell::Text(L"20", CellKind::Spin)}, {2, 1});

    const auto writes = BuildScheduleMoveUpWrites(grid, 1);
    Check(writes.size() == 2, "move-up should emit two order writes");
    Check(writes[0].key == DataKey{2103, 2, 1, DataStyle::Raw}, "selected row should receive previous order");
    Check(writes[0].value == L"10", "selected row should write previous order value");
    Check(writes[1].key == DataKey{2103, 1, 1, DataStyle::Raw}, "previous row should receive selected order");
    Check(writes[1].value == L"20", "previous row should write selected order value");

    Check(BuildScheduleMoveUpWrites(grid, 0).empty(), "first row should not move up");
    Check(BuildScheduleMoveUpWrites(grid, -1).empty(), "negative row should not move up");

    GridModel invalidGrid;
    invalidGrid.SetColumns({L"コンテナ", L"品目名", L"出庫開始予定", L"出庫終了予定", L"順序"});
    invalidGrid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A", CellKind::Text), GridCell::Text(L"start-a", CellKind::Text), GridCell::Text(L"end-a", CellKind::Text), GridCell::Text(L"bad", CellKind::Spin)}, {1, 1});
    invalidGrid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B", CellKind::Text), GridCell::Text(L"start-b", CellKind::Text), GridCell::Text(L"end-b", CellKind::Text), GridCell::Text(L"20", CellKind::Spin)}, {2, 1});
    Check(BuildScheduleMoveUpWrites(invalidGrid, 1).empty(), "non-numeric order should not move up");
}

/**
 * @brief Verify renumber helper rewrites visible order values in 10-step order.
 */
void TestBuildScheduleRenumberWrites()
{
    GridModel grid;
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫開始予定", L"出庫終了予定", L"順序"});
    grid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A", CellKind::Text), GridCell::Text(L"start-a", CellKind::Text), GridCell::Text(L"end-a", CellKind::Text), GridCell::Text(L"10", CellKind::Spin)}, {1, 1});
    grid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B", CellKind::Text), GridCell::Text(L"start-b", CellKind::Text), GridCell::Text(L"end-b", CellKind::Text), GridCell::Text(L"25", CellKind::Spin)}, {2, 1});
    grid.AddRow({GridCell::Text(L"3"), GridCell::Text(L"C", CellKind::Text), GridCell::Text(L"start-c", CellKind::Text), GridCell::Text(L"end-c", CellKind::Text), GridCell::Text(L"bad", CellKind::Spin)}, {3, 1});

    const auto writes = BuildScheduleRenumberWrites(grid);
    Check(writes.size() == 2, "renumber should skip rows already holding the target value");
    Check(writes[0].key == DataKey{2103, 2, 1, DataStyle::Raw}, "renumber second row should target 2103 raw");
    Check(writes[0].value == L"20", "renumber second row should write 20");
    Check(writes[1].key == DataKey{2103, 3, 1, DataStyle::Raw}, "renumber third row should target 2103 raw");
    Check(writes[1].value == L"30", "renumber third row should write 30");

    const auto restore = CaptureScheduleOrderRestoreWrites(grid, {0, 2});
    Check(restore.size() == 2, "restore capture should emit one write per requested valid row");
    Check(restore[0].key == DataKey{2103, 1, 1, DataStyle::Raw}, "restore should target first row order");
    Check(restore[0].value == L"10", "restore should preserve first row order text");
    Check(restore[1].key == DataKey{2103, 3, 1, DataStyle::Raw}, "restore should target third row order");
    Check(restore[1].value == L"bad", "restore should preserve non-empty original order text");
}

/**
 * @brief Verify duplicate-order detection can ignore the edited row.
 */
void TestScheduleDuplicateOrderDetection()
{
    GridModel grid;
    grid.SetColumns({L"コンテナ", L"品目名", L"出庫開始予定", L"出庫終了予定", L"順序"});
    grid.AddRow({GridCell::Text(L"1"), GridCell::Text(L"A", CellKind::Text), GridCell::Text(L"start-a", CellKind::Text), GridCell::Text(L"end-a", CellKind::Text), GridCell::Text(L"10", CellKind::Spin)}, {1, 1});
    grid.AddRow({GridCell::Text(L"2"), GridCell::Text(L"B", CellKind::Text), GridCell::Text(L"start-b", CellKind::Text), GridCell::Text(L"end-b", CellKind::Text), GridCell::Text(L"20", CellKind::Spin)}, {2, 1});

    const auto duplicate = FindDuplicateScheduleOrder(grid, 20, {});
    Check(duplicate.found, "existing schedule order should be detected as duplicate");
    Check(duplicate.row == 1, "duplicate result should include row index");
    Check(duplicate.binding.containerNo == 2 && duplicate.binding.itemNo == 1, "duplicate result should include binding");

    const auto skipped = FindDuplicateScheduleOrder(grid, 20, {2, 1});
    Check(!skipped.found, "duplicate detection should skip the current binding");
    Check(!FindDuplicateScheduleOrder(grid, 99, {}).found, "unknown order should not be duplicate");
    Check(HasScheduleRowBinding(grid, {1, 1}), "row binding lookup should find existing rows");
    Check(!HasScheduleRowBinding(grid, {1, 3}), "row binding lookup should reject missing rows");
}

/**
 * @brief Verify undo stack is LIFO and bounded to the configured maximum.
 */
void TestScheduleUndoStack()
{
    ScheduleUndoStack stack(20);
    for (int index = 0; index < 21; ++index) {
        ScheduleUndoEntry entry;
        entry.label = L"undo-" + std::to_wstring(index);
        entry.writes = {{{2103, index + 1, 1, DataStyle::Raw}, std::to_wstring(index)}};
        stack.Push(std::move(entry));
    }

    Check(stack.Size() == 20, "undo stack should drop the oldest item above max size");
    auto latest = stack.Pop();
    Check(latest.has_value(), "undo stack should pop latest item");
    Check(latest->label == L"undo-20", "undo stack should pop in LIFO order");
    auto next = stack.Pop();
    Check(next.has_value() && next->label == L"undo-19", "undo stack should continue LIFO order");

    stack.Restore(std::move(*latest));
    auto restored = stack.Pop();
    Check(restored.has_value() && restored->label == L"undo-20", "undo restore should put failed undo back on top");
}

/**
 * @brief Verify add/delete operations can build inverse writes for Undo.
 */
void TestScheduleUndoWriteBuilders()
{
    const auto cellUndo = BuildScheduleCellRestoreWrites({1, 2}, ScheduleGridColumn::OutboundStart, L"");
    Check(cellUndo.size() == 1, "cell undo should allow restoring an empty editable text value");
    Check(cellUndo[0].key == DataKey{2102, 1, 2, DataStyle::Raw}, "cell undo should target the edited schedule column");
    Check(cellUndo[0].value.empty(), "cell undo should preserve the original empty text");

    const ScheduleAddRequest request{1, 3, 777, L"ADDED-ITEM"};
    const auto addUndo = BuildScheduleAddUndoWrites(request);
    Check(addUndo.size() == 1, "add undo should emit one delete write");
    Check(addUndo[0].key == DataKey{2105, 1, 3, DataStyle::Raw}, "add undo should target schedule delete id");
    Check(addUndo[0].value == L"1", "add undo should use delete command value");

    GridRow deletedRow;
    deletedRow.binding = {1, 3};
    deletedRow.cells = {
        GridCell::Text(L"1"),
        GridCell::Text(L"ADDED-ITEM", CellKind::Text),
        GridCell::Text(L"2026/05/23 09:00", CellKind::Text),
        GridCell::Text(L"2026/05/23 09:30", CellKind::Text),
        GridCell::Text(L"777", CellKind::Spin),
    };

    const auto deleteUndo = BuildScheduleDeleteUndoWrites(deletedRow);
    Check(deleteUndo.size() == 3, "delete undo should restore add payload and editable dates");
    Check(deleteUndo[0].key == DataKey{2104, 1, 3, DataStyle::Raw}, "delete undo should target schedule add id");
    Check(deleteUndo[0].value == EncodeScheduleAddValue(request), "delete undo should encode original order and item name");
    Check(deleteUndo[1].key == DataKey{2102, 1, 3, DataStyle::Raw}, "delete undo should restore outbound start");
    Check(deleteUndo[1].value == L"2026/05/23 09:00", "delete undo should preserve outbound start value");
    Check(deleteUndo[2].key == DataKey{3000, 1, 3, DataStyle::Raw}, "delete undo should restore outbound end");
    Check(deleteUndo[2].value == L"2026/05/23 09:30", "delete undo should preserve outbound end value");
}

/**
 * @brief Verify in-cell schedule edits map only valid editable cells to schedule writes.
 */
void TestBuildScheduleCellEditWrites()
{
    const auto itemNameWrites = BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::ItemName, CellKind::Text, L"UPDATED-ITEM");
    Check(itemNameWrites.size() == 1, "schedule item-name edit should emit one write");
    Check(itemNameWrites[0].key == DataKey{2100, 1, 2, DataStyle::Raw}, "schedule item-name edit should target 2100 raw key");
    Check(itemNameWrites[0].value == L"UPDATED-ITEM", "schedule item-name edit should preserve edited value");

    const auto startWrites = BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::OutboundStart, CellKind::Text, L"2026/05/23 09:00");
    Check(startWrites.size() == 1, "schedule outbound-start edit should emit one write");
    Check(startWrites[0].key == DataKey{2102, 1, 2, DataStyle::Raw}, "schedule outbound-start edit should target 2102 raw key");

    const auto endWrites = BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::OutboundEnd, CellKind::Text, L"2026/05/23 09:30");
    Check(endWrites.size() == 1, "schedule outbound-end edit should emit one write");
    Check(endWrites[0].key == DataKey{3000, 1, 2, DataStyle::Raw}, "schedule outbound-end edit should target 3000 raw key");

    const auto orderWrites = BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Spin, L"4321");
    Check(orderWrites.size() == 1, "schedule order edit should emit one write");
    Check(orderWrites[0].key == DataKey{2103, 1, 2, DataStyle::Raw}, "schedule order edit should target 2103 raw key");
    Check(orderWrites[0].value == L"4321", "schedule order edit should preserve edited value");

    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Container, CellKind::Text, L"2").empty(), "container column should not write");
    Check(BuildScheduleCellEditWrites({0, 2}, ScheduleGridColumn::ItemName, CellKind::Text, L"UPDATED-ITEM").empty(), "invalid container binding should not write");
    Check(BuildScheduleCellEditWrites({1, 0}, ScheduleGridColumn::ItemName, CellKind::Text, L"UPDATED-ITEM").empty(), "invalid item binding should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::ItemName, CellKind::ReadOnlyText, L"UPDATED-ITEM").empty(), "read-only text cell should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::ItemName, CellKind::Text, L"").empty(), "empty text edit should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Text, L"4321").empty(), "order text cell should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Spin, L"").empty(), "empty order should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Spin, L"bad").empty(), "non-numeric order should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Spin, L"0").empty(), "zero order should not write");
    Check(BuildScheduleCellEditWrites({1, 2}, ScheduleGridColumn::Order, CellKind::Spin, L"10000").empty(), "order above V1 maximum should not write");
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
 * @brief Verify on-time critical cycles keep the planned cadence.
 */
void TestComputeNextPeriodicWakeKeepsNormalCadence()
{
    using clock = std::chrono::steady_clock;
    const auto base = clock::time_point{};
    const auto next = ComputeNextPeriodicWake(base, base + std::chrono::milliseconds(10), std::chrono::milliseconds(33));
    Check(next == base + std::chrono::milliseconds(33), "on-time critical cycle should keep the next scheduled tick");
}

/**
 * @brief Verify overdue critical cycles do not run catch-up spins.
 */
void TestComputeNextPeriodicWakeSkipsCatchUpWhenOverdue()
{
    using clock = std::chrono::steady_clock;
    const auto base = clock::time_point{};
    const auto next = ComputeNextPeriodicWake(base, base + std::chrono::milliseconds(80), std::chrono::milliseconds(33));
    Check(next == base + std::chrono::milliseconds(113), "overdue critical cycle should schedule from finish time instead of catching up");
}

/**
 * @brief Verify system function actions switch between start and cancel states.
 */
void TestSystemFunctionActionsReflectHistoryRunning()
{
    const auto idle = BuildSystemFunctionActions(false, false);
    Check(idle[0].enabled && idle[0].id == L"history", "system F1 should start history while idle");
    Check(!idle[1].enabled, "system F2 should be disabled while idle");
    Check(!idle[2].enabled, "system F3 should be disabled without external app selection");

    const auto launchable = BuildSystemFunctionActions(false, true);
    Check(launchable[2].enabled && launchable[2].id == L"external-launch", "system F3 should launch selected external app");
    Check(launchable[2].label == L"起動", "system F3 should use launch label");

    const auto running = BuildSystemFunctionActions(true, true);
    Check(!running[0].enabled, "system F1 should be disabled while history is running");
    Check(running[1].enabled && running[1].id == L"history-cancel", "system F2 should cancel running history");
    Check(running[2].enabled, "system F3 should stay enabled during history load");
}

/**
 * @brief Verify the fixed V1 external app definition is exposed by Core.
 */
void TestDefaultExternalAppsDefineContainerController()
{
    const auto apps = BuildDefaultExternalAppDefinitions();
    Check(apps.size() == 1, "V1 should define one external app");
    Check(apps[0].id == L"container-controller", "container controller app id should be stable");
    Check(apps[0].label == L"コンテナコントローラ", "container controller label should be Japanese operator text");
    Check(apps[0].executablePath == L"ContainerController.exe", "container controller executable should be provisional exe name");
    Check(apps[0].arguments.empty(), "container controller should not pass provisional arguments");
    Check(apps[0].workingDirectory.empty(), "container controller should use current working directory by default");
    Check(!apps[0].allowMultiple, "container controller should suppress duplicate launches by default");
}

/**
 * @brief Verify the system grid binds the external app row for F3 launch.
 */
void TestSystemGridBindsExternalAppRow()
{
    UpdateSnapshot snapshot;
    const auto apps = BuildDefaultExternalAppDefinitions();
    const auto grid = BuildSystemGrid(snapshot, apps, nullptr);
    Check(grid.ColumnCount() == 4, "system grid should expose four operator columns");
    Check(grid.RowCount() >= 2, "system grid should include app row and history status row");
    Check(grid.Rows()[0].binding.externalAppId == L"container-controller", "first system row should bind external app id");
    Check(grid.Rows()[0].cells[0].text == L"外部アプリ", "external app row should show row type");
    Check(grid.Rows()[0].cells[1].text == L"コンテナコントローラ", "external app row should show app label");
    Check(grid.Rows()[0].cells[2].text == L"未起動", "external app row should default to not-started status");
}

/**
 * @brief Verify launch results are reflected in the system grid row.
 */
void TestSystemGridShowsExternalLaunchResult()
{
    UpdateSnapshot snapshot;
    const auto apps = BuildDefaultExternalAppDefinitions();

    ExternalLaunchResult success{L"container-controller", true, false, 0, L"起動しました"};
    auto grid = BuildSystemGrid(snapshot, apps, &success);
    Check(grid.Rows()[0].cells[2].text == L"起動済み", "successful launch should show running status");
    Check(grid.Rows()[0].cells[3].text.find(L"起動しました") != std::wstring::npos, "successful launch message should be shown");

    ExternalLaunchResult duplicate{L"container-controller", true, true, 0, L"起動済み"};
    grid = BuildSystemGrid(snapshot, apps, &duplicate);
    Check(grid.Rows()[0].cells[2].text == L"起動済み", "duplicate launch should keep running status");
    Check(grid.Rows()[0].cells[3].text.find(L"起動済み") != std::wstring::npos, "duplicate launch message should be shown");

    ExternalLaunchResult failure{L"container-controller", false, false, 2, L"指定されたファイルが見つかりません。"};
    grid = BuildSystemGrid(snapshot, apps, &failure);
    Check(grid.Rows()[0].cells[2].text == L"起動失敗", "failed launch should show failure status");
    Check(grid.Rows()[0].cells[3].text.find(L"指定されたファイル") != std::wstring::npos, "failed launch message should be shown");
}

/**
 * @brief Verify maintenance details are enabled only for abnormal rows.
 */
void TestMaintenanceFunctionActionsReflectAbnormalSelection()
{
    const auto none = BuildMaintenanceFunctionActions(false);
    Check(none.size() == 8, "maintenance actions should always expose 8 slots");
    Check(!none[0].enabled, "maintenance details should be disabled without abnormal selection");

    const auto abnormal = BuildMaintenanceFunctionActions(true);
    Check(abnormal[0].enabled, "maintenance details should be enabled for abnormal selection");
    Check(abnormal[0].id == L"maintenance-details", "maintenance details action should have a stable id");
    Check(abnormal[0].label == L"詳細", "maintenance details should use details label");
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
 * @brief Verify queued write start delays track max and exceeded-count metrics.
 */
void TestUpdateCoordinatorRecordsWriteDelayEnvelope()
{
    auto catalog = DataCatalog::CreateDefault();
    MockLatencyOptions latency;
    latency.writeDelayMs = 150;
    auto bridge = std::make_shared<MockBackendBridge>(catalog, MockLoadProfile::Default, latency);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.RequestWrite({2103, 1, 1, DataStyle::Raw}, L"1111");
    coordinator.RequestWrite({2103, 1, 2, DataStyle::Raw}, L"2222");
    Check(WaitForWriteCount(coordinator, 2), "coordinator should complete delayed queued writes");
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.writeCompletedCount == 2, "delayed writes should complete");
    Check(metrics.maxWriteStartDelayMs >= 100, "second queued write should raise max start delay");
    Check(metrics.writeStartDelayExceededCount >= 1, "queued write delay over 100ms should be counted");
}

/**
 * @brief Verify critical timing metrics are recorded while background work runs.
 */
void TestCriticalTimingMetricsAreRecorded()
{
    auto catalog = DataCatalog::CreateDefault();
    MockLatencyOptions latency;
    latency.criticalReadDelayMs = 1;
    auto bridge = std::make_shared<MockBackendBridge>(catalog, MockLoadProfile::Default, latency);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(coordinator.StartHistoryLoad({1}), "history should start");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    coordinator.CancelHistoryLoad();
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.criticalCycles > 0, "critical cycles should run");
    Check(metrics.criticalLastCycleMs > 0, "last critical cycle time should be recorded");
    Check(metrics.criticalMaxCycleMs >= metrics.criticalLastCycleMs, "max critical cycle should include last cycle");
    Check(metrics.criticalMaxSnapshotLockMs >= 0, "snapshot lock metric should be recorded");
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
    Check(station.selected.itemCount == 2, "selected container should keep backend item count");
    Check(!station.selected.items.empty(), "selected container should expose items");
}

/**
 * @brief Verify container summary keeps total item count separate from visible detail rows.
 */
void TestContainerSummarySeparatesItemCountAndVisibleItems()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto summary = BuildContainerSummary(gateway, 9, 5);
    Check(summary.containerNo == 9, "summary should keep requested container number");
    Check(summary.itemCount == 10, "summary should preserve total item count from dataId 2003");
    Check(summary.items.size() == 5, "summary should cap visible items by maxItems");

    const auto missing = BuildContainerSummary(gateway, 29, 5);
    Check(missing.missing, "missing container should retain missing flag");
    Check(missing.itemCount == 0, "missing container should report zero total items");
    Check(missing.items.empty(), "missing container should not expose visible items");
}

/**
 * @brief Verify container detail model exposes container and item fields.
 */
void TestContainerDetailModelIncludesContainerAndItems()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto detail = BuildContainerDetailModel(BuildContainerSummary(gateway, 9, 5));
    Check(detail.title == L"コンテナ詳細: 9", "container detail title should include container number");
    Check(HasDetailRow(detail, L"コンテナ番号", L"9"), "container detail should include number");
    Check(HasDetailRow(detail, L"名称", L"CNT-9"), "container detail should include name");
    Check(HasDetailRow(detail, L"状態", L"空"), "container detail should include state");
    Check(HasDetailRow(detail, L"品目数", L"10"), "container detail should include total item count");
    Check(HasDetailRow(detail, L"表示品目数", L"5"), "container detail should include visible item count");
    Check(HasDetailRow(detail, L"品目1 名称", L"ITEM-9-1"), "container detail should include first item name");
    Check(HasDetailRow(detail, L"品目1 出庫順序", L"91"), "container detail should include first item order");
    Check(HasDetailRow(detail, L"品目1 作業時間", L"00:05:45"), "container detail should include first item work time");
}

/**
 * @brief Verify schedule detail model reads schedule fields from binding.
 */
void TestScheduleDetailModelReadsBindingValues()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    const auto detail = BuildScheduleDetailModel(gateway, {1, 1});
    Check(detail.title == L"スケジュール詳細: コンテナ 1 / 品目 1", "schedule detail title should include binding");
    Check(HasDetailRow(detail, L"コンテナ番号", L"1"), "schedule detail should include container number");
    Check(HasDetailRow(detail, L"品目番号", L"1"), "schedule detail should include item number");
    Check(HasDetailRow(detail, L"品目名", L"ITEM-1-1"), "schedule detail should include item name");
    Check(HasDetailRow(detail, L"入庫日", L"2026/05/3"), "schedule detail should include inbound date");
    Check(HasDetailRow(detail, L"出庫開始", L"2026/05/21 2:00"), "schedule detail should include outbound start");
    Check(HasDetailRow(detail, L"出庫終了", L"2026/05/22 2:30"), "schedule detail should include outbound end");
    Check(HasDetailRow(detail, L"出庫順序", L"11"), "schedule detail should include outbound order");
    Check(HasDetailRow(detail, L"作業時間", L"00:05:45"), "schedule detail should include work time");
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
 * @brief Verify container list layout maps 100 containers into row-major 3-column cells.
 */
void TestContainerListLayoutModelUsesThreeColumnRowMajorPlacement()
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

    const auto layout = BuildContainerListLayoutModel(snapshot, 4);
    Check(layout.columnCount == 3, "container list should use three columns");
    Check(layout.rowCount == 34, "container list should use 34 rows for 100 containers");
    Check(layout.cells.size() == 100, "container list should expose 100 cells");

    const auto& first = layout.cells[0];
    Check(first.containerNo == 1, "container 1 should be first cell");
    Check(first.column == 0 && first.row == 0, "container 1 should be column 0 row 0");
    Check(first.displayText == L"1", "cell display text should be container number");
    Check(first.containerName == L"CNT-1", "cell should preserve container name");

    const auto& second = layout.cells[1];
    Check(second.containerNo == 2, "container 2 should be second cell");
    Check(second.column == 1 && second.row == 0, "container 2 should be column 1 row 0");

    const auto& third = layout.cells[2];
    Check(third.containerNo == 3, "container 3 should be third cell");
    Check(third.column == 2 && third.row == 0, "container 3 should be column 2 row 0");

    const auto& fourth = layout.cells[3];
    Check(fourth.containerNo == 4, "container 4 should start the second visual row");
    Check(fourth.column == 0 && fourth.row == 1, "container 4 should be column 0 row 1");
    Check(fourth.selected, "selected container should be marked");

    const auto& missing = layout.cells[28];
    Check(missing.containerNo == 29, "container 29 should be present");
    Check(missing.missing, "container without state should be marked missing");
    Check(missing.state == L"コンテナなし", "cell should preserve container state");

    const auto& last = layout.cells[99];
    Check(last.containerNo == 100, "container 100 should be last cell");
    Check(last.column == 0 && last.row == 33, "container 100 should be column 0 row 33");
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

/**
 * @brief Verify normal maintenance status values do not create abnormal rows.
 */
void TestMaintenanceStatusModelShowsNormalRows()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    UpdateSnapshot snapshot;
    for (size_t index = 0; index < catalog.CriticalKeys().size(); ++index) {
        snapshot.criticalValues.push_back({L"VALUE-" + std::to_wstring(index), BridgeError::Ok, {}, false});
    }

    const auto model = BuildMaintenanceStatusModel(catalog, snapshot);
    Check(model.rows.size() == 20, "maintenance model should expose all critical rows");
    Check(model.abnormalCount == 0, "normal critical values should not count abnormal maintenance rows");
    Check(model.rows[0].dataId == 1000, "first maintenance row should keep data id");
    Check(model.rows[0].name == L"重要情報 1000", "maintenance row should use catalog name");
    Check(model.rows[0].displayText == L"VALUE-0", "maintenance row should keep display text");
    Check(!model.rows[0].abnormal, "normal maintenance row should not be abnormal");
    Check(!model.rows[0].operationAvailable, "normal maintenance row should not expose operation");
    Check(model.rows[0].supportHint.reason == MaintenanceAbnormalReason::None, "normal row should have no abnormal reason");
    Check(model.rows[0].supportHint.priorityText == L"通常", "normal row should have normal priority");
    Check(model.rows[0].supportHint.recommendedCheck.find(L"追加確認不要") != std::wstring::npos,
          "normal row should not recommend extra checks");
}

/**
 * @brief Verify maintenance status model marks error, stale, missing, and abnormal text rows.
 */
void TestMaintenanceStatusModelMarksAbnormalRows()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    UpdateSnapshot snapshot;
    for (size_t index = 0; index < catalog.CriticalKeys().size() - 1; ++index) {
        snapshot.criticalValues.push_back({L"VALUE-" + std::to_wstring(index), BridgeError::Ok, {}, false});
    }
    snapshot.criticalValues[1] = {L"", BridgeError::Timeout, {}, true};
    snapshot.criticalValues[2] = {L"STALE", BridgeError::Ok, {}, true};
    snapshot.criticalValues[3] = {L"", BridgeError::Ok, {}, false};
    snapshot.criticalValues[4] = {L"異常検知", BridgeError::Ok, {}, false};

    const auto model = BuildMaintenanceStatusModel(catalog, snapshot);
    Check(model.rows.size() == 20, "maintenance model should include missing critical rows");
    Check(model.abnormalCount == 5, "error, stale, empty, abnormal text, and missing values should count abnormal");
    Check(model.rows[1].abnormal && model.rows[1].errorCode == BridgeError::Timeout, "error row should be abnormal and keep error");
    Check(model.rows[2].abnormal && model.rows[2].stale, "stale row should be abnormal");
    Check(model.rows[3].abnormal && model.rows[3].displayText == L"未取得", "empty row should show missing value");
    Check(model.rows[4].abnormal && model.rows[4].displayText == L"異常検知", "abnormal text row should be abnormal");
    Check(model.rows[19].abnormal && model.rows[19].displayText == L"未取得", "missing critical value should become abnormal");
    Check(model.rows[19].operationAvailable, "abnormal row should expose operation availability");
    Check(model.rows[1].supportHint.reason == MaintenanceAbnormalReason::ReadError, "read error should take highest abnormal reason priority");
    Check(model.rows[1].supportHint.recommendedCheck.find(L"通信") != std::wstring::npos, "read error hint should mention communication");
    Check(model.rows[1].supportHint.recommendedCheck.find(L"COM") != std::wstring::npos, "read error hint should mention COM");
    Check(model.rows[1].supportHint.recommendedCheck.find(L"対象装置") != std::wstring::npos, "read error hint should mention target equipment");
    Check(model.rows[2].supportHint.reason == MaintenanceAbnormalReason::Stale, "stale row should classify stale reason");
    Check(model.rows[2].supportHint.recommendedCheck.find(L"最新値") != std::wstring::npos, "stale hint should mention latest value");
    Check(model.rows[3].supportHint.reason == MaintenanceAbnormalReason::MissingValue, "empty value row should classify missing value");
    Check(model.rows[3].supportHint.recommendedCheck.find(L"取得状態") != std::wstring::npos, "missing value hint should mention acquisition state");
    Check(model.rows[4].supportHint.reason == MaintenanceAbnormalReason::AbnormalText, "abnormal text row should classify abnormal text");
    Check(model.rows[4].supportHint.recommendedCheck.find(L"コンテナコントローラ") != std::wstring::npos,
          "abnormal text hint should mention container controller");
}

/**
 * @brief Verify maintenance detail model preserves diagnostic fields.
 */
void TestMaintenanceDetailModelIncludesDiagnosticRows()
{
    MaintenanceStatusRow row;
    row.dataId = 1001;
    row.name = L"重要情報 1001";
    row.displayText = L"";
    row.errorCode = BridgeError::Timeout;
    row.stale = true;
    row.abnormal = true;
    row.operationAvailable = true;
    row.supportHint = BuildMaintenanceSupportHint(row);

    const auto detail = BuildMaintenanceDetailModel(row);
    Check(detail.title == L"保守詳細: 重要情報 1001", "maintenance detail should include row name in title");
    Check(detail.rows.size() >= 11, "maintenance detail should expose diagnostic and support rows");
    Check(detail.rows[0].label == L"データID" && detail.rows[0].value == L"1001", "detail should include data id");
    Check(detail.rows[3].label == L"状態" && detail.rows[3].value == L"異常", "detail should include abnormal state");
    Check(detail.rows[4].label == L"エラー" && detail.rows[4].value == L"タイムアウト", "detail should include error text");
    Check(detail.rows[5].label == L"stale" && detail.rows[5].value == L"true", "detail should include stale flag");
    Check(detail.rows[6].label == L"操作可" && detail.rows[6].value == L"true", "detail should include operation availability");
    Check(detail.rows[7].label == L"原因分類" && detail.rows[7].value.find(L"Read") != std::wstring::npos,
          "detail should include support reason");
    Check(detail.rows[8].label == L"確認優先度" && detail.rows[8].value == L"高", "detail should include support priority");
    Check(detail.rows[9].label == L"推奨確認" && detail.rows[9].value.find(L"通信") != std::wstring::npos,
          "detail should include recommended check");
    Check(detail.rows[10].label == L"管理者メモ" && detail.rows[10].value.find(L"本画面から復旧Writeは行わない") != std::wstring::npos,
          "detail should state that this screen does not write recovery commands");
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
        TestBridgeFactoryParsesMockLoadAndLatencyOptions,
        TestMockBridgeFormatsValuesAndRejectsInvalidStyle,
        TestMockMaxLoadProfileBuildsThousandScheduleRows,
        TestGatewayMarksErrorsAsStale,
        TestFunctionActionsReflectSelection,
        TestFunctionSlotFromVirtualKey,
        TestNavigationModelBuildsDefaultCells,
        TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection,
        TestSystemFunctionActionsReflectHistoryRunning,
        TestDefaultExternalAppsDefineContainerController,
        TestSystemGridBindsExternalAppRow,
        TestSystemGridShowsExternalLaunchResult,
        TestMaintenanceFunctionActionsReflectAbnormalSelection,
        TestGridModelKeepsCellKinds,
        TestGridEditPolicyValidatesCellKinds,
        TestScheduleGridBindsRowsToContainerItems,
        TestScheduleGridSortsByOutboundOrder,
        TestMockWriteUpdatesScheduleOrderReadback,
        TestMockScheduleAddAndDeleteReflectInGrid,
        TestBuildScheduleMoveUpWrites,
        TestBuildScheduleRenumberWrites,
        TestScheduleDuplicateOrderDetection,
        TestScheduleUndoStack,
        TestScheduleUndoWriteBuilders,
        TestBuildScheduleCellEditWrites,
        TestHistoryRequestValidation,
        TestHistoryKeyGenerationUsesOutboundHistoryId,
        TestComputeNextPeriodicWakeKeepsNormalCadence,
        TestComputeNextPeriodicWakeSkipsCatchUpWhenOverdue,
        TestUpdateCoordinatorRecordsSuccessfulWriteMetrics,
        TestUpdateCoordinatorRecordsReadOnlyWriteError,
        TestUpdateCoordinatorRecordsScheduleMutationMetrics,
        TestUpdateCoordinatorRecordsWriteDelayEnvelope,
        TestCriticalTimingMetricsAreRecorded,
        TestUpdateCoordinatorCancelsHistoryLoad,
        TestUpdateCoordinatorCapsHistoryRecords,
        TestHistoryLoadRejectsInvalidRequestBeforeCommunication,
        TestHistoryLoadKeepsWritePriorityResponsive,
        TestPriorityQueueOrdersCriticalBeforeNormalAndHistory,
        TestScreenSnapshotBuildsContainerSummary,
        TestContainerSummarySeparatesItemCountAndVisibleItems,
        TestContainerDetailModelIncludesContainerAndItems,
        TestScheduleDetailModelReadsBindingValues,
        TestStationLayoutModelUsesFixedFiveColumnPlacement,
        TestContainerListLayoutModelUsesThreeColumnRowMajorPlacement,
        TestStatusSummaryShowsNormalCriticalState,
        TestStatusSummaryCountsCriticalErrorsAndMissingValues,
        TestStatusSummaryIncludesWriteAndHistoryDetails,
        TestMaintenanceStatusModelShowsNormalRows,
        TestMaintenanceStatusModelMarksAbnormalRows,
        TestMaintenanceDetailModelIncludesDiagnosticRows,
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
