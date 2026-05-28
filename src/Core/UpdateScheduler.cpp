#include "UpdateScheduler.h"

#include <chrono>
#include <iterator>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @file UpdateScheduler.cpp
 * @brief Multi-threaded scheduler implementation for critical/normal updates, writes, and history reads.
 */

namespace {

constexpr int kHistoryRecordsPerDay = 1000;
constexpr size_t kHistorySnapshotLimit = 500;
constexpr size_t kHistoryBatchSize = 10;
constexpr int kConsecutiveHistoryErrorLimit = 50;

bool IsScheduleMutationDataId(int dataId) noexcept
{
    return dataId == 2100 || dataId == 2102 || dataId == 2103 || dataId == 2104 ||
        dataId == 2105 || dataId == 3000;
}

void StoreMax(std::atomic<long long>& target, long long value) noexcept
{
    auto current = target.load();
    while (value > current && !target.compare_exchange_weak(current, value)) {
    }
}

} // namespace

void PrioritizedWorkQueue::Push(WorkItem item)
{
    queue_.push(std::move(item));
}

/**
 * @brief Pop highest-priority queue item.
 * @throws std::runtime_error when queue is empty.
 */
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

/**
 * @brief Priority comparator for priority_queue.
 */
bool PrioritizedWorkQueue::Compare::operator()(const WorkItem& left, const WorkItem& right) const noexcept
{
    if (left.priority != right.priority) {
        return static_cast<int>(left.priority) > static_cast<int>(right.priority);
    }
    return left.sequence > right.sequence;
}

/**
 * @brief Check history request range (1..365 days).
 */
bool IsValidHistoryRequest(HistoryRequest request) noexcept
{
    return request.days >= 1 && request.days <= 365;
}

/**
 * @brief Build synthetic key for history read records.
 */
DataKey MakeHistoryKey(int dayOffset, int recordIndex) noexcept
{
    return {4000, dayOffset, recordIndex, DataStyle::Raw};
}

/**
 * @brief Validate schedule add form values before creating a write payload.
 */
bool IsValidScheduleAddRequest(const ScheduleAddRequest& request) noexcept
{
    if (request.containerNo < 1 || request.containerNo > 100 ||
        request.itemNo < 1 || request.itemNo > 1000 ||
        request.order < 1 || request.order > 9999 ||
        request.itemName.empty() || request.itemName.size() > 40) {
        return false;
    }
    for (const auto ch : request.itemName) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') {
            return false;
        }
    }
    return true;
}

/**
 * @brief Encode order and item name for the provisional schedule-add COM write.
 */
std::wstring EncodeScheduleAddValue(const ScheduleAddRequest& request)
{
    return std::to_wstring(request.order) + L"\t" + request.itemName;
}

/**
 * @brief Compute the next periodic wake without catching up against past ticks.
 */
std::chrono::steady_clock::time_point ComputeNextPeriodicWake(
    std::chrono::steady_clock::time_point previousNext,
    std::chrono::steady_clock::time_point finished,
    std::chrono::milliseconds period) noexcept
{
    const auto scheduled = previousNext + period;
    if (scheduled <= finished) {
        return finished + period;
    }
    return scheduled;
}

/**
 * @brief Initialize coordinator state and initialize history status text.
 */
UpdateCoordinator::UpdateCoordinator(DataCatalog catalog, DataGateway gateway)
    : catalog_(std::move(catalog))
    , gateway_(std::move(gateway))
{
    snapshot_.historyStatusText = L"待機";
}

/**
 * @brief Ensure threads are stopped during destruction.
 */
UpdateCoordinator::~UpdateCoordinator()
{
    Stop();
}

/**
 * @brief Start critical/normal/write loops.
 */
void UpdateCoordinator::Start()
{
    if (running_.exchange(true)) {
        return;
    }
    writeLoopReady_ = false;

    criticalThread_ = std::thread(&UpdateCoordinator::CriticalLoop, this);
    normalThread_ = std::thread(&UpdateCoordinator::NormalLoop, this);
    writeThread_ = std::thread(&UpdateCoordinator::WriteLoop, this);
    for (int attempt = 0; attempt < 100 && !writeLoopReady_.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

/**
 * @brief Stop loops, cancel pending history, and join worker threads.
 */
void UpdateCoordinator::Stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    historyCancelRequested_ = true;
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

/**
 * @brief Enqueue write request with enqueue timestamp.
 */
void UpdateCoordinator::RequestWrite(DataKey key, std::wstring value)
{
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push({key, std::move(value), std::chrono::steady_clock::now()});
    }
    writeCv_.notify_one();
}

/**
 * @brief Start history read worker and initialize snapshot state.
 */
bool UpdateCoordinator::StartHistoryLoad(HistoryRequest request)
{
    if (historyRunning_.load()) {
        return false;
    }

    if (!IsValidHistoryRequest(request)) {
        historyProgress_ = 0;
        historyCancelRequested_ = false;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshot_.historyStatusText = L"履歴日数が不正です";
            snapshot_.historyRunning = false;
            snapshot_.historyCancelled = false;
            snapshot_.historyRecords.clear();
        }
        return false;
    }

    if (historyRunning_.exchange(true)) {
        return false;
    }
    if (historyThread_.joinable()) {
        historyThread_.join();
    }
    historyProgress_ = 0;
    historyCancelRequested_ = false;
    historyLastErrorCode_ = static_cast<int>(BridgeError::Ok);
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyRecords.clear();
        snapshot_.historyStatusText = L"履歴取得中";
        snapshot_.historyRunning = true;
        snapshot_.historyCancelled = false;
        snapshot_.historyProgress = 0;
    }
    historyThread_ = std::thread(&UpdateCoordinator::HistoryLoop, this, request);
    return true;
}

