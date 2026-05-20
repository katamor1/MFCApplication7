#pragma once

#include <string>
#include <vector>

/**
 * @brief ショートカットバー1枠の情報。
 */
struct FunctionAction
{
    /** ボタンスロット番号（1-8）。 */
    int slot{};
    /** 遷移先や操作種別を識別する ID。 */
    std::wstring id;
    /** 画面表示ラベル。 */
    std::wstring label;
    /** 押下可能か。 */
    bool enabled{false};
};

/**
 * @brief 空のファンクションバー定義を返す。
 */
std::vector<FunctionAction> BuildBlankFunctionActions();

/**
 * @brief コンテナ画面用アクションを構築する。
 */
std::vector<FunctionAction> BuildContainerFunctionActions(bool hasSelection, bool containerMissing);

/**
 * @brief スケジュール画面用アクションを構築する。
 */
std::vector<FunctionAction> BuildScheduleFunctionActions(bool hasSelection);

/**
 * @brief システム画面用アクションを構築する。
 */
std::vector<FunctionAction> BuildSystemFunctionActions(bool historyRunning);
