#pragma once

#include <string>
#include <vector>

struct FunctionAction
{
    int slot{};
    std::wstring id;
    std::wstring label;
    bool enabled{false};
};

std::vector<FunctionAction> BuildBlankFunctionActions();
std::vector<FunctionAction> BuildContainerFunctionActions(bool hasSelection, bool containerMissing);
std::vector<FunctionAction> BuildScheduleFunctionActions(bool hasSelection);
std::vector<FunctionAction> BuildSystemFunctionActions(bool historyRunning);
