#include "BridgeFactory.h"
#include "DataCatalog.h"
#include "DataGateway.h"
#include "UpdateScheduler.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

int ParseDurationMs(int argc, wchar_t** argv)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring(argv[index]) == L"--duration-ms") {
            return std::max(1000, std::stoi(argv[index + 1]));
        }
    }
    return 60000;
}

std::wstring JoinArguments(int argc, wchar_t** argv)
{
    std::wstring commandLine;
    for (int index = 1; index < argc; ++index) {
        if (!commandLine.empty()) {
            commandLine += L' ';
        }
        commandLine += argv[index];
    }
    return commandLine;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const int durationMs = ParseDurationMs(argc, argv);
    auto options = ParseBridgeFactoryOptions(JoinArguments(argc, argv));
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        std::wcerr << L"connect failed\n";
        return 1;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    coordinator.StartHistoryLoad(180);
    coordinator.RequestWrite({2001, 1, 0, DataStyle::Raw}, L"CNT-PERF");

    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    const int expectedMinimumCriticalCycles = (durationMs / 33) - 2;
    std::wcout << L"criticalCycles=" << metrics.criticalCycles << L"\n"
               << L"criticalDeadlineMisses=" << metrics.criticalDeadlineMisses << L"\n"
               << L"normalCycles=" << metrics.normalCycles << L"\n"
               << L"lastWriteStartDelayMs=" << metrics.lastWriteStartDelayMs << L"\n"
               << L"writeCompletedCount=" << metrics.writeCompletedCount << L"\n"
               << L"lastWriteErrorCode=" << static_cast<int>(metrics.lastWriteErrorCode) << L"\n";

    if (metrics.criticalCycles < expectedMinimumCriticalCycles) {
        std::wcerr << L"critical refresh cadence too slow\n";
        return 2;
    }
    if (metrics.criticalDeadlineMisses != 0) {
        std::wcerr << L"critical refresh deadline missed\n";
        return 3;
    }
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        std::wcerr << L"write start delay exceeded 100ms\n";
        return 4;
    }
    if (metrics.writeCompletedCount < 1) {
        std::wcerr << L"write did not complete\n";
        return 5;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok) {
        std::wcerr << L"write returned error\n";
        return 6;
    }
    return 0;
}
