#include "BridgeFactory.h"
#include "DataCatalog.h"
#include "DataGateway.h"
#include "UpdateScheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <windows.h>

/**
 * @file tests/PerformanceTest/main.cpp
 * @brief Runs the coordinator performance smoke test from the command line.
 */

namespace {

/**
 * @brief Parse optional performance-test duration.
 * @param argc Argument count.
 * @param argv Wide-character argument vector.
 * @return Duration in milliseconds, with a minimum of 1000 ms.
 */
int ParseDurationMs(int argc, wchar_t** argv)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring(argv[index]) == L"--duration-ms") {
            return std::max(1000, std::stoi(argv[index + 1]));
        }
    }
    return 60000;
}

/**
 * @brief Check whether a command-line token is present.
 */
bool HasArgument(int argc, wchar_t** argv, const wchar_t* argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]) == argument) {
            return true;
        }
    }
    return false;
}

std::wstring QuoteArgumentForCommandLine(const std::wstring& argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    for (const auto ch : argument) {
        if (ch == L'"') {
            quoted += L'\\';
        }
        quoted += ch;
    }
    quoted += L'"';
    return quoted;
}

/**
 * @brief Join command-line arguments for bridge option parsing.
 * @param argc Argument count.
 * @param argv Wide-character argument vector.
 * @return Space-separated command line without the executable name.
 */
std::wstring JoinArguments(int argc, wchar_t** argv)
{
    std::wstring commandLine;
    for (int index = 1; index < argc; ++index) {
        if (!commandLine.empty()) {
            commandLine += L' ';
        }
        commandLine += QuoteArgumentForCommandLine(argv[index]);
    }
    return commandLine;
}

} // namespace

/**
 * @brief Run update-coordinator performance checks against the configured bridge.
 * @param argc Argument count.
 * @param argv Wide-character argument vector.
 * @return 0 on success, or a non-zero code identifying the failed performance gate.
 */
