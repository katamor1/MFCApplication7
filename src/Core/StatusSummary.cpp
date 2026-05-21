#include "StatusSummary.h"

#include <sstream>

/**
 * @file StatusSummary.cpp
 * @brief Builds the common top-status display model from scheduler state.
 */

namespace {

/**
 * @brief Resolve display name for a data key from catalog metadata.
 */
std::wstring CriticalName(const DataCatalog& catalog, const DataKey& key)
{
    const auto* definition = catalog.FindDefinition(key.dataId);
    if (definition != nullptr && !definition->name.empty()) {
        return definition->name;
    }
    return L"dataId " + std::to_wstring(key.dataId);
}

/**
 * @brief Determine whether a read value represents an abnormal critical state.
 */
bool IsCriticalAbnormal(const CriticalStatusItem& item) noexcept
{
    return item.errorCode != BridgeError::Ok || item.stale;
}

} // namespace

/**
 * @brief Build a stable two-line status string and critical-item details.
 */
StatusSummary BuildStatusSummary(const DataCatalog& catalog,
                                 const UpdateSnapshot& snapshot,
                                 const SchedulerMetrics& metrics,
                                 const StatusContext& context)
{
    StatusSummary summary;
    const auto& criticalKeys = catalog.CriticalKeys();
    summary.criticalItems.reserve(criticalKeys.size());

    for (size_t index = 0; index < criticalKeys.size(); ++index) {
        CriticalStatusItem item;
        item.key = criticalKeys[index];
        item.name = CriticalName(catalog, item.key);
        if (index < snapshot.criticalValues.size()) {
            const auto& value = snapshot.criticalValues[index];
            item.displayText = value.displayText.empty() && value.errorCode != BridgeError::Ok
                ? ToDisplayText(value.errorCode)
                : value.displayText;
            item.errorCode = value.errorCode;
            item.stale = value.stale || value.errorCode != BridgeError::Ok;
        } else {
            item.displayText = L"未取得";
            item.errorCode = BridgeError::InternalError;
            item.stale = true;
        }

        if (IsCriticalAbnormal(item)) {
            ++summary.criticalErrorCount;
        }
        summary.criticalItems.push_back(std::move(item));
    }

    summary.hasCriticalError = summary.criticalErrorCount > 0;
    summary.businessStateText = L"状態不明";
    for (const auto& item : summary.criticalItems) {
        if (item.key.dataId == 1000 && !IsCriticalAbnormal(item) && !item.displayText.empty()) {
            summary.businessStateText = item.displayText;
            break;
        }
    }

    const auto criticalStateText = summary.hasCriticalError
        ? L"異常" + std::to_wstring(summary.criticalErrorCount) + L"件"
        : L"正常";

    std::wostringstream text;
    text << L"日時: " << context.currentDateTimeText
         << L" / ユーザー: " << context.userName
         << L" / 画面: " << context.screenTitle
         << L" / 業務状態: " << summary.businessStateText
         << L" / 重要: " << criticalStateText
         << L"\r\n重要更新: " << metrics.criticalCycles
         << L" / 期限超過: " << metrics.criticalDeadlineMisses
         << L" / 通常更新: " << metrics.normalCycles;

    if (metrics.lastWriteStartDelayMs >= 0) {
        text << L" / 最終Write開始遅延: " << metrics.lastWriteStartDelayMs << L"ms"
             << L" / Write完了: " << metrics.writeCompletedCount
             << L" / 最終Write結果: " << ToDisplayText(metrics.lastWriteErrorCode);
    }

    if (metrics.scheduleOrderWriteCompletedCount > 0 ||
        metrics.scheduleAddCompletedCount > 0 ||
        metrics.scheduleDeleteCompletedCount > 0) {
        text << L" / 予定順序: " << metrics.scheduleOrderWriteCompletedCount
             << L" / 予定追加: " << metrics.scheduleAddCompletedCount
             << L" / 予定削除: " << metrics.scheduleDeleteCompletedCount
             << L" / 予定Write結果: " << ToDisplayText(metrics.lastScheduleMutationErrorCode);
    }

    if (!snapshot.historyStatusText.empty()) {
        text << L" / 履歴: " << snapshot.historyStatusText
             << L" " << snapshot.historyProgress << L"%"
             << L" / 履歴Read: " << metrics.historyReadCount;
        if (metrics.historyErrorCount > 0) {
            text << L" / 履歴エラー: " << metrics.historyErrorCount
                 << L"(" << ToDisplayText(metrics.historyLastErrorCode) << L")";
        }
    }

    summary.displayText = text.str();
    return summary;
}