/**
 * @brief Request history loop cancellation.
 */
void UpdateCoordinator::CancelHistoryLoad()
{
    if (!historyRunning_.load()) {
        return;
    }
    historyCancelRequested_ = true;
    ++historyCancelCount_;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyStatusText = L"履歴中断要求";
    }
}

/**
 * @brief Clamp and apply selected container index used by normal loop.
 */
void UpdateCoordinator::SetSelectedContainer(int containerNo)
{
    selectedContainerNo_ = containerNo < 1 ? 1 : (containerNo > 100 ? 100 : containerNo);
}

/**
 * @brief Snapshot current scheduler outputs with atomics synchronized.
 */
UpdateSnapshot UpdateCoordinator::Snapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    auto copy = snapshot_;
    copy.historyProgress = historyProgress_.load();
    copy.historyRunning = historyRunning_.load();
    copy.historyCancelled = snapshot_.historyCancelled;
    return copy;
}

/**
 * @brief Collect diagnostics counters and codes.
 */
SchedulerMetrics UpdateCoordinator::Metrics() const noexcept
{
    SchedulerMetrics metrics;
    metrics.criticalCycles = criticalCycles_.load();
    metrics.criticalDeadlineMisses = criticalDeadlineMisses_.load();
    metrics.criticalLastCycleMs = criticalLastCycleMs_.load();
    metrics.criticalMaxCycleMs = criticalMaxCycleMs_.load();
    metrics.criticalMaxSnapshotLockMs = criticalMaxSnapshotLockMs_.load();
    metrics.normalCycles = normalCycles_.load();
    metrics.lastWriteStartDelayMs = lastWriteStartDelayMs_.load();
    metrics.maxWriteStartDelayMs = maxWriteStartDelayMs_.load();
    metrics.writeStartDelayExceededCount = writeStartDelayExceededCount_.load();
    metrics.writeCompletedCount = writeCompletedCount_.load();
    metrics.writeErrorCount = writeErrorCount_.load();
    metrics.lastWriteErrorCode = static_cast<BridgeError>(lastWriteErrorCode_.load());
    metrics.scheduleOrderWriteCompletedCount = scheduleOrderWriteCompletedCount_.load();
    metrics.scheduleAddCompletedCount = scheduleAddCompletedCount_.load();
    metrics.scheduleDeleteCompletedCount = scheduleDeleteCompletedCount_.load();
    metrics.scheduleMutationErrorCount = scheduleMutationErrorCount_.load();
    metrics.lastScheduleMutationErrorCode = static_cast<BridgeError>(lastScheduleMutationErrorCode_.load());
    metrics.historyReadCount = historyReadCount_.load();
    metrics.historyErrorCount = historyErrorCount_.load();
    metrics.historyCancelCount = historyCancelCount_.load();
    metrics.historyLastErrorCode = static_cast<BridgeError>(historyLastErrorCode_.load());
    return metrics;
}

/**
 * @brief Periodic high-priority cycle reading critical keys.
 */
void UpdateCoordinator::CriticalLoop()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
    using namespace std::chrono;
    constexpr auto period = milliseconds(33);
    auto next = steady_clock::now();
    while (running_) {
        const auto started = steady_clock::now();
        const auto values = gateway_.ReadMany(catalog_.CriticalKeys());
        const auto beforeSnapshotLock = steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshot_.criticalValues = values;
        }
        const auto finished = steady_clock::now();
        const auto cycleMs = duration_cast<milliseconds>(finished - started).count();
        const auto snapshotLockMs = duration_cast<milliseconds>(finished - beforeSnapshotLock).count();
        criticalLastCycleMs_ = cycleMs;
        StoreMax(criticalMaxCycleMs_, cycleMs);
        StoreMax(criticalMaxSnapshotLockMs_, snapshotLockMs);
        ++criticalCycles_;
        if (cycleMs > period.count()) {
            ++criticalDeadlineMisses_;
        }
        next = ComputeNextPeriodicWake(next, finished, period);
        std::this_thread::sleep_until(next);
    }
}

