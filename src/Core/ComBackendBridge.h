#pragma once

#include "BackendBridge.h"

#include <atlbase.h>

#include <mutex>

class ComBackendBridge final : public IBackendBridge
{
public:
    explicit ComBackendBridge(std::wstring progId);
    ~ComBackendBridge() override;

    BridgeError Connect(const std::wstring& ipAddress) override;
    BridgeError Read(const DataKey& key, std::wstring& value) override;
    BridgeError Write(const DataKey& key, const std::wstring& value) override;

private:
    BridgeError EnsureObject(ATL::CComPtr<IDispatch>& dispatch, bool connectIfNeeded);
    BridgeError InvokeConnect(IDispatch* dispatch, const std::wstring& ipAddress);

    std::wstring progId_;
    mutable std::mutex connectionMutex_;
    std::wstring connectedIpAddress_;
    bool hasConnectedIpAddress_{false};
};
