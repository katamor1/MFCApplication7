#pragma once

#include "BackendBridge.h"
#include "DataCatalog.h"

#include <memory>
#include <string>

enum class BridgeMode
{
    InProcessMock,
    Com,
};

struct BridgeFactoryOptions
{
    BridgeMode bridgeMode{BridgeMode::InProcessMock};
    std::wstring progId{L"MFCApplication7.BackendBridgeMock"};
    std::wstring ipAddress{L"127.0.0.1"};
    std::wstring catalogPath{L"config/data_catalog.json"};
};

DataCatalog LoadConfiguredCatalogOrDefault(const std::wstring& catalogPath);
std::shared_ptr<IBackendBridge> CreateBackendBridge(const BridgeFactoryOptions& options);
BridgeFactoryOptions ParseBridgeFactoryOptions(const std::wstring& commandLine);
