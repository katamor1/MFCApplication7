#pragma once

#include "DataGateway.h"
#include "GridModel.h"

#include <string>
#include <vector>

/**
 * @brief コンテナ簡易情報の品目項目。
 */
struct ContainerItemSummary
{
    /** 品目名。 */
    std::wstring itemName;
    /** 入庫日。 */
    std::wstring inboundDate;
    /** 出庫開始予定。 */
    std::wstring outboundStart;
    /** 出庫順序。 */
    std::wstring outboundOrder;
    /** 出庫作業時間。 */
    std::wstring workTime;
};

/**
 * @brief 1コンテナの要約情報。
 */
struct ContainerSummary
{
    /** コンテナ番号。 */
    int containerNo{};
    /** コンテナ名。 */
    std::wstring containerName;
    /** コンテナ状態。 */
    std::wstring state;
    /** コンテナなし判定。 */
    bool missing{false};
    /** 表示対象の品目一覧。 */
    std::vector<ContainerItemSummary> items;
};

/**
 * @brief ステーション画面向けの全体スナップショット。
 */
struct StationSnapshot
{
    /** 画面全コンテナ。 */
    std::vector<ContainerSummary> containers;
    /** 選択中コンテナ。 */
    ContainerSummary selected;
};

/**
 * @brief 1コンテナ分の表示情報を読み取りし生成する。
 */
ContainerSummary BuildContainerSummary(const DataGateway& gateway, int containerNo, int maxItems);
/**
 * @brief ステーション画面用のコンテナ一覧スナップショットを生成する。
 */
StationSnapshot BuildStationSnapshot(const DataGateway& gateway, int selectedContainerNo);
/**
 * @brief コンテナ一覧画面向けのグリッドデータを構築する。
 */
GridModel BuildContainerListGrid(const StationSnapshot& snapshot);
/**
 * @brief スケジュール画面向けのグリッドデータを構築する。
 */
GridModel BuildScheduleGrid(const DataGateway& gateway);
/**
 * @brief メンテナンス画面向けのグリッドデータを構築する。
 */
GridModel BuildMaintenanceGrid(const DataGateway& gateway);
