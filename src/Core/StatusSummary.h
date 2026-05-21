#pragma once

#include "DataCatalog.h"
#include "UpdateScheduler.h"

#include <string>
#include <vector>

/**
 * @brief 共通ステータス表示を組み立てるためのUI文脈。
 */
struct StatusContext
{
    std::wstring screenTitle;
    std::wstring currentDateTimeText;
    std::wstring userName;
};

/**
 * @brief 重要情報1件分のステータス表示項目。
 */
struct CriticalStatusItem
{
    DataKey key;
    std::wstring name;
    std::wstring displayText;
    BridgeError errorCode{BridgeError::Ok};
    bool stale{false};
};

/**
 * @brief 上部共通ステータスの表示モデル。
 */
struct StatusSummary
{
    std::wstring displayText;
    std::wstring businessStateText;
    int criticalErrorCount{};
    bool hasCriticalError{false};
    std::vector<CriticalStatusItem> criticalItems;
};

/**
 * @brief カタログ、更新スナップショット、メトリクスから共通ステータス表示を構築する。
 */
StatusSummary BuildStatusSummary(const DataCatalog& catalog,
                                 const UpdateSnapshot& snapshot,
                                 const SchedulerMetrics& metrics,
                                 const StatusContext& context);
