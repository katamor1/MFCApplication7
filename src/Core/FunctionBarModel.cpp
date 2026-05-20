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
 */
std::vector<FunctionAction> BuildScheduleFunctionActions(bool hasSelection)
{
    auto actions = EightSlots();
    actions[0] = {1, L"details", L"詳細", hasSelection};
    actions[1] = {2, L"order-change", L"順序変更", hasSelection};
    actions[2] = {3, L"add", L"追加", false};
    actions[3] = {4, L"delete", L"削除", false};
    return actions;
}

/**
 * @brief Build function bar actions for system screen.
 * @param historyRunning Whether history read is running.
 */
std::vector<FunctionAction> BuildSystemFunctionActions(bool historyRunning)
{
    auto actions = EightSlots();
    actions[0] = {1, L"history", historyRunning ? L"取得中" : L"履歴取得", !historyRunning};
    actions[1] = {2, L"history-cancel", L"中断", historyRunning};
    return actions;
}
