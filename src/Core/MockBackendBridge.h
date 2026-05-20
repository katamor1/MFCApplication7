#pragma once

#include "BackendBridge.h"
#include "DataCatalog.h"

#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>

class MockBackendBridge final : public IBackendBridge
{
public:
    explicit MockBackendBridge(DataCatalog catalog);

    BridgeError Connect(const std::wstring& ipAddress) override;
    BridgeError Read(const DataKey& key, std::wstring& value) override;
    BridgeError Write(const DataKey& key, const std::wstring& value) override;

private:
    std::wstring RawValue(const DataKey& key) const;
    std::wstring FormatValue(const std::wstring& rawValue, DataStyle style) const;

    DataCatalog catalog_;
    bool connected_{false};
    mutable std::shared_mutex mutex_;
    std::map<DataKey, std::wstring> overrides_;
};
