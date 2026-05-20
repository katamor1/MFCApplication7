#include "DataGateway.h"

/**
 * @file DataGateway.cpp
 * @brief Thin wrapper that bridges transport-level bridge calls to typed data values.
 */

/**
 * @brief Construct gateway with shared backend bridge.
 * @param bridge Bridge implementation to use.
 */
DataGateway::DataGateway(std::shared_ptr<IBackendBridge> bridge)
    : bridge_(std::move(bridge))
{
}

/**
 * @brief Connect to backend using IP/endpoint argument.
 * @param ipAddress Target endpoint.
 * @return Bridge status.
 */
BridgeError DataGateway::Connect(const std::wstring& ipAddress)
{
    return bridge_ == nullptr ? BridgeError::InternalError : bridge_->Connect(ipAddress);
}

/**
 * @brief Read one value and include freshness/error metadata.
 * @param key Read target.
 * @return DataValue that may carry stale/error state when read fails.
 */
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

/**
 * @brief Read many keys and return values preserving input order.
 * @param keys Read keys in request order.
 * @return Values in same order as keys.
 */
std::vector<DataValue> DataGateway::ReadMany(const std::vector<DataKey>& keys) const
{
    std::vector<DataValue> values;
    values.reserve(keys.size());
    for (const auto& key : keys) {
        values.push_back(Read(key));
    }
    return values;
}

/**
 * @brief Write one value to backend.
 * @param key Target key.
 * @param value Text payload.
 * @return Bridge status.
 */
BridgeError DataGateway::Write(const DataKey& key, const std::wstring& value) const
{
    return bridge_ == nullptr ? BridgeError::InternalError : bridge_->Write(key, value);
}
