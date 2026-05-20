#include "FunctionBarModel.h"

namespace {

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

std::vector<FunctionAction> BuildContainerFunctionActions(bool hasSelection, bool containerMissing)
{
    auto actions = EightSlots();
    actions[0] = {1, L"details", L"詳細", hasSelection && !containerMissing};
    return actions;
}

std::vector<FunctionAction> BuildScheduleFunctionActions(bool hasSelection)
{
    auto actions = EightSlots();
    actions[0] = {1, L"details", L"詳細", hasSelection};
    actions[1] = {2, L"order-change", L"順序変更", hasSelection};
    actions[2] = {3, L"add", L"追加", false};
    actions[3] = {4, L"delete", L"削除", false};
    return actions;
}

std::vector<FunctionAction> BuildSystemFunctionActions(bool historyRunning)
{
    auto actions = EightSlots();
    actions[0] = {1, L"history", historyRunning ? L"取得中" : L"履歴", !historyRunning};
    return actions;
}