int wmain(int argc, wchar_t** argv)
{
    const int durationMs = ParseDurationMs(argc, argv);
    const bool maxLoad = HasArgument(argc, argv, L"--max-load");
    auto options = ParseBridgeFactoryOptions(JoinArguments(argc, argv));
    if (maxLoad && options.bridgeMode == BridgeMode::InProcessMock) {
        options.mockLoadProfile = MockLoadProfile::MaxLoad;
    }
    const auto catalog = LoadConfiguredCatalog(options);
    auto bridge = CreateBackendBridge(options);
    DataGateway gateway(bridge);
    if (gateway.Connect(options.ipAddress) != BridgeError::Ok) {
        std::wcerr << L"connect failed\n";
        return 1;
    }

    UpdateCoordinator coordinator(catalog, gateway);
    std::atomic<bool> loadRunning{true};
    std::atomic<int> requestedWrites{0};
    std::atomic<int> scheduleGridBuildCount{0};
    std::atomic<int> scheduleGridLastRows{0};
    std::atomic<long long> scheduleGridMaxMs{0};
    std::thread scheduleGridThread;
    std::thread writeProducerThread;

    coordinator.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    coordinator.StartHistoryLoad({3});
    if (maxLoad) {
        scheduleGridThread = std::thread([&] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
            while (loadRunning.load()) {
                const auto started = std::chrono::steady_clock::now();
                const auto grid = BuildScheduleGrid(gateway);
                scheduleGridLastRows = static_cast<int>(grid.RowCount());
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                auto currentMax = scheduleGridMaxMs.load();
                while (elapsed > currentMax && !scheduleGridMaxMs.compare_exchange_weak(currentMax, elapsed)) {
                }
                ++scheduleGridBuildCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        writeProducerThread = std::thread([&] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
            int sequence = 0;
            while (loadRunning.load()) {
                ++sequence;
                const int containerNo = ((sequence - 1) % 100) + 1;
                const int itemNo = ((sequence - 1) % 10) + 1;
                ++requestedWrites;
                coordinator.RequestWrite({2103, containerNo, itemNo, DataStyle::Raw}, std::to_wstring(5000 + sequence));
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        });
    } else {
        requestedWrites = 1;
        coordinator.RequestWrite({2001, 1, 0, DataStyle::Raw}, L"CNT-PERF");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    loadRunning = false;
    if (writeProducerThread.joinable()) {
        writeProducerThread.join();
    }
    if (scheduleGridThread.joinable()) {
        scheduleGridThread.join();
    }
    for (int attempt = 0; attempt < 200 && coordinator.Metrics().writeCompletedCount < requestedWrites.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    const int expectedMinimumCriticalCycles = (durationMs / 33) - 2;
    std::wcout << L"criticalCycles=" << metrics.criticalCycles << L"\n"
               << L"criticalDeadlineMisses=" << metrics.criticalDeadlineMisses << L"\n"
               << L"criticalLastCycleMs=" << metrics.criticalLastCycleMs << L"\n"
               << L"criticalMaxCycleMs=" << metrics.criticalMaxCycleMs << L"\n"
               << L"criticalMaxSnapshotLockMs=" << metrics.criticalMaxSnapshotLockMs << L"\n"
               << L"normalCycles=" << metrics.normalCycles << L"\n"
               << L"lastWriteStartDelayMs=" << metrics.lastWriteStartDelayMs << L"\n"
               << L"maxWriteStartDelayMs=" << metrics.maxWriteStartDelayMs << L"\n"
               << L"writeStartDelayExceededCount=" << metrics.writeStartDelayExceededCount << L"\n"
               << L"requestedWrites=" << requestedWrites.load() << L"\n"
               << L"writeCompletedCount=" << metrics.writeCompletedCount << L"\n"
               << L"lastWriteErrorCode=" << static_cast<int>(metrics.lastWriteErrorCode) << L"\n"
               << L"historyReadCount=" << metrics.historyReadCount << L"\n"
               << L"historyErrorCount=" << metrics.historyErrorCount << L"\n"
               << L"historyCancelCount=" << metrics.historyCancelCount << L"\n"
               << L"historyLastErrorCode=" << static_cast<int>(metrics.historyLastErrorCode) << L"\n"
               << L"scheduleGridBuildCount=" << scheduleGridBuildCount.load() << L"\n"
               << L"scheduleGridLastRows=" << scheduleGridLastRows.load() << L"\n"
               << L"scheduleGridMaxMs=" << scheduleGridMaxMs.load() << L"\n";
    std::wcout.flush();

    const bool strictCadence = options.bridgeMode == BridgeMode::InProcessMock;
    if (strictCadence && metrics.criticalCycles < expectedMinimumCriticalCycles) {
        std::wcerr << L"critical refresh cadence too slow\n";
        return 2;
    }
    if (!strictCadence && metrics.criticalCycles <= 0) {
        std::wcerr << L"critical refresh did not run\n";
        return 2;
    }
    if (strictCadence && metrics.criticalDeadlineMisses != 0) {
        std::wcerr << L"critical refresh deadline missed\n";
        return 3;
    }
    if (metrics.lastWriteStartDelayMs < 0 || metrics.lastWriteStartDelayMs > 100) {
        std::wcerr << L"write start delay exceeded 100ms\n";
        return 4;
    }
    if (metrics.maxWriteStartDelayMs < 0 || metrics.maxWriteStartDelayMs > 100 || metrics.writeStartDelayExceededCount != 0) {
        std::wcerr << L"write start delay envelope exceeded 100ms\n";
        return 9;
    }
    if (metrics.writeCompletedCount < requestedWrites.load()) {
        std::wcerr << L"not all requested writes completed\n";
        return 5;
    }
    if (metrics.lastWriteErrorCode != BridgeError::Ok) {
        std::wcerr << L"write returned error\n";
        return 6;
    }
    if (metrics.historyReadCount <= 0) {
        std::wcerr << L"history did not run\n";
        return 7;
    }
    if (metrics.historyErrorCount != 0) {
        std::wcerr << L"history returned errors\n";
        return 8;
    }
    if (maxLoad && scheduleGridBuildCount <= 0) {
        std::wcerr << L"schedule grid max-load rebuild did not run\n";
        return 10;
    }
    if (maxLoad && options.bridgeMode == BridgeMode::InProcessMock && scheduleGridLastRows != 1000) {
        std::wcerr << L"schedule grid max-load row count mismatch\n";
        return 11;
    }
    return 0;
}
