#pragma once

#include "DataTypes.h"
#include "MockBackendBridge.h"

#include <memory>
#include <string>
#include <vector>

class DataGateway
{
public:
    explicit DataGateway(std::shared_ptr<IBackendBridge> bridge);

    BridgeError Connect(const std::wstring& ipAddress);
    DataValue Read(const DataKey& key) const;
    std::vector<DataValue> ReadMany(const std::vector<DataKey>& keys) const;
    BridgeError Write(const DataKey& key, const std::wstring& value) const;

private:
    std::shared_ptr<IBackendBridge> bridge_;
};
