#pragma once

#include "BackendBridge.h"

#include <atlbase.h>

class ComBackendBridge final : public IBackendBridge
{
public:
    explicit ComBackendBridge(std::wstring progId);
    ~ComBackendBridge() override;

    BridgeError Connect(const std::wstring& ipAddress) override;
    BridgeError Read(const DataKey& key, std::wstring& value) override;
    BridgeError Write(const DataKey& key, const std::wstring& value) override;

private:
    BridgeError EnsureObject();

    std::wstring progId_;
    bool comUsable_{false};
    bool comInitialized_{false};
    ATL::CComPtr<IDispatch> bridge_;
};
