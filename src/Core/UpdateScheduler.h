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

/**
 * @brief 更新ループ内で扱うジョブ優先度。
 */
enum class WorkPriority
{
    /** 30fps 近辺で繰り返す重要監視。 */
    Critical = 0,
    /** 100ms 応答要件を優先するユーザー入力反映。 */
    UserWrite = 1,
    /** 定常スナップショット更新。 */
    Normal = 2,
    /** 履歴一括取得。 */
    History = 3,
};

/**
 * @brief 優先度付きの内部キュー要素。
 */
struct WorkItem
{
    /** ジョブの重要度。 */
    WorkPriority priority{WorkPriority::Normal};
    /** 同優先度内の FIFO 順序を担保する採番。 */
    unsigned long long sequence{};
    /** ログ用途の簡易ラベル。 */
    std::wstring name;
};

/**
 * @brief 優先度付きワークキューの薄いラッパー。
 */
class PrioritizedWorkQueue
{
public:
    /** キューへジョブを追加する。 */
    void Push(WorkItem item);
    /** キューから次ジョブを取り出す。 */
    WorkItem Pop();
    /** 空かどうかを返す。 */
    bool Empty() const noexcept;

private:
    struct Compare
    {
        /** priority と sequence で順序決定する比較関数。 */
        bool operator()(const WorkItem& left, const WorkItem& right) const noexcept;
    };

    std::priority_queue<WorkItem, std::vector<WorkItem>, Compare> queue_;
};

struct HistoryRequest
{
    /** 直近何日を対象に履歴を読むか。 */
    int days{7};
};

struct ScheduleAddRequest
{
    int containerNo{};
    int itemNo{};
    int order{};
    std::wstring itemName;
};

/**
 * @brief 履歴取得1件分の表示情報。
 */
struct HistoryRecord
{
    int dayOffset{};
    int recordIndex{};
    std::wstring displayText;
    BridgeError errorCode{BridgeError::Ok};
    bool stale{false};
};

bool IsValidHistoryRequest(HistoryRequest request) noexcept;
/** @brief 履歴読み取り用キーを組み立てる。 */
DataKey MakeHistoryKey(int dayOffset, int recordIndex) noexcept;
/** @brief 出庫予定追加要求を検証する。 */
bool IsValidScheduleAddRequest(const ScheduleAddRequest& request) noexcept;
/** @brief 出庫予定追加Write用の値を組み立てる。 */
std::wstring EncodeScheduleAddValue(const ScheduleAddRequest& request);

/**
 * @brief UI が1回の再描画で参照する状態スナップショット。
 */
struct UpdateSnapshot
{
    std::vector<DataValue> criticalValues;
    StationSnapshot station;
    std::vector<HistoryRecord> historyRecords;
    std::wstring historyStatusText;
    int historyProgress{};
    bool historyRunning{false};
    bool historyCancelled{false};
};

struct SchedulerMetrics
{
    int criticalCycles{};
    int criticalDeadlineMisses{};
    int normalCycles{};
    long long lastWriteStartDelayMs{-1};
    int writeCompletedCount{};
    BridgeError lastWriteErrorCode{BridgeError::Ok};
    int scheduleOrderWriteCompletedCount{};
    int scheduleAddCompletedCount{};
    int scheduleDeleteCompletedCount{};
    BridgeError lastScheduleMutationErrorCode{BridgeError::Ok};
    int historyReadCount{};
    int historyErrorCount{};
    int historyCancelCount{};
    BridgeError historyLastErrorCode{BridgeError::Ok};
};

/**
 * @brief 4種類ループ（重要/通常/Write/履歴）を管理し、スナップショットとメトリクスを提供する。
 */
class UpdateCoordinator
{
public:
    /**
     * @brief カタログとゲートウェイを受け取り、初期状態のスナップショットを作る。
     */
    UpdateCoordinator(DataCatalog catalog, DataGateway gateway);
    /**
     * @brief スレッドを停止し、資源を解放する。
     */
    ~UpdateCoordinator();

    UpdateCoordinator(const UpdateCoordinator&) = delete;
    UpdateCoordinator& operator=(const UpdateCoordinator&) = delete;

    /**
     * @brief 監視/更新ループを開始する。
     */
    void Start();
    /**
     * @brief 全ループを停止し、進行中ジョブを終わらせる。
     */
    void Stop();
    /**
     * @brief 画面入力に応じた書き込みリクエストを積む。
     */
    void RequestWrite(DataKey key, std::wstring value);
    /**
     * @brief 出庫履歴取得を開始し、true を返すと受付成功。
     */
    bool StartHistoryLoad(HistoryRequest request);
    /**
     * @brief 履歴取得を中断要求する。
     */
    void CancelHistoryLoad();
    /**
     * @brief ステーション画面の選択コンテナを切り替える。
     */
    void SetSelectedContainer(int containerNo);

    /**
     * @brief 直近状態をコピーして返す。
     */
    UpdateSnapshot Snapshot() const;
    /**
     * @brief 性能・エラー指標を返す。
     */
    SchedulerMetrics Metrics() const noexcept;

private:
    struct WriteRequest
    {
        /** 書き込み対象。 */
        DataKey key;
        /** 書き込み値。 */
        std::wstring value;
        /** キュー投入時刻。 */
        std::chrono::steady_clock::time_point enqueuedAt;
    };

    /** 30fps 近辺で重要値を再読込するループ。 */
    void CriticalLoop();
    /** 500ms ごとにステーション表示を再生成するループ。 */
    void NormalLoop();
    /** 書き込みキューを処理するループ。 */
    void WriteLoop();
    /** 履歴取得を実行するループ。 */
    void HistoryLoop(HistoryRequest request);
    /** 履歴結果を上限付きでスナップショットへ追加する。 */
    void AppendHistoryRecords(std::vector<HistoryRecord> records);

    DataCatalog catalog_;
    DataGateway gateway_;

    std::atomic<bool> running_{false};
    std::atomic<int> selectedContainerNo_{1};
    std::atomic<int> criticalCycles_{0};
    std::atomic<int> criticalDeadlineMisses_{0};
    std::atomic<int> normalCycles_{0};
    std::atomic<int> historyProgress_{0};
    std::atomic<bool> historyRunning_{false};
    std::atomic<bool> historyCancelRequested_{false};
    std::atomic<long long> lastWriteStartDelayMs_{-1};
    std::atomic<int> writeCompletedCount_{0};
    std::atomic<int> lastWriteErrorCode_{static_cast<int>(BridgeError::Ok)};
    std::atomic<int> scheduleOrderWriteCompletedCount_{0};
    std::atomic<int> scheduleAddCompletedCount_{0};
    std::atomic<int> scheduleDeleteCompletedCount_{0};
    std::atomic<int> lastScheduleMutationErrorCode_{static_cast<int>(BridgeError::Ok)};
    std::atomic<int> historyReadCount_{0};
    std::atomic<int> historyErrorCount_{0};
    std::atomic<int> historyCancelCount_{0};
    std::atomic<int> historyLastErrorCode_{static_cast<int>(BridgeError::Ok)};

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
