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
        TestGridModelKeepsCellKinds,
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
