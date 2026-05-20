#include "DataCatalog.h"
#include "DataGateway.h"
#include "FunctionBarModel.h"
#include "GridModel.h"
#include "MockBackendBridge.h"
#include "ScreenModels.h"
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

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestCatalogDefinesCriticalDataAndStyles()
{
    const auto catalog = DataCatalog::CreateDefault();
    Check(catalog.CriticalKeys().size() == 20, "critical key count must be 20");
    Check(catalog.IsStyleAllowed(1000, DataStyle::Raw), "critical raw style should be allowed");
    Check(catalog.IsStyleAllowed(1010, DataStyle::ThousandsSeparated), "number style should be allowed");
    Check(!catalog.IsStyleAllowed(1000, DataStyle::MillimetersToInches), "invalid critical style should be rejected");
}

void TestCatalogLoadsFromJsonFile()
{
    const auto catalog = DataCatalog::LoadFromFile(L"config/data_catalog.json");
    Check(catalog.Definitions().size() == 31, "json catalog should include 31 definitions");
    Check(catalog.CriticalKeys().size() == 20, "json catalog should include 20 critical keys");
    Check(catalog.FindDefinition(2001)->writable, "container name should be writable");
    Check(catalog.IsStyleAllowed(2104, DataStyle::SecondsToHhMmSs), "work time should allow hhmmss style");
}

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

void TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection()
{
    const auto none = BuildScheduleFunctionActions(false);
    Check(none.size() == 8, "schedule actions should always expose 8 slots");
    Check(!none[0].enabled, "schedule details should be disabled without selection");
    Check(!none[1].enabled, "schedule order change should be disabled without selection");
    Check(!none[2].enabled, "schedule add should stay disabled in V1");
    Check(!none[3].enabled, "schedule delete should stay disabled in V1");

    const auto selected = BuildScheduleFunctionActions(true);
    Check(selected[0].enabled && selected[0].id == L"details", "schedule details should be enabled with selection");
    Check(selected[1].enabled && selected[1].id == L"order-change", "schedule F2 should edit order with selection");
    Check(selected[1].label == L"順序変更", "schedule F2 should be labeled for order change");
    Check(!selected[2].enabled && !selected[3].enabled, "schedule add/delete should stay disabled in V1");
}

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
}

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
    Check(metrics.lastWriteStartDelayMs >= 0 && metrics.lastWriteStartDelayMs <= 100, "write should start within 100ms");
    const auto value = gateway.Read({2103, 1, 1, DataStyle::Raw});
    Check(value.displayText == L"6789", "coordinator write should update backend value");
}

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

} // namespace

int wmain()
{
    const std::vector<void (*)()> tests = {
        TestCatalogDefinesCriticalDataAndStyles,
        TestCatalogLoadsFromJsonFile,
        TestCatalogRejectsInvalidJsonFile,
        TestCatalogRejectsUnknownStyleName,
        TestBridgeFactoryCreatesMockBridge,
        TestMockBridgeFormatsValuesAndRejectsInvalidStyle,
        TestGatewayMarksErrorsAsStale,
        TestFunctionActionsReflectSelection,
        TestScheduleFunctionActionsExposeOrderChangeOnlyForSelection,
        TestGridModelKeepsCellKinds,
        TestScheduleGridBindsRowsToContainerItems,
        TestMockWriteUpdatesScheduleOrderReadback,
        TestUpdateCoordinatorRecordsSuccessfulWriteMetrics,
        TestUpdateCoordinatorRecordsReadOnlyWriteError,
        TestPriorityQueueOrdersCriticalBeforeNormalAndHistory,
        TestScreenSnapshotBuildsContainerSummary,
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