/**
 * @brief Periodic lower-priority UI-facing station update cycle.
 */
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

/**
 * @brief Serialize write queue and dispatch gateway writes.
 */
void UpdateCoordinator::WriteLoop()
{
    writeLoopReady_ = true;
    writeCv_.notify_all();
    while (true) {
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
        StoreMax(maxWriteStartDelayMs_, delay);
        if (delay > 100) {
            ++writeStartDelayExceededCount_;
        }
        const auto error = gateway_.Write(request.key, request.value);
        lastWriteErrorCode_ = static_cast<int>(error);
        if (error != BridgeError::Ok) {
            ++writeErrorCount_;
        }
        if (IsScheduleMutationDataId(request.key.dataId)) {
            lastScheduleMutationErrorCode_ = static_cast<int>(error);
            if (error != BridgeError::Ok) {
                ++scheduleMutationErrorCount_;
            }
        }
        if (request.key.dataId == 2103) {
            ++scheduleOrderWriteCompletedCount_;
        } else if (request.key.dataId == 2104) {
            ++scheduleAddCompletedCount_;
        } else if (request.key.dataId == 2105) {
            ++scheduleDeleteCompletedCount_;
        }
        ++writeCompletedCount_;
    }
}

/**
 * @brief Append history batch and trim to capped snapshot window.
 */
void UpdateCoordinator::AppendHistoryRecords(std::vector<HistoryRecord> records)
{
    if (records.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(snapshotMutex_);
    auto& snapshotRecords = snapshot_.historyRecords;
    snapshotRecords.insert(snapshotRecords.end(),
                           std::make_move_iterator(records.begin()),
                           std::make_move_iterator(records.end()));
    if (snapshotRecords.size() > kHistorySnapshotLimit) {
        snapshotRecords.erase(snapshotRecords.begin(),
                              snapshotRecords.begin() + static_cast<std::ptrdiff_t>(snapshotRecords.size() - kHistorySnapshotLimit));
    }
}

/**
 * @brief History read worker with per-day/per-record loops and cancellation/error handling.
 */
void UpdateCoordinator::HistoryLoop(HistoryRequest request)
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    const int totalReads = request.days * kHistoryRecordsPerDay;
    int completedReads = 0;
    int consecutiveErrors = 0;
    bool cancelled = false;
    bool stoppedByErrors = false;
    std::vector<HistoryRecord> batch;
    batch.reserve(kHistoryBatchSize);

    for (int dayOffset = 0; running_ && dayOffset < request.days; ++dayOffset) {
        for (int recordIndex = 0; running_ && recordIndex < kHistoryRecordsPerDay; ++recordIndex) {
            if (historyCancelRequested_.load()) {
                cancelled = true;
                break;
            }

            const auto value = gateway_.Read(MakeHistoryKey(dayOffset, recordIndex));
            const bool emptyValue = value.errorCode == BridgeError::Ok && value.displayText.empty();
            const auto errorCode = emptyValue ? BridgeError::InternalError : value.errorCode;
            batch.push_back({dayOffset, recordIndex, value.displayText, errorCode, value.stale || emptyValue});
            ++historyReadCount_;
            historyLastErrorCode_ = static_cast<int>(errorCode);

            if (errorCode != BridgeError::Ok) {
                ++historyErrorCount_;
                ++consecutiveErrors;
            } else {
                consecutiveErrors = 0;
            }

            ++completedReads;
            historyProgress_ = (completedReads * 100) / totalReads;

            if (batch.size() >= kHistoryBatchSize) {
                AppendHistoryRecords(std::move(batch));
                batch.clear();
                batch.reserve(kHistoryBatchSize);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (consecutiveErrors >= kConsecutiveHistoryErrorLimit) {
                stoppedByErrors = true;
                break;
            }
        }

        if (cancelled || stoppedByErrors) {
            break;
        }
    }

    AppendHistoryRecords(std::move(batch));

    if (!running_.load() && !cancelled) {
        cancelled = true;
    }

    if (cancelled) {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyStatusText = L"履歴中断";
        snapshot_.historyCancelled = true;
    } else if (stoppedByErrors) {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyStatusText = L"履歴エラー停止: " + ToDisplayText(static_cast<BridgeError>(historyLastErrorCode_.load()));
        snapshot_.historyCancelled = false;
    } else {
        historyProgress_ = 100;
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyStatusText = L"履歴取得完了";
        snapshot_.historyCancelled = false;
    }
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.historyRunning = false;
        snapshot_.historyProgress = historyProgress_.load();
    }
    historyRunning_ = false;
}
