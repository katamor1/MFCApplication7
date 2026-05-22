#pragma once

#include "DataCatalog.h"
#include "DataGateway.h"
#include "GridModel.h"

#include <string>
#include <vector>

struct UpdateSnapshot;

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
    /** バックエンドが返した全品目数。 */
    int itemCount{};
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
 * @brief ステーション配置図の固定配置種別。
 */
enum class StationLayoutKind
{
    LeftSemiCircle,
    Straight,
    BottomSemiCircle,
    RightSemiCircle,
};

/**
 * @brief ステーション配置図の1セル。
 */
struct StationLayoutCell
{
    int containerNo{};
    int column{};
    int row{};
    StationLayoutKind kind{StationLayoutKind::Straight};
    std::wstring displayText;
    std::wstring state;
    bool missing{false};
    bool selected{false};
};

/**
 * @brief ステーション配置図の表示モデル。
 */
struct StationLayoutModel
{
    int columnCount{};
    int rowsPerColumn{};
    std::vector<StationLayoutCell> cells;
};

/**
 * @brief コンテナ一覧3列表示の1セル。
 */
struct ContainerListCell
{
    int containerNo{};
    int column{};
    int row{};
    std::wstring displayText;
    std::wstring containerName;
    std::wstring state;
    bool missing{false};
    bool selected{false};
};

/**
 * @brief コンテナ一覧3列表示の表示モデル。
 */
struct ContainerListLayoutModel
{
    int columnCount{};
    int rowCount{};
    std::vector<ContainerListCell> cells;
};

/**
 * @brief スケジュール順序入れ替えで発行するWrite要求。
 */
struct ScheduleOrderWrite
{
    DataKey key;
    std::wstring value;
};

/**
 * @brief 読み取り専用詳細の1行。
 */
struct DetailRow
{
    std::wstring label;
    std::wstring value;
};

/**
 * @brief 読み取り専用詳細ダイアログの表示モデル。
 */
struct ReadOnlyDetailModel
{
    std::wstring title;
    std::vector<DetailRow> rows;
};

/**
 * @brief システム画面から起動できる外部アプリ定義。
 */
struct ExternalAppDefinition
{
    std::wstring id;
    std::wstring label;
    std::wstring executablePath;
    std::wstring arguments;
    std::wstring workingDirectory;
    bool allowMultiple{false};
};

/**
 * @brief 外部アプリ起動の最新結果。
 */
struct ExternalLaunchResult
{
    std::wstring appId;
    bool success{false};
    bool alreadyRunning{false};
    unsigned long errorCode{};
    std::wstring message;
};

/**
 * @brief 保守画面の重要情報1行。
 */
struct MaintenanceStatusRow
{
    int dataId{};
    std::wstring name;
    std::wstring displayText;
    BridgeError errorCode{BridgeError::Ok};
    bool stale{false};
    bool abnormal{false};
    bool operationAvailable{false};
};

/**
 * @brief 保守画面の重要情報一覧モデル。
 */
struct MaintenanceStatusModel
{
    std::vector<MaintenanceStatusRow> rows;
    int abnormalCount{};
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
 * @brief ステーション画面用の固定配置モデルを生成する。
 */
StationLayoutModel BuildStationLayoutModel(const StationSnapshot& snapshot, int selectedContainerNo);
/**
 * @brief コンテナ一覧画面用の3列配置モデルを生成する。
 */
ContainerListLayoutModel BuildContainerListLayoutModel(const StationSnapshot& snapshot, int selectedContainerNo);
/**
 * @brief コンテナ一覧画面向けのグリッドデータを構築する。
 */
GridModel BuildContainerListGrid(const StationSnapshot& snapshot);
/**
 * @brief コンテナ要約から読み取り専用詳細モデルを構築する。
 */
ReadOnlyDetailModel BuildContainerDetailModel(const ContainerSummary& summary);
/**
 * @brief スケジュール画面向けのグリッドデータを構築する。
 */
GridModel BuildScheduleGrid(const DataGateway& gateway);
/**
 * @brief スケジュール行バインドから読み取り専用詳細モデルを構築する。
 */
ReadOnlyDetailModel BuildScheduleDetailModel(const DataGateway& gateway, GridRowBinding binding);
/**
 * @brief 選択行を直前表示行へ繰り上げるための順序Writeを組み立てる。
 */
std::vector<ScheduleOrderWrite> BuildScheduleMoveUpWrites(const GridModel& grid, int selectedRow);
/**
 * @brief システム画面V1の固定外部アプリ定義を返す。
 */
std::vector<ExternalAppDefinition> BuildDefaultExternalAppDefinitions();
/**
 * @brief システム画面向けの外部アプリ/履歴グリッドを構築する。
 */
GridModel BuildSystemGrid(const UpdateSnapshot& snapshot,
                          const std::vector<ExternalAppDefinition>& externalApps,
                          const ExternalLaunchResult* lastLaunchResult);
/**
 * @brief メンテナンス画面向けのグリッドデータを構築する。
 */
GridModel BuildMaintenanceGrid(const DataGateway& gateway);
/**
 * @brief 更新スナップショットから保守画面向けの重要情報状態を構築する。
 */
MaintenanceStatusModel BuildMaintenanceStatusModel(const DataCatalog& catalog, const UpdateSnapshot& snapshot);
/**
 * @brief 保守重要情報行から読み取り専用詳細モデルを構築する。
 */
ReadOnlyDetailModel BuildMaintenanceDetailModel(const MaintenanceStatusRow& row);
/**
 * @brief 保守重要情報状態モデルからグリッドデータを構築する。
 */
GridModel BuildMaintenanceStatusGrid(const MaintenanceStatusModel& model);
