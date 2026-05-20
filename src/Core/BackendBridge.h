#pragma once

#include "DataTypes.h"

#include <string>
#include <winsock2.h>
#include <windows.h>
#include <oaidl.h>

struct __declspec(uuid("260B8AF6-FD4C-4060-A407-77D52EF54D30")) IBackendBridgeCom : public IDispatch
{
    virtual HRESULT STDMETHODCALLTYPE Connect(BSTR ipAddress, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Read(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR* value, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode) = 0;
};

struct __declspec(uuid("A737D261-91BC-4646-B58E-9E8B53378D6F")) BackendBridgeComClass;

class IBackendBridge
{
public:
    virtual ~IBackendBridge() = default;
    virtual BridgeError Connect(const std::wstring& ipAddress) = 0;
    virtual BridgeError Read(const DataKey& key, std::wstring& value) = 0;
    virtual BridgeError Write(const DataKey& key, const std::wstring& value) = 0;
};
