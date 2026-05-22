#pragma once

#include <string>
#include <vector>

/**
 * @brief Main dialog screen identifiers shared by Core models and the MFC shell.
 */
enum class MainScreenId
{
    /** コンテナステーション画面。 */
    Station = 0,
    /** コンテナ一覧画面。 */
    ContainerList = 1,
    /** スケジュール画面。 */
    Schedule = 2,
    /** システム画面。 */
    System = 3,
    /** メンテナンス画面。 */
    Maintenance = 4,
};

/**
 * @brief One navigation target definition.
 */
struct NavigationItem
{
    MainScreenId screen{MainScreenId::Station};
    std::wstring id;
    std::wstring label;
    std::wstring shortLabel;
};

/**
 * @brief A rendered navigation item with calculated grid placement.
 */
struct NavigationCell
{
    MainScreenId screen{MainScreenId::Station};
    std::wstring id;
    std::wstring label;
    std::wstring shortLabel;
    int commandIndex{};
    int column{};
    int row{};
    bool selected{false};
};

/**
 * @brief Build the V1 fixed navigation target list in display order.
 */
std::vector<NavigationItem> BuildDefaultNavigationItems();

/**
 * @brief Calculate one-column or two-column navigation cells for display.
 */
std::vector<NavigationCell> BuildNavigationCells(const std::vector<NavigationItem>& items,
                                                 MainScreenId currentScreen,
                                                 bool expanded);

/**
 * @brief Resolve a screen title from navigation definitions.
 */
std::wstring NavigationLabelForScreen(const std::vector<NavigationItem>& items, MainScreenId screen);
