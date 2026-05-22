#include "NavigationModel.h"

#include <utility>

/**
 * @file NavigationModel.cpp
 * @brief Core navigation item definitions and layout helpers.
 */

std::vector<NavigationItem> BuildDefaultNavigationItems()
{
    return {
        {MainScreenId::Station, L"station", L"コンテナステーション", L"ST"},
        {MainScreenId::ContainerList, L"container-list", L"コンテナ一覧", L"LIST"},
        {MainScreenId::Schedule, L"schedule", L"コンテナスケジュール", L"SCH"},
        {MainScreenId::System, L"system", L"システム", L"SYS"},
        {MainScreenId::Maintenance, L"maintenance", L"コンテナ保守", L"MNT"},
    };
}

std::vector<NavigationCell> BuildNavigationCells(const std::vector<NavigationItem>& items,
                                                 MainScreenId currentScreen,
                                                 bool expanded)
{
    std::vector<NavigationCell> cells;
    cells.reserve(items.size());
    for (size_t index = 0; index < items.size(); ++index) {
        const int commandIndex = static_cast<int>(index);
        const int column = expanded ? commandIndex % 2 : 0;
        const int row = expanded ? commandIndex / 2 : commandIndex;
        cells.push_back({
            items[index].screen,
            items[index].id,
            items[index].label,
            items[index].shortLabel,
            commandIndex,
            column,
            row,
            items[index].screen == currentScreen,
        });
    }
    return cells;
}

std::wstring NavigationLabelForScreen(const std::vector<NavigationItem>& items, MainScreenId screen)
{
    for (const auto& item : items) {
        if (item.screen == screen) {
            return item.label;
        }
    }
    return {};
}
