#include "FunctionBarModel.h"

/**
 * @file FunctionBarModel.cpp
 * @brief Builds reusable function-bar action lists for each dialog screen.
 * @details
 * The model maps active screen state to the set of function-key actions shown
 * by the main dialog. The returned list always allocates eight slots to keep the
 * UI function bar stable across screens.
 */

namespace {

/**
 * @brief Build a vector of 8 empty function actions.
 */
std::vector<FunctionAction> EightSlots()
{
    std::vector<FunctionAction> actions;
    actions.reserve(8);
    for (int slot = 1; slot <= 8; ++slot) {
        actions.push_back({slot, L"", L"", false});
    }
    return actions;
}

} // namespace

std::vector<FunctionAction> BuildBlankFunctionActions()
{
    return EightSlots();
}

/**
 * @brief Build function bar actions for station/container screens.
 * @param hasSelection Whether a container row is currently selected.
 * @param containerMissing Whether selected container is invalid/empty.
 */
std::vector<FunctionAction> BuildContainerFunctionActions(bool hasSelection, bool containerMissing)
{
    auto actions = EightSlots();
    actions[0] = {1, L"details", L"詳細", hasSelection && !containerMissing};
    return actions;
}

/**
 * @brief Build function bar actions for schedule screen.
 * @param hasSelection Whether an item row is selected.
 * @param canMoveUp Whether the selected row can be swapped with the previous visible row.
 */
std::vector<FunctionAction> BuildScheduleFunctionActions(bool hasSelection, bool canMoveUp)
{
    auto actions = EightSlots();
    actions[0] = {1, L"details", L"詳細", hasSelection};
    actions[1] = {2, L"order-change", L"順序変更", hasSelection};
    actions[2] = {3, L"add", L"追加", true};
    actions[3] = {4, L"delete", L"削除", hasSelection};
    actions[4] = {5, L"move-up", L"繰上げ", hasSelection && canMoveUp};
    return actions;
}

/**
 * @brief Map VK_F1..VK_F8 to function bar slots.
 */
int FunctionSlotFromVirtualKey(int virtualKey) noexcept
{
    constexpr int firstFunctionKey = 0x70;
    constexpr int lastFunctionKey = 0x77;
    if (virtualKey < firstFunctionKey || virtualKey > lastFunctionKey) {
        return 0;
    }
    return virtualKey - firstFunctionKey + 1;
}

/**
 * @brief Build function bar actions for system screen.
 * @param historyRunning Whether history read is running.
 * @param hasLaunchableSelection Whether an external app row is selected.
 */
std::vector<FunctionAction> BuildSystemFunctionActions(bool historyRunning, bool hasLaunchableSelection)
{
    auto actions = EightSlots();
    actions[0] = {1, L"history", historyRunning ? L"取得中" : L"履歴取得", !historyRunning};
    actions[1] = {2, L"history-cancel", L"中断", historyRunning};
    actions[2] = {3, L"external-launch", L"起動", hasLaunchableSelection};
    return actions;
}

/**
 * @brief Build function bar actions for maintenance screen.
 * @param abnormalSelection Whether the current row represents an abnormal critical item.
 */
std::vector<FunctionAction> BuildMaintenanceFunctionActions(bool abnormalSelection)
{
    auto actions = EightSlots();
    actions[0] = {1, L"maintenance-details", L"詳細", abnormalSelection};
    return actions;
}
