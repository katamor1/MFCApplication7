#pragma once

#include "DataCatalog.h"
#include "DataGateway.h"
#include "ScreenModels.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum class WorkPriority
{
    Critical = 0,
    UserWrite = 1,
    Normal = 2,
    History = 3,
};

struct WorkItem
{
    WorkPriority priority{WorkPriority::Normal};
    unsigned long long sequence{};
    std::wstring name;
};

class PrioritizedWorkQueue
{
public:
    void Push(WorkItem item);
    WorkItem Pop();
    bool Empty() const noexcept;

private:
    struct Compare
    {
        bool operator()(const WorkItem& left, const WorkItem& right) const noexcept;
    };

    std::priority_queue<WorkItem, std::vector<WorkItem>, Compare> queue_;
};

struct UpdateSnapshot
{
    std::vector<DataValue> criticalValues;
    StationSnapshot station;
    int historyProgress{};
    bool historyRunning{false};
};

struct SchedulerMetrics
{
    int criticalCycles{};
    int criticalDeadlineMisses{};
    int normalCycles{};
    long long lastWriteStartDelayMs{-1};
    int writeCompletedCount{};
    BridgeError lastWriteErrorCode{BridgeError::Ok};
};

class UpdateCoordinator
{
public:
    UpdateCoordinator(DataCatalog catalog, DataGateway gateway);
    ~UpdateCoordinator();

    UpdateCoordinator(const UpdateCoordinator&) = delete;
    UpdateCoordinator& operator=(const UpdateCoordinator&) = delete;

    void Start();
    void Stop();
    void RequestWrite(DataKey key, std::wstring value);
    void StartHistoryLoad(int totalSteps);
    void SetSelectedContainer(int containerNo);

    UpdateSnapshot Snapshot() const;
    SchedulerMetrics Metrics() const noexcept;

private:
    struct WriteRequest
    {
        DataKey key;
        std::wstring value;
        std::chrono::steady_clock::time_point enqueuedAt;
    };

    void CriticalLoop();
    void NormalLoop();
    void WriteLoop();
    void HistoryLoop(int totalSteps);

    DataCatalog catalog_;
    DataGateway gateway_;

    std::atomic<bool> running_{false};
    std::atomic<int> selectedContainerNo_{1};
    std::atomic<int> criticalCycles_{0};
    std::atomic<int> criticalDeadlineMisses_{0};
    std::atomic<int> normalCycles_{0};
    std::atomic<int> historyProgress_{0};
    std::atomic<bool> historyRunning_{false};
    std::atomic<long long> lastWriteStartDelayMs_{-1};
    std::atomic<int> writeCompletedCount_{0};
    std::atomic<int> lastWriteErrorCode_{static_cast<int>(BridgeError::Ok)};

    mutable std::mutex snapshotMutex_;
    UpdateSnapshot snapshot_;

    std::mutex writeMutex_;
    std::condition_variable writeCv_;
    std::queue<WriteRequest> writeQueue_;

    std::thread criticalThread_;
    std::thread normalThread_;
    std::thread writeThread_;
    std::thread historyThread_;
};
