#include "UpdateScheduler.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

void PrioritizedWorkQueue::Push(WorkItem item)
{
    queue_.push(std::move(item));
}

WorkItem PrioritizedWorkQueue::Pop()
{
    if (queue_.empty()) {
        throw std::runtime_error("work queue is empty");
    }
    auto item = queue_.top();
    queue_.pop();
    return item;
}

bool PrioritizedWorkQueue::Empty() const noexcept
{
    return queue_.empty();
}

bool PrioritizedWorkQueue::Compare::operator()(const WorkItem& left, const WorkItem& right) const noexcept
{
    if (left.priority != right.priority) {
        return static_cast<int>(left.priority) > static_cast<int>(right.priority);
    }
    return left.sequence > right.sequence;
}

UpdateCoordinator::UpdateCoordinator(DataCatalog catalog, DataGateway gateway)
    : catalog_(std::move(catalog))
    , gateway_(std::move(gateway))
{
}

UpdateCoordinator::~UpdateCoordinator()
{
    Stop();
}

void UpdateCoordinator::Start()
{
    if (running_.exchange(true)) {
        return;
    }

    criticalThread_ = std::thread(&UpdateCoordinator::CriticalLoop, this);
    normalThread_ = std::thread(&UpdateCoordinator::NormalLoop, this);
    writeThread_ = std::thread(&UpdateCoordinator::WriteLoop, this);
}

void UpdateCoordinator::Stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    writeCv_.notify_all();
    if (criticalThread_.joinable()) {
        criticalThread_.join();
    }
    if (normalThread_.joinable()) {
        normalThread_.join();
    }
    if (writeThread_.joinable()) {
        writeThread_.join();
    }
    if (historyThread_.joinable()) {
        historyThread_.join();
    }
}

void UpdateCoordinator::RequestWrite(DataKey key, std::wstring value)
{
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push({key, std::move(value), std::chrono::steady_clock::now()});
    }
    writeCv_.notify_one();
}

void UpdateCoordinator::StartHistoryLoad(int totalSteps)
{
    if (historyRunning_.exchange(true)) {
        return;
    }
    if (historyThread_.joinable()) {
        historyThread_.join();
    }
    historyProgress_ = 0;
    historyThread_ = std::thread(&UpdateCoordinator::HistoryLoop, this, totalSteps);
}

void UpdateCoordinator::SetSelectedContainer(int containerNo)
{
    selectedContainerNo_ = containerNo < 1 ? 1 : (containerNo > 100 ? 100 : containerNo);
}

UpdateSnapshot UpdateCoordinator::Snapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    auto copy = snapshot_;
    copy.historyProgress = historyProgress_.load();
    copy.historyRunning = historyRunning_.load();
    return copy;
}

SchedulerMetrics UpdateCoordinator::Metrics() const noexcept
{
    return {criticalCycles_.load(),
            criticalDeadlineMisses_.load(),
            normalCycles_.load(),
            lastWriteStartDelayMs_.load(),
            writeCompletedCount_.load(),
            static_cast<BridgeError>(lastWriteErrorCode_.load())};
}

void UpdateCoordinator::CriticalLoop()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
    using namespace std::chrono;
    auto next = steady_clock::now();
    while (running_) {
        const auto started = steady_clock::now();
        const auto values = gateway_.ReadMany(catalog_.CriticalKeys());
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshot_.criticalValues = values;
        }
        ++criticalCycles_;
        if (duration_cast<milliseconds>(steady_clock::now() - started).count() > 33) {
            ++criticalDeadlineMisses_;
        }
        next += milliseconds(33);
        std::this_thread::sleep_until(next);
    }
}

void UpdateCoordinator::NormalLoop()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    using namespace std::chrono;
    while (running_) {
        auto station = BuildStationSnapshot(gateway_, selectedContainerNo_.load());
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            std::swap(snapshot_.station, station);
        }
        ++normalCycles_;
        std::this_thread::sleep_for(milliseconds(500));
    }
}

void UpdateCoordinator::WriteLoop()
{
    while (running_) {
        WriteRequest request;
        {
            std::unique_lock<std::mutex> lock(writeMutex_);
            writeCv_.wait(lock, [this] { return !running_ || !writeQueue_.empty(); });
            if (!running_ && writeQueue_.empty()) {
                return;
            }
            request = std::move(writeQueue_.front());
            writeQueue_.pop();
        }

        const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - request.enqueuedAt).count();
        lastWriteStartDelayMs_ = delay;
        const auto error = gateway_.Write(request.key, request.value);
        lastWriteErrorCode_ = static_cast<int>(error);
        ++writeCompletedCount_;
    }
}

void UpdateCoordinator::HistoryLoop(int totalSteps)
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
    const int steps = totalSteps <= 0 ? 1 : totalSteps;
    for (int step = 1; running_ && step <= steps; ++step) {
        std::wstring ignored;
        gateway_.Read({4000, step % 366, step % 1000, DataStyle::Raw});
        historyProgress_ = (step * 100) / steps;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    historyProgress_ = 100;
    historyRunning_ = false;
}
