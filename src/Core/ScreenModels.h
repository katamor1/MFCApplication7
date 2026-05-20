#pragma once

#include "DataGateway.h"
#include "GridModel.h"

#include <string>
#include <vector>

struct ContainerItemSummary
{
    std::wstring itemName;
    std::wstring inboundDate;
    std::wstring outboundStart;
    std::wstring outboundOrder;
    std::wstring workTime;
};

struct ContainerSummary
{
    int containerNo{};
    std::wstring containerName;
    std::wstring state;
    bool missing{false};
    std::vector<ContainerItemSummary> items;
};

struct StationSnapshot
{
    std::vector<ContainerSummary> containers;
    ContainerSummary selected;
};

ContainerSummary BuildContainerSummary(const DataGateway& gateway, int containerNo, int maxItems);
StationSnapshot BuildStationSnapshot(const DataGateway& gateway, int selectedContainerNo);
GridModel BuildContainerListGrid(const StationSnapshot& snapshot);
GridModel BuildScheduleGrid(const DataGateway& gateway);
GridModel BuildMaintenanceGrid(const DataGateway& gateway);
