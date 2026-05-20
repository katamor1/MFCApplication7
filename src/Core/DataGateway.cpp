#include "DataGateway.h"

DataGateway::DataGateway(std::shared_ptr<IBackendBridge> bridge)
    : bridge_(std::move(bridge))
{
}

BridgeError DataGateway::Connect(const std::wstring& ipAddress)
{
    return bridge_ == nullptr ? BridgeError::InternalError : bridge_->Connect(ipAddress);
}

DataValue DataGateway::Read(const DataKey& key) const
{
    DataValue value;
    value.updatedAt = std::chrono::steady_clock::now();

    if (bridge_ == nullptr) {
        value.errorCode = BridgeError::InternalError;
        value.stale = true;
        return value;
    }

    std::wstring text;
    value.errorCode = bridge_->Read(key, text);
    value.displayText = std::move(text);
    value.stale = value.errorCode != BridgeError::Ok;
    return value;
}

std::vector<DataValue> DataGateway::ReadMany(const std::vector<DataKey>& keys) const
{
    std::vector<DataValue> values;
    values.reserve(keys.size());
    for (const auto& key : keys) {
        values.push_back(Read(key));
    }
    return values;
}

BridgeError DataGateway::Write(const DataKey& key, const std::wstring& value) const
{
    return bridge_ == nullptr ? BridgeError::InternalError : bridge_->Write(key, value);
}
